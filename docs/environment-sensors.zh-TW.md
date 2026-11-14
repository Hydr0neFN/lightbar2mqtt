[English](environment-sensors.md) · **繁體中文**

# 環境感測器 *（分支新增）*

一個選用性的 BME280（溫度／濕度／氣壓）與 Sensirion SCD41（CO2／溫度／濕度），
共用一條 I2C 匯流排，程式碼位於 `sensors.cpp`／`sensors.h`。並非上游原有功能。
兩顆感測器互相獨立、皆為選用——模組會在開機時分別偵測，只針對實際偵測到的
感測器發布對應實體。

- 以**獨立的 Home Assistant 裝置**（「Room Environment」，ID 為
  `<client id>_env`）發布，與燈條自己的裝置區隔開來。
- 設計上為非阻塞式：`Radio::loop()` 每個週期都會輪詢 nRF24 以取得遙控器封包，
  一旦在這裡漏接封包，就等於漏掉一次遙控器操作。感測器模組的 `loop()` 以計時器
  控制節奏（`SENSOR_UPDATE_INTERVAL_MS`，預設 60 秒），開機時唯一會阻塞的呼叫
  是 SCD41 的 `stopPeriodicMeasurement()`（約 500 毫秒）；其餘部分都會立即返回。
- BME280 的氣壓讀數會提供給 SCD41 使用（`setAmbientPressure()`），因為 NDIR
  CO2 量測本身會受氣壓影響——這正是這兩顆感測器要共用同一條匯流排、不各自獨立
  存在的原因。
- BME280 會同時嘗試 `0x76` 與 `0x77` 這兩個位址，因為不同廠牌的擴充板依 `SDO`
  接腳的接法不同，位址也會不同。
- `SENSOR_SCD4X_TEMP_OFFSET` 用來補償 SCD41 緊鄰 WiFi SoC 產生的自我發熱效應（預設
  4°C，即 Sensirion 原廠預設值）；實際數值取決於外殼結構，需要就地調校。感測器
  自身的溫度／濕度，以及與 BME280 讀數之間的差值（`t_delta`），都會以診斷用實體
  發布，目的就是讓調校時有資料可看。
- 每一次 `env/state` 更新，也都會一併帶上 `rssi`、`uptime_s`、
  `mqtt_connects` 與 `reset_reason`（`POWERON` / `BROWNOUT` / `PANIC` / …，
  取自 `esp_reset_reason()`）等診斷用實體——這些連線品質相關的遙測資料搭著同一
  次發布週期一起送出；訊號不穩的 WiFi 環境下、無頭運作的裝置，很需要這些資料。
  （下方的 `env/config` 是另一則僅在開機時發布一次的訊息，不會帶有這些欄位。）
- Payload 範例（`.../env/state`）：
  ```json
  {"temperature":21.34,"humidity":47.02,"pressure":1013.25,"co2":612,"scd_temperature":24.88,"scd_humidity":41.10,"t_delta":3.54,"rssi":-58,"uptime_s":86412,"mqtt_connects":1,"reset_reason":"POWERON"}
  ```
- 設定巨集（皆為選用，於 `config.h` 中設定）：`SENSOR_PIN_SDA`、
  `SENSOR_PIN_SCL`、`SENSOR_I2C_CLOCK`（預設 100 kHz——這是 SCD4x 本身的速度
  上限；將匯流排設得更快不會乾脆地失敗，而是會出現間歇性的 NACK 與不合理的
  讀數）、`SENSOR_UPDATE_INTERVAL_MS`、`SENSOR_SCD4X_TEMP_OFFSET`。
