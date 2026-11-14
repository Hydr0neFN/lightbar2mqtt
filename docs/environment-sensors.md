**English** · [繁體中文](environment-sensors.zh-TW.md)

# Environment sensors *(fork addition)*

An optional BME280 (temperature/humidity/pressure) and Sensirion SCD41
(CO2/temperature/humidity) on one I2C bus, in `sensors.cpp`/`sensors.h`. Not
part of upstream. Both sensors are independently optional — the module probes
for each at boot and only publishes entities for the ones it finds.

- Published as a **separate Home Assistant device** ("Room Environment", ID
  `<client id>_env`), distinct from the light bar's own device.
- Non-blocking by design: `Radio::loop()` polls the nRF24 for remote packets
  every cycle, and a packet missed there is a lost remote press. The sensor
  module's `loop()` is timer-gated (`SENSOR_UPDATE_INTERVAL_MS`, default 60 s)
  and only the SCD41's `stopPeriodicMeasurement()` at boot blocks (~500 ms);
  everything else returns immediately.
- The BME280 feeds its pressure reading to the SCD41 (`setAmbientPressure()`),
  since NDIR CO2 measurement is pressure-dependent — this is the reason the
  two sensors share a bus rather than being independent add-ons.
- The BME280 is probed at both `0x76` and `0x77`, since that differs between
  breakout board vendors depending on how the `SDO` pin is tied.
- `SENSOR_SCD4X_TEMP_OFFSET` compensates the SCD41 self-heating next to a WiFi SoC
  (default 4 °C, Sensirion's own default); the right value depends on the
  enclosure and is meant to be tuned in place. The sensor's own temperature/
  humidity and the delta against the BME280 (`t_delta`) are published as
  diagnostic entities specifically so that tuning has something to read.
- Every `env/state` update also carries `rssi`, `uptime_s`, `mqtt_connects`
  and `reset_reason` (`POWERON` / `BROWNOUT` / `PANIC` / …, from
  `esp_reset_reason()`) as diagnostic entities — link-quality telemetry
  riding on the same publish cycle, useful for a headless board on a
  marginal WiFi link. (`env/config`, below, is a separate, boot-only
  message and doesn't carry these.)
- Payload example (`.../env/state`):
  ```json
  {"temperature":21.34,"humidity":47.02,"pressure":1013.25,"co2":612,"scd_temperature":24.88,"scd_humidity":41.10,"t_delta":3.54,"rssi":-58,"uptime_s":86412,"mqtt_connects":1,"reset_reason":"POWERON"}
  ```
- Config macros (all optional, in `config.h`): `SENSOR_PIN_SDA`,
  `SENSOR_PIN_SCL`, `SENSOR_I2C_CLOCK` (100 kHz default — the SCD4x's own
  ceiling; running the bus faster does not fail cleanly, it shows up as
  intermittent NACKs and implausible readings), `SENSOR_UPDATE_INTERVAL_MS`,
  `SENSOR_SCD4X_TEMP_OFFSET`.
