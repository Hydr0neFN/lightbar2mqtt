// WiFi CSI presence sensor: the node pings its gateway, the AP's ICMP replies
// are the only frames we look at, and the per-subcarrier amplitude of their
// L-LTF is published over MQTT so Home Assistant can tell rooms apart.
//
// - Traffic is generated locally by a ping to the gateway, so no AP side setup
//   and no other traffic on the air is needed. Each reply is one CSI frame.
// - Frames from other transmitters are dropped by comparing info->mac with the
//   BSSID we are associated to (other OFDM frames from the AP also count).
// - subscribe() may run before setup() (first MQTT connect); then it subscribes
//   nothing and the cmd topic is only subscribed by the BLE module (same topic)
//   or after the next reconnect. Keep the node name equal to BLE_NODE_NAME.
// - CSI needs CONFIG_ESP_WIFI_CSI_ENABLED (true in both Arduino-ESP32 cores used
//   here). It also works with WiFi modem sleep: the 25 Hz traffic keeps the
//   radio on by itself, so no extra power setting is needed.
// - OTA: ArduinoOTA blocks loop() for the whole transfer, so a 250 ms esp_timer
//   ("csi_guard") stops CSI and ping as soon as Update.isRunning() turns true,
//   and loop() holds them off through otaHold until the transfer is over.
//   setPaused() is the same thing for an update started by the application.
#ifndef CSI_PRESENCE_H
#define CSI_PRESENCE_H

#include <Arduino.h>

namespace csipresence
{
    typedef bool (*CsiPublishFn)(const char *topic, const char *payload, bool retain);

    // Call once WiFi is up. Publishes to presence/<node>/csi.
    void setup(const char *node, CsiPublishFn publish);
    // Cheap: state machine and one publish. Call from every loop() iteration.
    void loop();
    // True only for presence/<node>/cmd payloads starting with "csi_" (csi_on /
    // csi_off). Call before the BLE handler, which swallows all presence/ topics.
    bool onMessage(const char *topic, const uint8_t *payload, unsigned int length);
    // Calls sub() for each topic we need, after every (re)connect.
    void subscribe(void (*sub)(const char *topic));
    // External pause (OTA and similar). loop() stops CSI on its next call.
    void setPaused(bool p);
    // True while CSI and the ping session are active.
    bool running();
}

#endif
