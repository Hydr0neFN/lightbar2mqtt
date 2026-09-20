[English](README.md) · **繁體中文**

# lightbar2mqtt（分支）

> 基於 [ebinf/lightbar2mqtt](https://github.com/ebinf/lightbar2mqtt)（MIT 授權，
> © 2024 Erik Borowski）。這是一份獨立儲存庫，而非 GitHub fork——上游的完整開發
> 歷史都保留在本儲存庫的根部（Erik Borowski 的 14 個 commit，一路回溯到最初那
> 一個，見 `git log`），`LICENSE` 同時列出兩行著作權聲明。下方[本分支新增內容](#本分支新增內容)為新增段落；本文件其餘部分為上游
> 對原始專案功能的原文文件，維持原樣，僅在上游段落中本分支有變動之處，以
> `*（分支）*` 標示。

透過 MQTT 控制你的 [Xiaomi Mi 電腦螢幕燈條](https://www.mi.com/global/product/mi-computer-monitor-light-bar/)，並將它加入 Home Assistant！你只需要一片 ESP32、一個 nRF24 模組，當然還有一條燈條。

## 本分支新增內容

以下所有內容都是為特定部署場景撰寫的：一片以市電供電、無頭運作的 ESP32-C3，
所在房間同時需要 CO2／溫度／濕度監測。以下程式碼變動皆為本分支自行撰寫——
包含最後一項，它修正的是一個上游原本就存在、卻只有在本分支才被修正的錯誤。
上游自己的致謝名單維持不變，見[致謝](#致謝)。

- **環境感測器**——I2C 匯流排上的 BME280 + Sensirion SCD41，以獨立的 Home
  Assistant 裝置形式發布，與燈條並列。見[環境感測器](#環境感測器分支新增)。
- **PlatformIO 建置**，作為本分支自身的開發方式。Arduino IDE 仍然可以使用
  （見[PlatformIO 建置](#platformio-建置分支新增)），但並非與上游完全一致：
  `Lightbar.ino` 現在會無條件 include `sensors.h`，因此下方兩個感測器函式庫
  無論用 Arduino IDE 還是 PlatformIO 都是建置時的必要項目。
- **ESP32-C3 移植**——上游針對一般 ESP32；本分支新增對應 ESP32-C3 原生 USB
  與 GPIO 限制的接腳配置與設定範例。見
  [ESP32-C3 移植](#esp32-c3-移植分支新增)。
- **OTA 韌體更新**，需密碼授權，預設關閉。見
  [OTA 更新](#ota-更新分支新增)。
- **WiFi／MQTT 穩定性修正**，因為這片板子連續數週待在同一個 AP 上運作，而非
  只跑過一次開發週期——見
  [穩定性修正](#wifi--mqtt-穩定性修正分支新增)。
- **一個貨真價實的錯誤修正**，位於 `remote.cpp` 的監聽器移除邏輯，這個問題
  在上游同樣存在——見[Remote 監聽器修正](#remote-監聽器修正分支新增)。

## 致謝

本專案大量參考了 [lamperez](https://github.com/lamperez) 出色的逆向工程成果。可以參考他們的[儲存庫](https://github.com/lamperez/xiaomi-lightbar-nrf24)，裡面有更多關於通訊協定、逆向工程過程，以及一個在樹莓派上執行的 Python 版本的資訊。該儲存庫採用 GNU General Public License v3.0 授權。

我也從 [Ben Allen](https://github.com/benallen-dev) 的[儲存庫](https://github.com/benallen-dev/xiaomi-lightbar)獲得一些啟發，該儲存庫採用 MIT 授權。

沒有這些出色的人的付出，這個專案不可能誕生！謝謝你們！

## 功能特色

- 透過簡單的 MQTT 訊息控制電源狀態、亮度與色溫
- 同樣透過 MQTT 接收遙控器的狀態更新
- 可以額外使用原廠遙控器，也可以將燈條與遙控器解耦以取得完全控制權
- 整合 Home Assistant——可手動設定，或透過自動探索功能零設定完成
  - 燈條會以 `light` 實體呈現
  - 遙控器會以 `sensor` 實體呈現
  - 遙控器上的操作動作也會觸發 `device_automation`，讓你能依據遙控器上的操作在 Home Assistant 中觸發自動化
- 輕鬆使用多個燈條或遙控器！每一個都能個別控制／監控，並在 Home Assistant 中擁有各自獨立的實體。
- *（分支新增）* 選用性的 BME280／SCD41 環境感測器，以獨立的 Home Assistant 裝置發布。
- *（分支新增）* 選用性、需密碼授權的 OTA 韌體更新。

## 需求

- 一條小米電腦螢幕燈條，型號 MJGJD**01**YL（不含藍牙／WiFi 版本）。MJGJD**02**YL 無法使用！
- 一片 ESP32（或 ESP32-C3——見[ESP32-C3 移植](#esp32-c3-移植分支新增)）
- 一個 nRF24 模組（已測試 nRF24L01）
- *（分支新增、選用）* 同一 I2C 匯流排上的 BME280 與／或 Sensirion SCD4x，用於[環境感測器](#環境感測器分支新增)

## 安裝

### 1. 硬體

依照下表將 nRF24 模組接到 ESP32。至少在我這組 ESP32 與 nRF24 模組的搭配下，以下接腳可以正常運作。如有需要，可以在 `config.h` 檔案中變更 CE 與 CSN 接腳。SPI 接腳（SCK、MOSI、MISO）在不同的 ESP32 板子上可能不同——請先確認你手上 ESP32 的接腳圖！

| nRF24 |  ESP32 |
| :---- | -----: |
| VCC   |    3V3 |
| GND   |    GND |
| CE    |  Pin 4 |
| CSN   |  Pin 5 |
| SCK   | Pin 18 |
| MOSI  | Pin 23 |
| MISO  | Pin 19 |

本分支所使用的 ESP32-C3 接腳配置不同，見
[ESP32-C3 移植](#esp32-c3-移植分支新增)。

### 2. 軟體

上游原本的 Arduino IDE 工作流程（如下）仍可使用，但無論用哪一種開發板或
IDE 都有一項共通變動：第 4 步現在需要多安裝兩個函式庫，因為 `Lightbar.ino`
無條件 include 了本分支的 `sensors.h`。本分支本身是以 PlatformIO 開發的
——若想改用 PlatformIO，見
[PlatformIO 建置](#platformio-建置分支新增)。

1. 複製（clone）這個儲存庫
2. 將 `config-example.h` 複製為 `config.h`（若是 ESP32-C3，則複製
   `config-example-c3.h`——見[ESP32-C3 移植](#esp32-c3-移植分支新增)），並依需求調整其中的設定。
3. 將 ESP32 連接到電腦。
4. 開啟 Arduino IDE 並安裝所需的函式庫：
   - [Arduino_JSON](https://github.com/arduino-libraries/Arduino_JSON)，作者 Arduino，_版本 0.2.0_
   - [CRC](https://github.com/RobTillaart/CRC)，作者 Rob Tillaart，_版本 1.0.3_
   - [PubSubClient](https://pubsubclient.knolleary.net/)，作者 Nick O'Leary，_版本 2.8_
   - [RF24](https://nrf24.github.io/RF24/)，作者 TMRh20，_版本 1.4.10_
   - *（分支新增）* [Adafruit BME280 Library](https://github.com/adafruit/Adafruit_BME280_Library) 與 [Sensirion I2C SCD4x](https://github.com/Sensirion/arduino-i2c-scd4x)——`Lightbar.ino` 無條件 include 了 `sensors.h`，所以這兩個函式庫現在是建置時的必要項目，並非選用。真正選用的是實體感測器本身：I2C 匯流排上不一定要真的接上這兩顆晶片（見[環境感測器](#環境感測器分支新增)），但函式庫仍必須安裝才能編譯成功。
5. 選擇你的序列埠與開發板，將程式碼上傳到 ESP32。
6. 檢視序列埠監控視窗（115200 baud）。若設定正確，ESP32 應會連上你的 WiFi 與 MQTT 伺服器。
7. 此時你大概還不知道遙控器的序號。只要按壓／轉動遙控器，就能在序列埠監控視窗中看到序號（例如：`[Radio] Ignoring package with not matching serial: 0x7B7E12`）。將它複製並貼到 `config.h` 檔案中的「Remotes」區段。
8. 再次上傳程式碼。
9. 若你的 Home Assistant 已設定好 MQTT 整合，燈條應會自動被探索到。
10. 開始享受透過 MQTT 控制你的燈條吧！

### 3. 配對燈條與 ESP32（選用）

如果你選擇在 `config.h` 檔案中為序號與遙控器設定不同的值，就需要將燈條與 ESP32 配對。方法是：將燈條斷電重開，並在 10 秒內按下 Home Assistant 中的「Pair」按鈕，或是傳送一則訊息到配對主題（見下方）。若配對成功，燈條會閃爍幾下。

## 使用方式

### MQTT 主題

所有 MQTT 主題都會加上一個根主題前綴。你可以在 `config.h` 檔案中設定這個根主題，預設值為 `lightbar2mqtt`。根主題後面會接上你 ESP32 的 MAC 位址，格式為 `l2m_<你 ESP32 的 MAC>`，例如 `l2m_1234567890AB`。只要查看 ESP32 的序列埠監控視窗，就能找到實際使用的根主題與 MAC 位址，它會在開機訊息之後印出來：

```text
...
[MQTT] Device ID: l2m_1234567890AB
[MQTT] Root Topic: lightbar2mqtt/l2m_12345679890AB
...
```

為了能夠控制多個燈條或遙控器，每一個都有各自的子主題，以所選定的序號（全部小寫）開頭。

#### 燈條

若要控制燈條，可以傳送訊息到以下主題：`<MQTT_ROOT_TOPIC>/l2m_<你 ESP32 的 MAC>/0x<燈條的序號>/command`，例如 `lightbar2mqtt/l2m_1234567890AB/0xabcdef/command`。payload 應為一個 JSON 物件，包含以下欄位：

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

#### 遙控器

遙控器會將自己的狀態傳送到以下主題：`<MQTT_ROOT_TOPIC>/l2m_<你 ESP32 的 MAC>/0x<遙控器的序號>/state`，例如 `lightbar2mqtt/l2m_1234567890AB/0x123456/state`。payload 為純文字字串，可能的值如下：

- `press`
- `turn_clockwise`
- `turn_counterclockwise`
- `hold`
- `press_turn_clockwise`
- `press_turn_counterclockwise`

#### 配對

若要將燈條與 ESP32 配對，請傳送訊息到以下主題：`<MQTT_ROOT_TOPIC>/l2m_<你 ESP32 的 MAC>/0x<燈條的序號>/pair`，例如 `lightbar2mqtt/l2m_1234567890AB/0xabcdef/pair`。payload 內容不拘，會被忽略。

請注意，在傳送配對訊息之前，燈條必須先在 10 秒內斷電重開。

#### 上線狀態

ESP32 會將自己的上線狀態傳送到以下主題：`<MQTT_ROOT_TOPIC>/l2m_<你 ESP32 的 MAC>/availability`，例如 `lightbar2mqtt/l2m_1234567890AB/availability`。payload 為 `online` 或 `offline` 其中之一。

#### 環境感測器 *（分支新增）*

若啟用此功能，感測器模組會在同一根主題下再發布到兩個主題——payload 格式見
[環境感測器](#環境感測器分支新增)：

- `<MQTT_ROOT_TOPIC>/l2m_<MAC>/env/state`——感測器讀數，每隔 `SENSOR_UPDATE_INTERVAL_MS` 發布一次
- `<MQTT_ROOT_TOPIC>/l2m_<MAC>/env/config`——開機時讀回的 SCD41 設定值，保留（retained）發布

### Home Assistant

若你的 Home Assistant 已設定好 MQTT 整合，燈條與遙控器應會自動被探索到。

此外，上面提到的遙控器事件也能作為 `device_automation` 觸發器使用。你可以利用這些觸發器，依據遙控器上的操作在 Home Assistant 中建立自動化。作法是：在 Home Assistant 中建立新的自動化，並選擇「裝置」作為觸發類型，選取對應的遙控器實體與所需的觸發條件，例如 `"press" action`。

### 已知問題／限制

- 燈條不會將自己的狀態回傳給 ESP32。這表示如果你透過原廠控制器變更燈條狀態，ESP32 並不會知道。這是燈條所使用通訊協定本身的限制。
- 沒有辦法得知燈條目前是開啟還是關閉。因此本專案假設 ESP32 開機時燈條處於開啟狀態。如果你（例如透過遙控器）將燈條關閉，這可能導致 Home Assistant 中的狀態與實際相反。此時只要在 Home Assistant 中將裝置開啟，並將燈條斷電重開即可修正。
- 遙控器上的操作有時不會被 ESP32 辨識到。在 Home Assistant 中建立自動化時，請不要假設遙控器事件百分之百準確——通常第二次或第三次操作就會成功。因此也建議將燈條與原廠遙控器解耦，否則遙控器上的某些操作可能會改變燈條狀態，卻不會在 Home Assistant 中觸發任何事件。

## 環境感測器 *（分支新增）*

一個選用性的 BME280（溫度／濕度／氣壓）與 Sensirion SCD41（CO2／溫度／濕度），共用一條 I2C 匯流排，程式碼位於 `sensors.cpp`／`sensors.h`。並非上游原有功能。兩顆感測器互相獨立、皆為選用——模組會在開機時分別偵測，只針對實際偵測到的感測器發布對應實體。

- 以**獨立的 Home Assistant 裝置**（「Room Environment」，ID 為
  `<client id>_env`）發布，與燈條自己的裝置區隔開來。
- 設計上為非阻塞式：`Radio::loop()` 每個週期都會輪詢 nRF24 以取得遙控器封包，
  一旦在這裡漏接封包，就等於漏掉一次遙控器操作。感測器模組的 `loop()` 以計時器
  控制節奏（`SENSOR_UPDATE_INTERVAL_MS`，預設 60 秒），開機時唯一會阻塞的呼叫
  是 SCD41 的 `stopPeriodicMeasurement()`（約 500 毫秒）；其餘部分都會立即返回。
- BME280 的氣壓讀數會提供給 SCD41 使用（`setAmbientPressure()`），因為 NDIR
  CO2 量測本身會受氣壓影響——這正是這兩顆感測器共用同一條匯流排，而非各自獨立
  存在的原因。
- BME280 會同時嘗試 `0x76` 與 `0x77` 這兩個位址，因為不同廠牌的擴充板依 `SDO`
  接腳的接法不同，位址也會不同。
- `SENSOR_SCD4X_TEMP_OFFSET` 用來補償 SCD41 緊鄰 WiFi SoC 所產生的自我發熱效應（預設
  4°C，即 Sensirion 原廠預設值）；實際數值取決於外殼結構，需要就地調校。感測器
  自身的溫度／濕度，以及與 BME280 讀數之間的差值（`t_delta`），都會以診斷用實體
  發布，目的就是讓調校時有資料可看。
- 每一次 `env/state` 更新，也都會一併帶上 `rssi`、`uptime_s`、
  `mqtt_connects` 與 `reset_reason`（`POWERON` / `BROWNOUT` / `PANIC` / …，
  取自 `esp_reset_reason()`）等診斷用實體——這些連線品質相關的遙測資料搭著同一
  次發布週期一起送出，對於處在訊號不穩 WiFi 環境下、無頭運作的裝置相當實用。
  （下方的 `env/config` 是另一則僅在開機時發布一次的訊息，不會帶有這些欄位。）
- Payload 範例（`.../env/state`）：
  ```json
  {"temperature":21.34,"humidity":47.02,"pressure":1013.25,"co2":612,"scd_temperature":24.88,"scd_humidity":41.10,"t_delta":3.54,"rssi":-58,"uptime_s":86412,"mqtt_connects":1,"reset_reason":"POWERON"}
  ```
- 設定巨集（皆為選用，於 `config.h` 中設定）：`SENSOR_PIN_SDA`、
  `SENSOR_PIN_SCL`、`SENSOR_I2C_CLOCK`（預設 100 kHz——這是 SCD4x 本身的速度
  上限；將匯流排設得更快不會乾脆地失敗，而是會出現間歇性的 NACK 與不合理的
  讀數）、`SENSOR_UPDATE_INTERVAL_MS`、`SENSOR_SCD4X_TEMP_OFFSET`。

## PlatformIO 建置 *（分支新增）*

`platformio.ini` 為本分支新增——上游原本只提供一個沒有任何建置設定、也沒有
發行版二進位檔的 Arduino IDE 專案。`src_dir = .` 讓原始碼維持在儲存庫根目錄，
以確保與上游的差異（diff）仍然易讀。

- 建置環境：`esp32-c3-pro-mini`（`espressif32` / `esp32-c3-devkitm-1`，
  4 MB flash）。
- **需要 C++17**（`build_unflags = -std=gnu++11`，`-std=gnu++17`）：`radio.h`
  在類別內宣告了 `static constexpr byte preamble[8]`，卻沒有類別外的定義。
  這之所以能連結成功，是因為 C++17 讓 static constexpr 成員隱含 inline；
  PlatformIO 的 `espressif32` 平台預設卻是 `gnu++11`，在該標準下這屬於一次
  沒有定義的 ODR-use，連結時會出現 `undefined reference to
  Radio::preamble`。上游之所以從未遇到這個問題，是因為 Arduino IDE 使用的
  arduino-esp32 3.x 是以 `gnu++2a` 編譯的。
- `-DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=1`：C3 沒有 USB-UART 橋接
  晶片，`Serial` 是走內建的 USB Serial/JTAG。若沒有這兩個定義，
  `Serial.print()` 會改走 GPIO20/21，序列埠監控視窗就會沒有任何輸出——這會
  導致上方安裝說明第 7 步（從監控視窗讀取遙控器序號）無法進行。
- `lib_deps` 所釘選的函式庫版本，與上游 README 所指定的版本相同，並加上本
  分支新增的兩個感測器函式庫。
- `[env:wifitest]`——與正式版相同的韌體，但加上 `-DWIFI_TEST_NO_RADIO`，會
  跳過 `radio.setup()`。這個呼叫在找不到任何 nRF24 回應時會阻塞長達 60 秒
  並重開機，若不跳過它，在 nRF24 模組實際接線完成之前就無法測試 WiFi／
  MQTT。切勿在正式使用時燒錄此環境——它無法與燈條通訊。
- `[env:esp32-c3-pro-mini-ota]`——見[OTA 更新](#ota-更新分支新增)。

## ESP32-C3 移植 *（分支新增）*

`config-example-c3.h`——針對 ESP32-C3 開發板（已在絲印為
`ESP32-C3_MINI_V1` 的 ESP32-C3 Pro Mini 上測試）的設定範例，與上游原有、
針對一般 ESP32 的 `config-example.h` 並存。兩者的主要差異在於接腳選擇，
這是因為 C3 可用的 GPIO 遠比一般 ESP32 少：

| 訊號        | 接腳（C3） | 原因 |
| :---------- | -------: | :-- |
| `RADIO_PIN_SCK`  | GPIO 4  | 原生 FSPI 接腳 |
| `RADIO_PIN_MISO` | GPIO 5  | 原生 FSPI 接腳 |
| `RADIO_PIN_MOSI` | GPIO 6  | 原生 FSPI 接腳 |
| `RADIO_PIN_CSN`  | GPIO 7  | 原生 FSPI 接腳 |
| `RADIO_PIN_CE`   | GPIO 10 | 最後一個完全沒有其他用途的接腳 |
| *（感測器）* `SENSOR_PIN_SDA` | GPIO 0 | 不用 GPIO8——原因見下方 |
| *（感測器）* `SENSOR_PIN_SCL` | GPIO 1 | 不用 GPIO9——原因見下方 |

刻意避開的接腳：GPIO8／GPIO9（strapping pin——GPIO8 同時驅動了板上 LED，
GPIO9 則是 BOOT 按鈕；若感測器在開機時的匯流排卡住期間把 SDA／SCL 拉低，
會導致整片板子完全無法開機，症狀看起來會像板子壞掉，而不是感測器出問題）
以及 GPIO20／GPIO21（UART，安裝說明第 7 步讀取序列埠監控視窗時需要用到）。

設定範例中同時記錄了一種特定的解耦設定：`LIGHTBARS[]` 中燈條的序號刻意
*不*設為實體遙控器的序號，如此一來，配對完成後就只有 ESP 能改變燈條的狀態
——這正是上游自己所建議、能讓 Home Assistant 狀態值得信賴的作法（見上方
[已知問題／限制](#已知問題限制)）。

## OTA 更新 *（分支新增）*

在 `config.h` 中定義 `OTA_PASSWORD` 即可啟用
[ArduinoOTA](https://docs.espressif.com/projects/arduino-esp32/en/latest/tutorials/ota_web_update.html)；
不定義的話，OTA 相關程式碼完全不會被編譯進去——這是選用（opt-in）功能，
而非預設行為。`OTA_PORT` 預設為 `3232`。

**這是安全性提醒，不是形式上的免責聲明：**這份韌體會把 WiFi 密碼（PSK）與
MQTT 密碼直接編譯進映像檔裡。這也是為什麼 `dist/`（建置出來的 `firmware.bin`
所在位置）會被列入 `.gitignore`（見 `.gitignore`）：只要 `config.h` 中含有
真實金鑰，從該工作目錄建置出的任何產出物，都會把這兩組金鑰以可被還原提取的
形式，帶入任何分享出去的檔案裡。未經身分驗證的 OTA 連接埠，同樣會把這兩組
金鑰交給區域網路上的任何人——這就是為什麼一旦啟用 OTA，`OTA_PASSWORD` 就是
必要項目，而非選用項目。

透過 `esp32-c3-pro-mini-ota` 這個 PlatformIO 環境燒錄：

```sh
pio run -e esp32-c3-pro-mini-ota -t upload
```

`platformio.ini` 中的 `upload_port` 刻意留為預留位置（`<device-ip>`）——
請自行換成你裝置實際的 IP 位址，因為裝置的區域網路位址不應該寫死在一份會
分享給其他人使用的檔案裡。OTA 密碼本身則是從環境變數
`LIGHTBAR_OTA_PASSWORD` 讀取（`platformio.ini` 受 git 版本控制，`config.h`
則否），只需設定一次：

```sh
setx LIGHTBAR_OTA_PASSWORD "<與 config.h 中 OTA_PASSWORD 相同的值>"
```

`espota` 會讓裝置反過來對主機開啟一條 TCP 連線，使用固定連接埠（`3233`），
而 Windows 防火牆預設會擋下這條連線——一次性所需的 `New-NetFirewallRule`
指令，請見 `platformio.ini` 中的註解。

## WiFi／MQTT 穩定性修正 *（分支新增）*

這些問題是在讓其中一片板子以市電供電、在訊號不穩的 WiFi 環境下連續運作數週
（而非只跑過一次開發週期）後才發現並修正的：

- **`WiFi.setSleep(false)`**——modem sleep（ESP32 預設行為）會在 DTIM
  beacon 之間讓無線電暫停運作，這正是為什麼 ping 延遲會在 4 毫秒到 157
  毫秒之間大幅擺動，也是為什麼 MQTT 心跳（keepalive）與 TCP ACK 經常遺失到
  足以每天造成數百次重新連線的地步。這片板子是以市電供電，睡眠模式所省下的
  約 20 mA 完全划不來這樣的代價。搭配 `WiFi.setAutoReconnect(true)` 一起使用。
- **MQTT 心跳時間放寬至 60 秒**，socket timeout 設為 10 秒
  （`mqtt.cpp`）——PubSubClient 預設的 15 秒心跳，大多數 broker 會以 1.5 倍
  時間強制斷線，這代表任何超過約 22 秒的 WiFi 中斷，都會被視為連線已死而
  觸發重新連線。這裡並不需要如此精細（次分鐘等級）的存活偵測。
- **在 `MQTT` 上新增一個 `publish()` 包裝函式**（`mqtt.cpp`／
  `mqtt.h`），改用 PubSubClient 的串流式 `beginPublish`／`print`／
  `endPublish` API，而非其有緩衝區限制的 `publish()`。該緩衝區預設只有
  256 位元組，而且本專案中從未在任何地方將它放大；Home Assistant 探索用
  的 payload 長度落在 650 至 900 位元組之間，因此原本有緩衝區限制的呼叫
  只會直接回傳 `false`——而且完全沒有任何地方記錄失敗原因。這同時也是
  感測器模組得以發布訊息的前提，因為原本 `MQTT` 底層的 `PubSubClient`
  是私有（private）成員，外部無法存取。
- **連線次數計數器**（`MQTT::getConnectCount()`）——記錄 `setup()`
  成功完成與 broker 連線的次數。第一次是開機，之後每一次都是重新連線，
  而這正是在懷疑 WiFi 連線品質時真正有意義的指標。這項資料會出現在感測器
  模組的診斷欄位中（`mqtt_connects`），原因正是它揭露了這個部署場景中
  每天約 300 次的重新連線，而在此之前完全無從得知原因。

## Remote 監聽器修正 *（分支新增）*

`remote.cpp` 中的 `Remote::unregisterCommandListener()` 在 PlatformIO 的
建置環境下原本無法編譯：上游的比較邏輯使用了 `std::function::target<T>()`，
而這需要 RTTI，但 arduino-esp32 的建置設定是 `-fno-rtti`。

深入追查編譯失敗原因後，發現的其實是一個原本就存在的錯誤，而不只是可攜性
（portability）問題：即便啟用 RTTI，也只會讓這一行「能編譯」，並不會讓它
「正確運作」。上游那一行的樣板參數（template argument）是一個函式型別，但
實際儲存的卻是一個 `std::bind` 物件——因此無論是否啟用 RTTI，
`target<T>()` 在兩邊都會回傳 `nullptr`，比較結果永遠是
`nullptr == nullptr`，在第一次迴圈就必定成立。也就是說，這個比較邏輯
其實從來沒有真正比較過任何東西；實際、真正上線執行的行為，一直都是
「移除第一個註冊的監聽器」。

本分支的修正方式（見 `remote.cpp:59` 的註解）是把這個既有的實際行為明確
寫出來，而不是嘗試恢復一個 `std::function` 本來就沒有對應運算子可用的
比較邏輯。這個作法對唯一的呼叫端（`MQTT::removeRemote()`）而言是正確的，
因為它針對每個遙控器都只會註冊剛好一個監聽器。如果日後同一個遙控器需要
註冊第二個監聽器，就需要一套真正的識別機制——例如讓
`registerCommandListener()` 回傳一個 token——而不是再次嘗試比較這些
callable 物件本身。

## 貢獻

如果你發現錯誤或有新功能的想法，歡迎開 issue 或建立 pull request。我很樂見這個專案持續成長與改進！

我把程式碼設計成可以輕鬆™擴充。~~舉例來說，要新增支援多個燈條或多個遙控器應該相對容易。~~

## 授權

本專案採用 MIT 授權。詳情請見 [LICENSE](LICENSE) 檔案——其中同時列出原始的
2024 年著作權聲明，以及本分支針對上述新增內容的 2026 年著作權聲明。
