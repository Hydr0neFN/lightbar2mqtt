[English](setup.md) · **繁體中文**

# 安裝設定

接線、Arduino IDE 流程與配對。需求清單請看[讀我檔](../README.zh-TW.md#建置與執行)。

## 1. 硬體

依照下表將 nRF24 模組接到 ESP32。至少在我這組 ESP32 與 nRF24 模組的搭配下，
以下接腳可以正常運作。如有需要，可以在 `config.h` 檔案中變更 CE 與 CSN 接腳。
SPI 接腳（SCK、MOSI、MISO）在不同的 ESP32 板子上可能不同——請先確認你手上
ESP32 的接腳圖！

| nRF24 |  ESP32 |
| :---- | -----: |
| VCC   |    3V3 |
| GND   |    GND |
| CE    |  Pin 4 |
| CSN   |  Pin 5 |
| SCK   | Pin 18 |
| MOSI  | Pin 23 |
| MISO  | Pin 19 |

本分支用的 ESP32-C3 接腳配置不同，見[ESP32-C3 移植](esp32-c3-port.zh-TW.md)。

## 2. 軟體

上游原本的 Arduino IDE 工作流程（如下）仍可使用，但無論用哪一種開發板或 IDE
都有一項共通變動：第 4 步現在需要多安裝兩個函式庫，因為 `Lightbar.ino` 無條件
include 了本分支的 `sensors.h`。本分支本身是以 PlatformIO 開發的——若想改用
PlatformIO，見 [PlatformIO 建置](platformio-build.zh-TW.md)。

1. 複製（clone）這個儲存庫
2. 將 `config-example.h` 複製為 `config.h`（若是 ESP32-C3，則複製
   `config-example-c3.h`——見[ESP32-C3 移植](esp32-c3-port.zh-TW.md)），並依需求調整其中的設定。
3. 將 ESP32 連接到電腦。
4. 開啟 Arduino IDE 並安裝所需的函式庫：
   - [Arduino_JSON](https://github.com/arduino-libraries/Arduino_JSON)，作者 Arduino，_版本 0.2.0_
   - [CRC](https://github.com/RobTillaart/CRC)，作者 Rob Tillaart，_版本 1.0.3_
   - [PubSubClient](https://pubsubclient.knolleary.net/)，作者 Nick O'Leary，_版本 2.8_
   - [RF24](https://nrf24.github.io/RF24/)，作者 TMRh20，_版本 1.4.10_
   - *（分支新增）* [Adafruit BME280 Library](https://github.com/adafruit/Adafruit_BME280_Library) 與 [Sensirion I2C SCD4x](https://github.com/Sensirion/arduino-i2c-scd4x)——`Lightbar.ino` 無條件 include 了 `sensors.h`，所以這兩個函式庫現在是建置時的必要項目，並非選用。真正選用的是實體感測器本身：I2C 匯流排上不一定要真的接上這兩顆晶片（見[環境感測器](environment-sensors.zh-TW.md)），但函式庫仍必須安裝才能編譯成功。
5. 選擇你的序列埠與開發板，將程式碼上傳到 ESP32。
6. 檢視序列埠監控視窗（115200 baud）。若設定正確，ESP32 應會連上你的 WiFi 與 MQTT 伺服器。
7. 此時你大概還不知道遙控器的序號。只要按壓／轉動遙控器，就能在序列埠監控視窗中看到序號（例如：`[Radio] Ignoring package with not matching serial: 0x7B7E12`）。將它複製並貼到 `config.h` 檔案中的「Remotes」區段。
8. 再次上傳程式碼。
9. 若你的 Home Assistant 已設定好 MQTT 整合，燈條應會自動被探索到。
10. 開始用 MQTT 控制你的燈條吧！

## 3. 配對燈條與 ESP32（選用）

如果你選擇在 `config.h` 檔案中為序號與遙控器設定不同的值，就需要將燈條與 ESP32
配對。方法是：將燈條斷電重開，並在 10 秒內按下 Home Assistant 中的「Pair」按鈕，
或是傳送一則訊息到配對主題（見 [MQTT 主題](mqtt-topics.zh-TW.md#配對)）。若配對
成功，燈條會閃爍幾下。

燈條序號若和某個遙控器相同，原廠遙控器就還能直接控制燈條；設成不同的值，兩邊
就解耦，控制權完全交給 ESP32。
