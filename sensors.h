// Environment sensors — BME280 + Sensirion SCD41 on one I2C bus.
//
// This file and sensors.cpp are additions to ebinf/lightbar2mqtt, not part of
// it. They are kept in separate files on purpose: upstream's own sources stay
// almost untouched, so this fork remains mergeable.
//
// Design constraint that shapes everything here: Radio::loop() polls the nRF24
// for packets from the remote, and a packet missed is a remote press lost.
// Nothing in this module may block. All timing is millis()-driven, and the only
// long call (SCD41 stopPeriodicMeasurement, ~500 ms) is confined to setup().

#ifndef SENSORS_H
#define SENSORS_H

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <Adafruit_BME280.h>
#include <SensirionI2CScd4x.h>
#include <esp_system.h>

#include "mqtt.h"

// Not optional, and not merely tidy: sensors.cpp is its own translation unit
// and includes only this header. Without config.h here, every #ifndef below
// wins and the user's settings are silently ignored in exactly the file that
// uses them. That went unnoticed until 2026-09-06 because the defaults
// happened to equal the configured values for the pins, the clock and the
// interval — the first setting to differ (SENSOR_SCD4X_TEMP_OFFSET) was the
// first to expose it.
#include "config.h"

// I2C pins. GPIO0/GPIO1 rather than the GPIO8/GPIO9 printed on the board:
// 8 and 9 are ESP32-C3 strapping pins, so a sensor holding SDA low during a
// bus hang at power-up would stop the board booting at all. GPIO0/1 have no
// strapping role, and the C3's GPIO matrix routes I2C anywhere at no cost.
#ifndef SENSOR_PIN_SDA
#define SENSOR_PIN_SDA 0
#endif
#ifndef SENSOR_PIN_SCL
#define SENSOR_PIN_SCL 1
#endif

// 100 kHz, not 400 kHz. The SCD4x tops out at 100 kHz while the BME280 would
// manage 400 kHz, and a shared bus runs at the speed of its slowest device.
// Overclocking it does not fail cleanly — it shows up as intermittent NACKs and
// implausible readings that look like a dying sensor.
#ifndef SENSOR_I2C_CLOCK
#define SENSOR_I2C_CLOCK 100000
#endif

#ifndef SENSOR_UPDATE_INTERVAL_MS
#define SENSOR_UPDATE_INTERVAL_MS 60000
#endif

// The SCD41 sits next to a WiFi SoC and its own readings drive the CO2
// compensation, so self-heating costs accuracy twice. Sensirion's default
// offset is 4 C; the right value depends on the enclosure and must be measured
// against a reference thermometer once the thing is in its final position.
#ifndef SENSOR_SCD4X_TEMP_OFFSET
#define SENSOR_SCD4X_TEMP_OFFSET 4.0f
#endif

class Sensors
{
public:
    Sensors(MQTT *mqtt);
    void setup();
    void loop();

private:
    MQTT *mqtt;
    Adafruit_BME280 bme;
    SensirionI2CScd4x scd4x;

    bool bmePresent = false;
    bool scdPresent = false;
    uint8_t bmeAddress = 0x76;
    // SCD41 settings read back from the sensor at boot, published retained
    // because the board is headless once it leaves the PC.
    String scdConfig;
    unsigned long lastUpdate = 0;

    // Last good readings. Kept so that a single failed read does not produce a
    // state message with a key missing, which would make the matching entity's
    // value template throw in Home Assistant on every subsequent update.
    float lastTemperature = 0;
    float lastHumidity = 0;
    float lastPressure = 0;
    uint16_t lastCo2 = 0;
    float lastScdTemperature = 0;
    float lastScdHumidity = 0;
    bool haveScdReading = false;

    void sendHomeAssistantDiscoveryMessages();
    // numeric = false drops the state_class. A text state carrying
    // state_class "measurement" is rejected by Home Assistant's recorder, so
    // reset_reason would land as an entity that logs an error every update.
    void sendOneDiscoveryMessage(const char *key, const char *name,
                                 const char *deviceClass, const char *unit,
                                 const char *icon, bool diagnostic,
                                 bool numeric = true);
    // Why the last boot happened. Constant for the life of a boot, published
    // anyway in every state message so it survives a Home Assistant restart
    // that the ESP does not witness.
    static const char *resetReasonName();
    const String deviceId();
};

#endif
