[English](mqtt-topics.md) · **繁體中文**

# MQTT 主題與 Home Assistant

電源狀態、亮度與色溫都只要送一則 MQTT 訊息就能控制，遙控器的狀態也會透過 MQTT
送回。以下主題都掛在 `config.h` 設定的根主題（預設 `lightbar2mqtt`）之下，再加上
`l2m_<你 ESP32 的 MAC>`。

所有 MQTT 主題都會加上一個根主題前綴。你可以在 `config.h` 檔案中設定這個根主題，
預設值為 `lightbar2mqtt`。根主題後面會接上你 ESP32 的 MAC 位址，格式為
`l2m_<你 ESP32 的 MAC>`，例如 `l2m_1234567890AB`。只要查看 ESP32 的序列埠監控
視窗，就能找到實際使用的根主題與 MAC 位址，它會在開機訊息之後印出來：

```text
...
[MQTT] Device ID: l2m_1234567890AB
[MQTT] Root Topic: lightbar2mqtt/l2m_12345679890AB
...
```

為了能夠控制多個燈條或遙控器，每一個都有各自的子主題，以所選定的序號（全部
小寫）開頭。

## 燈條

若要控制燈條，可以傳送訊息到以下主題：
`<MQTT_ROOT_TOPIC>/l2m_<你 ESP32 的 MAC>/0x<燈條的序號>/command`，例如
`lightbar2mqtt/l2m_1234567890AB/0xabcdef/command`。payload 應為一個 JSON 物件，
包含以下欄位：

- `state`：`"ON"` 或 `"OFF"`
- `brightness`：`0`（關閉）到 `15`（全亮）
- `color_temp`：Mireds 色溫值——`153`（冷白）到 `370`（暖白）

範例：

```json
{
  "state": "ON",
  "brightness": 15,
  "color_temp": 153
}
```

## 遙控器

遙控器會將自己的狀態傳送到以下主題：
`<MQTT_ROOT_TOPIC>/l2m_<你 ESP32 的 MAC>/0x<遙控器的序號>/state`，例如
`lightbar2mqtt/l2m_1234567890AB/0x123456/state`。payload 為純文字字串，可能的
值如下：

- `press`
- `turn_clockwise`
- `turn_counterclockwise`
- `hold`
- `press_turn_clockwise`
- `press_turn_counterclockwise`

## 配對

若要將燈條與 ESP32 配對，請傳送訊息到以下主題：
`<MQTT_ROOT_TOPIC>/l2m_<你 ESP32 的 MAC>/0x<燈條的序號>/pair`，例如
`lightbar2mqtt/l2m_1234567890AB/0xabcdef/pair`。payload 內容不拘，會被忽略。

請注意，在傳送配對訊息之前，燈條必須先在 10 秒內斷電重開。

## 上線狀態

ESP32 會將自己的上線狀態傳送到以下主題：
`<MQTT_ROOT_TOPIC>/l2m_<你 ESP32 的 MAC>/availability`，例如
`lightbar2mqtt/l2m_1234567890AB/availability`。payload 為 `online` 或 `offline`
其中之一。

## 環境感測器 *（分支新增）*

若啟用此功能，感測器模組會在同一根主題下再發布到兩個主題——payload 格式見
[環境感測器](environment-sensors.zh-TW.md)：

- `<MQTT_ROOT_TOPIC>/l2m_<MAC>/env/state`——感測器讀數，每隔 `SENSOR_UPDATE_INTERVAL_MS` 發布一次
- `<MQTT_ROOT_TOPIC>/l2m_<MAC>/env/config`——開機時讀回的 SCD41 設定值，保留（retained）發布

## Home Assistant

若你的 Home Assistant 已設定好 MQTT 整合，Home Assistant 應會自動找到燈條與遙控器。

- 燈條會以 `light` 實體呈現
- 遙控器會以 `sensor` 實體呈現
- 多個燈條或遙控器都沒問題：每一個都能個別控制／監控，在 Home Assistant 中也
  各有獨立的實體。

此外，上面提到的遙控器事件也能作為 `device_automation` 觸發器使用。你可以利用
這些觸發器，依據遙控器上的操作在 Home Assistant 中建立自動化。作法是：在 Home
Assistant 中建立新的自動化，並選擇「裝置」作為觸發類型，選取對應的遙控器實體與
所需的觸發條件，例如 `"press" action`。
