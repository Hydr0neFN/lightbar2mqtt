#include "sensors.h"

Sensors::Sensors(MQTT *mqtt)
{
    this->mqtt = mqtt;
}

const String Sensors::deviceId()
{
    return this->mqtt->getClientId() + "_env";
}

void Sensors::setup()
{
    Wire.begin(SENSOR_PIN_SDA, SENSOR_PIN_SCL);
    Wire.setClock(SENSOR_I2C_CLOCK);

    // 0x76 is the usual address; the module answers on 0x77 instead when its
    // SDO pin is tied high, which differs between breakout vendors. Try both
    // rather than letting the user discover it by getting no readings.
    this->bmePresent = this->bme.begin(0x76, &Wire);
    if (this->bmePresent)
    {
        this->bmeAddress = 0x76;
    }
    else
    {
        this->bmePresent = this->bme.begin(0x77, &Wire);
        if (this->bmePresent)
            this->bmeAddress = 0x77;
    }

    Serial.print("[Sensors] BME280: ");
    if (this->bmePresent)
    {
        Serial.print("found at 0x");
        Serial.println(this->bmeAddress, HEX);
        // Weather-station preset: forced mode, 1x oversampling, filter off.
        // Continuous sampling would self-heat the die and bias the very reading
        // being taken, which matters more here than sample rate does.
        this->bme.setSampling(Adafruit_BME280::MODE_FORCED,
                              Adafruit_BME280::SAMPLING_X1,
                              Adafruit_BME280::SAMPLING_X1,
                              Adafruit_BME280::SAMPLING_X1,
                              Adafruit_BME280::FILTER_OFF);
    }
    else
    {
        Serial.println("NOT FOUND (checked 0x76 and 0x77)");
    }

    this->scd4x.begin(Wire);
    // A warm reset does not stop an SCD41 that is already measuring, and
    // startPeriodicMeasurement() fails while one is in progress. This is the
    // only blocking call in the module (~500 ms) and is why it lives here in
    // setup() rather than anywhere loop() can reach.
    this->scd4x.stopPeriodicMeasurement();

    uint16_t serial0, serial1, serial2;
    uint16_t error = this->scd4x.getSerialNumber(serial0, serial1, serial2);
    this->scdPresent = (error == 0);

    Serial.print("[Sensors] SCD41: ");
    if (this->scdPresent)
    {
        Serial.print("found, serial 0x");
        Serial.print(serial0, HEX);
        Serial.print(serial1, HEX);
        Serial.println(serial2, HEX);
        this->scd4x.setTemperatureOffset(SENSOR_SCD4X_TEMP_OFFSET);

        // Read back what the sensor actually holds. Every SCD4x getter fails
        // while a periodic measurement is running, so this is the only window
        // in which these can be asked at all. Without them the offset is a
        // value we believe we wrote rather than one we know is in effect.
        float readbackOffset = 0;
        uint16_t altitude = 0, asc = 0;
        this->scd4x.getTemperatureOffset(readbackOffset);
        this->scd4x.getSensorAltitude(altitude);
        this->scd4x.getAutomaticSelfCalibration(asc);
        Serial.print("[Sensors] SCD41 config: temp_offset=");
        Serial.print(readbackOffset, 2);
        Serial.print(" C, altitude=");
        Serial.print(altitude);
        Serial.print(" m, ASC=");
        // ASC assumes the lowest CO2 seen over ~7 days is 400 ppm outdoor air.
        // That holds in a room that gets aired and fails in one that never is,
        // where it will drag the baseline down to whatever the minimum was.
        Serial.println(asc ? "on" : "off");
        this->scdConfig = "{\"temp_offset\":" + String(readbackOffset, 2) +
                          ",\"altitude_m\":" + String(altitude) +
                          ",\"asc\":" + String(asc ? 1 : 0) + "}";

        this->scd4x.startPeriodicMeasurement();
    }
    else
    {
        Serial.print("NOT FOUND (error ");
        Serial.print(error);
        Serial.println(")");
    }

    if (this->bmePresent || this->scdPresent)
        this->sendHomeAssistantDiscoveryMessages();

    if (this->scdPresent && this->scdConfig.length())
    {
        const String topic = this->mqtt->getCombinedRootTopic() + "/env/config";
        // Retained: this is what the sensor was configured with at boot, and it
        // has to still be readable hours later when the offset is being tuned.
        this->mqtt->publish(topic.c_str(), this->scdConfig.c_str(), true);
        Serial.print("[Sensors] config -> MQTT: ");
        Serial.println(this->scdConfig);
    }
    else
        Serial.println("[Sensors] Nothing on the bus - check SDA/SCL and pull-ups.");

    // The first publish waits a full interval on purpose. The SCD41 needs ~5 s
    // in periodic mode before it has anything, and publishing early would mean
    // a message missing the co2 key, which every CO2 entity's value template
    // would then fail on.
    this->lastUpdate = millis();
}

void Sensors::loop()
{
    if (!this->bmePresent && !this->scdPresent)
        return;

    // Unsigned subtraction, so this stays correct across the millis() rollover
    // at ~49.7 days. `millis() > last + interval` would not.
    if (millis() - this->lastUpdate < SENSOR_UPDATE_INTERVAL_MS)
        return;
    this->lastUpdate = millis();

    if (this->bmePresent)
    {
        this->bme.takeForcedMeasurement();
        float t = this->bme.readTemperature();
        float h = this->bme.readHumidity();
        float p = this->bme.readPressure() / 100.0f;
        // NaN is what the library returns for a read that did not happen. Keep
        // the previous value rather than publishing NaN, which HA would show as
        // an unavailable entity and which also poisons the pressure
        // compensation below.
        if (!isnan(t))
            this->lastTemperature = t;
        if (!isnan(h))
            this->lastHumidity = h;
        if (!isnan(p))
            this->lastPressure = p;
    }

    if (this->scdPresent)
    {
        // Feed the BME280's pressure to the SCD41. NDIR CO2 measurement is
        // pressure-dependent, so this is not decoration — it is the reason
        // these two sensors earn their place on the same bus.
        // setAmbientPressure() wants hPa; readPressure() returns Pa.
        if (this->bmePresent && this->lastPressure > 300 && this->lastPressure < 1200)
            this->scd4x.setAmbientPressure((uint16_t)this->lastPressure);

        bool ready = false;
        if (this->scd4x.getDataReadyFlag(ready) == 0 && ready)
        {
            uint16_t co2 = 0;
            float scdT = 0, scdH = 0;
            // co2 == 0 is the sensor's own way of saying the reading is not
            // valid yet, so it is rejected rather than published as 0 ppm.
            if (this->scd4x.readMeasurement(co2, scdT, scdH) == 0 && co2 != 0)
            {
                this->lastCo2 = co2;
                this->lastScdTemperature = scdT;
                this->lastScdHumidity = scdH;
                this->haveScdReading = true;
            }
        }
    }

    // Every key a discovery message referenced is emitted every time, using the
    // cached value when this cycle's read failed. A missing key would make the
    // corresponding entity's value template throw on every update.
    String payload = "{";
    bool first = true;

    if (this->bmePresent)
    {
        payload += "\"temperature\":" + String(this->lastTemperature, 2);
        payload += ",\"humidity\":" + String(this->lastHumidity, 2);
        payload += ",\"pressure\":" + String(this->lastPressure, 2);
        first = false;
    }

    if (this->scdPresent && this->haveScdReading)
    {
        if (!first)
            payload += ",";
        payload += "\"co2\":" + String(this->lastCo2);
        // Diagnostics: the SCD41's own temperature and humidity are what its
        // internal compensation actually uses, so seeing them is how
        // SENSOR_SCD4X_TEMP_OFFSET gets tuned. Trust the BME280 for the room.
        payload += ",\"scd_temperature\":" + String(this->lastScdTemperature, 2);
        payload += ",\"scd_humidity\":" + String(this->lastScdHumidity, 2);
        // The gap between the two temperatures is the thing that has to settle
        // before SENSOR_SCD4X_TEMP_OFFSET can be tuned: a moved assembly takes
        // hours to reach thermal equilibrium, and an offset computed before
        // then is calibrating the transient. Publishing it makes the settling
        // visible as a flat line in Home Assistant rather than a guess.
        if (this->bmePresent)
            payload += ",\"t_delta\":" + String(this->lastScdTemperature - this->lastTemperature, 2);
        first = false;
    }

    // Link diagnostics ride along in the message that already goes out every
    // cycle. They exist because 2026-09-08 turned up ~300 MQTT reconnects a
    // day with no way to see why: RSSI says whether it is signal, and the
    // connect count against uptime says how bad the churn is.
    if (!first)
        payload += ",";
    payload += "\"rssi\":" + String(WiFi.RSSI());
    payload += ",\"uptime_s\":" + String(millis() / 1000UL);
    payload += ",\"mqtt_connects\":" + String(this->mqtt->getConnectCount());
    // The board is powered from a PC USB port because no wall socket is free,
    // so "did it lose power" and "did it crash" are the two live hypotheses for
    // the ~3 reboots a day seen since 2026-09-08, and nothing on the wire could
    // tell them apart. POWERON/BROWNOUT means the supply; PANIC/*_WDT means the
    // firmware.
    payload += ",\"reset_reason\":\"" + String(Sensors::resetReasonName()) + "\"";
    first = false;

    payload += "}";

    if (first)
        return; // nothing has ever been read successfully

    const String topic = this->mqtt->getCombinedRootTopic() + "/env/state";
    this->mqtt->publish(topic.c_str(), payload.c_str(), false);
    Serial.print("[Sensors] ");
    Serial.println(payload);
}

const char *Sensors::resetReasonName()
{
    switch (esp_reset_reason())
    {
    case ESP_RST_POWERON:
        return "POWERON";
    case ESP_RST_EXT:
        return "EXT";
    case ESP_RST_SW:
        return "SW";
    case ESP_RST_PANIC:
        return "PANIC";
    case ESP_RST_INT_WDT:
        return "INT_WDT";
    case ESP_RST_TASK_WDT:
        return "TASK_WDT";
    case ESP_RST_WDT:
        return "WDT";
    case ESP_RST_DEEPSLEEP:
        return "DEEPSLEEP";
    case ESP_RST_BROWNOUT:
        return "BROWNOUT";
    case ESP_RST_SDIO:
        return "SDIO";
    default:
        return "UNKNOWN";
    }
}

void Sensors::sendOneDiscoveryMessage(const char *key, const char *name,
                                      const char *deviceClass, const char *unit,
                                      const char *icon, bool diagnostic,
                                      bool numeric)
{
    const String base = this->mqtt->getCombinedRootTopic() + "/env";

    String cfg = "{";
    cfg += "\"~\":\"" + base + "\"";
    cfg += ",\"availability_topic\":\"" + this->mqtt->getCombinedRootTopic() + "/availability\"";
    cfg += ",\"o\":{\"name\":\"lightbar2mqtt\",\"sw_version\":\"" + constants::VERSION +
           "\",\"support_url\":\"https://github.com/ebinf/lightbar2mqtt\"}";
    cfg += ",\"dev\":{\"ids\":\"" + this->deviceId() + "\"";
    cfg += ",\"name\":\"Room Environment\"";
    cfg += ",\"mdl\":\"BME280 + SCD41\"";
    cfg += ",\"mf\":\"Bosch / Sensirion\"";
    cfg += ",\"sw\":\"lightbar2mqtt " + constants::VERSION + "\"}";
    cfg += ",\"name\":\"" + String(name) + "\"";
    cfg += ",\"uniq_id\":\"" + this->deviceId() + "_" + String(key) + "\"";
    // One state message feeds every entity, so a publish cycle costs one
    // message rather than one per value.
    cfg += ",\"stat_t\":\"~/state\"";
    cfg += ",\"val_tpl\":\"{{ value_json." + String(key) + " }}\"";
    if (numeric)
        cfg += ",\"stat_cla\":\"measurement\"";
    if (deviceClass != nullptr)
        cfg += ",\"dev_cla\":\"" + String(deviceClass) + "\"";
    if (unit != nullptr)
        cfg += ",\"unit_of_meas\":\"" + String(unit) + "\"";
    if (icon != nullptr)
        cfg += ",\"icon\":\"" + String(icon) + "\"";
    if (diagnostic)
        cfg += ",\"ent_cat\":\"diagnostic\"";
    cfg += "}";

    const String topic = "homeassistant/sensor/" + this->deviceId() + "/" + String(key) + "/config";
    // Retained, like every other discovery message here: Home Assistant must
    // still find these entities after a restart that the ESP did not witness.
    this->mqtt->publish(topic.c_str(), cfg.c_str(), true);
}

void Sensors::sendHomeAssistantDiscoveryMessages()
{
    Serial.println("[Sensors] Sending discovery messages");

    // °C rather than a literal degree sign: the source file would
    // otherwise have to be UTF-8 all the way through the toolchain to MQTT, and
    // JSON escapes make that irrelevant.
    if (this->bmePresent)
    {
        this->sendOneDiscoveryMessage("temperature", "Temperature", "temperature", "\\u00b0C", nullptr, false);
        this->sendOneDiscoveryMessage("humidity", "Humidity", "humidity", "%", nullptr, false);
        this->sendOneDiscoveryMessage("pressure", "Pressure", "atmospheric_pressure", "hPa", nullptr, false);
    }
    if (this->scdPresent)
    {
        this->sendOneDiscoveryMessage("co2", "CO2", "carbon_dioxide", "ppm", "mdi:molecule-co2", false);
        this->sendOneDiscoveryMessage("scd_temperature", "SCD41 Temperature", "temperature", "\\u00b0C", nullptr, true);
        this->sendOneDiscoveryMessage("scd_humidity", "SCD41 Humidity", "humidity", "%", nullptr, true);
    }
    {
        this->sendOneDiscoveryMessage("rssi", "WiFi signal", "signal_strength", "dBm", nullptr, true);
        this->sendOneDiscoveryMessage("uptime_s", "Uptime", "duration", "s", nullptr, true);
        this->sendOneDiscoveryMessage("mqtt_connects", "MQTT connections", nullptr, nullptr, "mdi:lan-connect", true);
        this->sendOneDiscoveryMessage("reset_reason", "Reset reason", nullptr, nullptr, "mdi:restart-alert", true, false);
        if (this->bmePresent)
            this->sendOneDiscoveryMessage("t_delta", "SCD41 minus BME280", "temperature", "\\u00b0C", nullptr, true);
    }
}
