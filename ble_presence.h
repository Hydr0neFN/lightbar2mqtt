// BLE presence scanner (this fork): passive RSSI scan of Apple Continuity
// adverts, optionally resolved against iPhone IRKs, published over MQTT so Home
// Assistant can compare signal strength between rooms' anchors.
#ifndef BLE_PRESENCE_H
#define BLE_PRESENCE_H

#include <Arduino.h>

class MQTT;

namespace blepresence
{
    // Starts NimBLE and the passive scan. Call after WiFi and MQTT are up.
    void setup(MQTT *mqtt);
    // Publishes the aggregate every BLE_PUBLISH_MS and keeps the scan alive.
    void loop();
    // Handles presence/config/irk and presence/<node>/cmd. Returns true when
    // the topic was ours (the caller must then not parse the payload as a
    // light bar command).
    bool onMessage(const char *topic, const uint8_t *payload, unsigned int length);
    // Calls sub() for each topic we need; invoked by MQTT::setup() after every
    // (re)connect.
    void subscribe(void (*sub)(const char *topic));
}

#endif
