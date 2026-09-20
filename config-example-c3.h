#ifndef CONFIG_H
#define CONFIG_H
// Include guard added by the fork. sensors.h includes this file so that
// sensors.cpp -- its own translation unit -- actually sees these settings
// instead of silently falling back to its own #ifndef defaults. Without the
// guard, Lightbar.ino would pull config.h in twice and the constexpr arrays
// below would be redefined.

// lightbar2mqtt configuration — Xiaomi Mi Computer Monitor Light Bar (MJGJD01YL)
// Host: ESP32-C3 Pro Mini (silkscreen ESP32-C3_MINI_V1)
// Written 2026-09-04. Target: nl-pi Home Assistant, Groningen NL.
//
// MODE B — DECOUPLED. LIGHTBARS[] below carries a serial that is deliberately
// NOT the original remote's. Once the light bar is paired to it, the physical
// remote stops controlling the bar; the ESP becomes the only control path, so
// Home Assistant's state is finally trustworthy. The remote keeps transmitting
// and this firmware keeps listening to it, so it survives as a pure HA input
// (6 actions -> device_automation triggers). This is what upstream recommends:
// in the coupled mode the remote can change the bar without HA ever knowing.

#include "constants.h"

/* -- nRF24 --------------------------------------------------------------- */
// ESP32-C3 native FSPI pins. These match the board's own silkscreen labels
// (SCK/MISO/MOSI/SS on GPIO 4/5/6/7), so nothing exotic is going on here.
// GPIO10 is the only fully unencumbered pin left, hence CE.
//
// Deliberately avoided:
//   GPIO8  - strapping pin AND the onboard LED
//   GPIO9  - strapping pin (BOOT button)
//   GPIO20 - UART RX  } needed for the serial monitor, which is the only way
//   GPIO21 - UART TX  } to read the remote's serial in step 7 below
//
// All five are consumed: Lightbar.ino:59 calls SPI.begin(SCK, MISO, MOSI) and
// Lightbar.ino:13 constructs Radio(CE, CSN).
#define RADIO_PIN_SCK   4
#define RADIO_PIN_MISO  5
#define RADIO_PIN_MOSI  6
#define RADIO_PIN_CSN   7
#define RADIO_PIN_CE    10

/* -- Light Bars ---------------------------------------------------------- */
// A serial that is NOT any remote's -> decoupled (mode B). Any 24-bit value
// works; this one is arbitrary. Pairing procedure, AFTER flashing:
//   1. power-cycle the light bar
//   2. within 10 seconds, press "Pair" on the HA device page
//      (or publish anything to .../0xa11ce0/pair)
//   3. the bar blinks a few times = paired
// From then on the physical remote no longer drives the bar.
constexpr SerialWithName LIGHTBARS[] = {
    {0xA11CE0, "Monitor Light Bar"},
};

/* -- Remotes ------------------------------------------------------------- */
// <<< PLACEHOLDER — REPLACE AFTER THE FIRST FLASH >>>
// You cannot know this value in advance; it is burned into the remote. Flash
// once with the placeholder, open the serial monitor at 115200, then press or
// turn the knob. The firmware will print the serial of the packet it is
// ignoring, e.g.
//     [Radio] Ignoring package with unknown serial: 0x7B7E12
// Put that value here and reflash. Do this BEFORE pairing, so the remote is
// still doing something you can observe.
//
// Skipping this step costs you only the remote-as-HA-input feature; light bar
// control works regardless.
constexpr SerialWithName REMOTES[] = {
    {0x123456, "Lightbar Remote"},
};

/* -- WiFi ---------------------------------------------------------------- */
// 2.4 GHz only, which is all an ESP32-C3 can do anyway.
#define WIFI_SSID     "<Your WiFi SSID>"
#define WIFI_PASSWORD "<Your WiFi password>"

/* -- MQTT ---------------------------------------------------------------- */
// nl-pi. Mosquitto 2.1.2, anonymous access is refused (verified), so the
// dedicated `lightbar` account below is required. Home Assistant connects to
// the same broker with its own separate `homeassistant` account.
// Credentials also live at /root/mqtt-credentials.env on nl-pi, mode 600.
#define MQTT_SERVER   "<Your MQTT broker IP>"
#define MQTT_PORT     1883
#define MQTT_USER     "lightbar"
#define MQTT_PASSWORD "<Your MQTT password>"

#define MQTT_ROOT_TOPIC "lightbar2mqtt"

/* -- Home Assistant Device Discovery -------------------------------------- */
// Verified working end to end on 2026-09-04: a probe config published to
// homeassistant/sensor/.../config appeared as an entity within 11 s.
#define HOME_ASSISTANT_DISCOVERY true
#define HOME_ASSISTANT_DISCOVERY_PREFIX "homeassistant"
#define HOME_ASSISTANT_DEVICE_NAME "Mi Computer Monitor Light Bar"

/* -- Time ----------------------------------------------------------------- */
// Europe/Amsterdam: CET = UTC+1, CEST adds another hour.
#define NTP_SERVER "pool.ntp.org"
#define GMT_OFFSET_SEC 3600
#define DST_OFFSET_SEC 3600

/* -- Environment sensors (fork addition) ---------------------------------- */
// BME280 + Sensirion SCD41 on one I2C bus. Not part of upstream lightbar2mqtt.
//
// GPIO0/GPIO1 rather than the GPIO8/GPIO9 the board prints as SDA/SCL: 8 and 9
// are ESP32-C3 strapping pins, so a sensor holding SDA low during a bus hang at
// power-up would stop the board booting at all, and the symptom would look like
// a dead board rather than a sensor fault. GPIO0/1 carry no strapping duty and
// the C3's GPIO matrix routes I2C to any pin at no cost.
#define SENSOR_PIN_SDA 0
#define SENSOR_PIN_SCL 1

// The SCD4x tops out at 100 kHz; the BME280 would do 400 kHz but a shared bus
// runs at its slowest device. Setting 400 kHz here does not fail cleanly — it
// shows up as intermittent NACKs and implausible values.
#define SENSOR_I2C_CLOCK 100000

#define SENSOR_UPDATE_INTERVAL_MS 60000

// Compensates the SCD41's self-heating next to a WiFi SoC. Sensirion's default
// is 4 C. The correct figure depends on the enclosure and must be measured in
// the final position against a reference thermometer — compare the "SCD41
// Temperature" diagnostic entity against the BME280 one and adjust.
#define SENSOR_SCD4X_TEMP_OFFSET 4.0f
/* -- Over-the-air updates (fork addition) -------------------------------- *
 * Defining OTA_PASSWORD is what enables OTA at all; leave it out and no OTA
 * code is compiled in. The password is not optional when it is defined: this
 * firmware carries the WiFi PSK and the MQTT password in its image, so an
 * unauthenticated OTA port would hand both to anyone on the LAN.
 *
 * Upload with:  pio run -e esp32-c3-pro-mini-ota -t upload
 */
#define OTA_PASSWORD "CHANGE_ME"
#define OTA_PORT 3232
#endif // CONFIG_H
