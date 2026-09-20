#include <WiFi.h>
#include <SPI.h>

#include "constants.h"
#include "config.h"
#include "config_defaults.h"
#include "radio.h"
#include "lightbar.h"
#include "mqtt.h"
#include "sensors.h"
#include "time.h"

#ifdef OTA_PASSWORD
#include <ArduinoOTA.h>
// ArduinoOTA defaults to 3232 internally but exposes no getter, so the value
// has to be known here to print it.
#ifndef OTA_PORT
#define OTA_PORT 3232
#endif
#endif

WiFiClient wifiClient;
Radio radio(RADIO_PIN_CE, RADIO_PIN_CSN);
MQTT mqtt(&wifiClient, MQTT_SERVER, MQTT_PORT, MQTT_USER, MQTT_PASSWORD, MQTT_ROOT_TOPIC, HOME_ASSISTANT_DISCOVERY, HOME_ASSISTANT_DISCOVERY_PREFIX);
Sensors sensors(&mqtt);

void setupOta();

void setupWifi()
{
  Serial.print("[WiFi] Connecting to network \"");
  Serial.print(WIFI_SSID);
  Serial.print("\"...");

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  WiFi.setHostname(mqtt.getClientId().c_str());
  // Modem sleep is the default and it is wrong for this device. It parks the
  // radio between DTIM beacons, which is why ping latency here swings from
  // 4 ms to 157 ms, and why MQTT keepalives and TCP ACKs get lost often enough
  // to produce a few hundred reconnects a day. The board is mains powered, so
  // the ~20 mA saved buys nothing and costs the link.
  WiFi.setSleep(false);
  // Never let the ESP negotiate a lower rate than it has to; also stops the
  // station from wandering when the AP is marginal.
  WiFi.setAutoReconnect(true);

  uint retries = 0;
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(1000);
    Serial.print(".");
    retries++;
    if (retries > 60)
      ESP.restart();
  }
  Serial.println();
  Serial.println("[WiFi] connected!");

  Serial.print("[WiFi] IP address: ");
  Serial.println(WiFi.localIP());

  // init and get the time
  #if defined(GMT_OFFSET_SEC) && defined(DST_OFFSET_SEC) && defined(NTP_SERVER)
    Serial.println("[Time] Syncing time with NTP server...");
    configTime(GMT_OFFSET_SEC, DST_OFFSET_SEC, NTP_SERVER);
    printLocalTime();
  #else
    Serial.println("[Time] NTP server not configured, skipping time sync.");
  #endif
}

void setup()
{
  Serial.begin(115200);
  Serial.println("##########################################");
  Serial.println("# LIGHTBAR2MQTT            (Version " + constants::VERSION + ") #");
  Serial.println("# https://github.com/ebinf/lightbar2mqtt #");
  Serial.println("##########################################");

  SPI.end();
  SPI.begin(RADIO_PIN_SCK, RADIO_PIN_MISO, RADIO_PIN_MOSI);

  // LOCAL ADDITION — radio.setup() blocks for up to 60 s and then reboots when
  // no nRF24 answers, which sits *before* setupWifi() and so makes it
  // impossible to test WiFi/MQTT until the radio is physically wired. The
  // `wifitest` env defines this to bring the network up on bare hardware.
#ifdef WIFI_TEST_NO_RADIO
  Serial.println("[Radio] SKIPPED - WIFI_TEST_NO_RADIO build, network test only");
#else
  radio.setup();
#endif

  setupWifi();

  for (int i = 0; i < sizeof(REMOTES) / sizeof(SerialWithName); i++)
  {
    Remote *remote = new Remote(&radio, REMOTES[i].serial, REMOTES[i].name);
    mqtt.addRemote(remote);
  }

  for (int i = 0; i < sizeof(LIGHTBARS) / sizeof(SerialWithName); i++)
  {
    Lightbar *lightbar = new Lightbar(&radio, LIGHTBARS[i].serial, LIGHTBARS[i].name);
    mqtt.addLightbar(lightbar);
  }

  mqtt.setup();

  // After mqtt.setup(), never before: that call is what connects to the broker,
  // and Sensors::setup() publishes its discovery messages immediately.
  sensors.setup();

  setupOta();
}

void setupOta()
{
#ifdef OTA_PASSWORD
  // Must precede begin(): ArduinoOTAClass ignores every setter once
  // _initialized is true.
  ArduinoOTA.setHostname(mqtt.getClientId().c_str());
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.setPort(OTA_PORT);

  ArduinoOTA.onStart([]() {
    // Nothing to tear down: _runUpdate() blocks until the image is written and
    // then reboots, so loop() -- and with it the radio and the MQTT client --
    // simply stops being called. An nRF24 FIFO that overflows meanwhile costs
    // nothing, because those packets are for a firmware that is being replaced.
    Serial.println("[OTA] Update starting...");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\n[OTA] Done, rebooting...");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("[OTA] %u%%\r", (progress * 100) / total);
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("[OTA] Error %u: ", error);
    switch (error)
    {
    case OTA_AUTH_ERROR:    Serial.println("auth failed");    break;
    case OTA_BEGIN_ERROR:   Serial.println("begin failed");   break;
    case OTA_CONNECT_ERROR: Serial.println("connect failed"); break;
    case OTA_RECEIVE_ERROR: Serial.println("receive failed"); break;
    case OTA_END_ERROR:     Serial.println("end failed");     break;
    }
  });

  ArduinoOTA.begin();
  Serial.print("[OTA] Listening on ");
  Serial.print(WiFi.localIP());
  Serial.print(":");
  Serial.println(OTA_PORT);
#endif
}

void loop()
{
  if (!WiFi.isConnected())
  {
    Serial.println("[WiFi] connection lost!");
    setupWifi();
  }

#ifdef OTA_PASSWORD
  // Deliberately ahead of mqtt.loop(): that call spends real time retrying a
  // dead broker, and an update must still land when the broker is the thing
  // that is broken. handle() itself only polls a UDP socket.
  ArduinoOTA.handle();
#endif

  mqtt.loop();
  radio.loop();
  // Returns immediately except once per SENSOR_UPDATE_INTERVAL_MS. It must stay
  // that way — radio.loop() polls the nRF24 for the remote's packets, and any
  // blocking I2C wait here would be dropped button presses.
  sensors.loop();
}

void printLocalTime()
{
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    return;
  }
  Serial.println(&timeinfo, "[Time] %A, %B %d %Y %H:%M:%S");
}
