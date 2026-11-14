[English](README.md) · **繁體中文**

# lightbar2mqtt（分支）

用 MQTT 控制 [Xiaomi Mi 電腦螢幕燈條](https://www.mi.com/global/product/mi-computer-monitor-light-bar/)，
接進 Home Assistant。你只需要一片 ESP32、一個 nRF24 模組，當然還有燈條本身。

本專案是 [ebinf/lightbar2mqtt](https://github.com/ebinf/lightbar2mqtt) 的分支
（MIT 授權，© 2024 Erik Borowski）。這裡是獨立複製的儲存庫，GitHub 上沒有對應的
fork：上游的完整開發歷史都留在本儲存庫的根部，由 Erik Borowski 與其他上游貢獻者
留下的 14 個 commit，一路回溯到最初那一筆（見 `git log`），`LICENSE` 也同時列出
兩行著作權聲明。本分支新增的文字會標上 *（分支）*，其餘段落寫的是上游原始專案的
內容，維持原樣。

## 重點速覽

| | |
| :---- | :---- |
| **問題** | 燈條只講自己的 2.4 GHz nRF24 協定。沒有 WiFi、沒有 MQTT，Home Assistant 看不到也控制不了。 |
| **我做了什麼** | 用一片 ESP32-C3 當橋樑：MQTT 指令進來、nRF24 封包出去；原廠遙控器的封包也接回來，改走 MQTT 發布。同一片板子還能選配 BME280 + SCD41 房間感測器。 |
| **結果** | 部署現場量到的數字：ping 延遲在 4 到 157 ms 之間來回，一天對 broker 重連約 300 次——下面的修正就是從這些數字來的。感測器預設每 60 秒發布一次。 |
| **技術堆疊** | ESP32 / ESP32-C3、Arduino C++、PlatformIO、nRF24L01、MQTT、Home Assistant、BME280 + Sensirion SCD41（I2C）。 |
| **我的角色** | 分支新增的部分都是本分支自己寫的：感測器、PlatformIO 建置、ESP32-C3 移植、OTA、穩定性修正，以及 `remote.cpp` 的錯誤修正。基礎專案來自上游，作者是 Erik Borowski 與其他貢獻者。 |

## 運作方式

```
   Home Assistant / any MQTT client
        |                          ^
        |  command (JSON)          |  state, availability
        v                          |
  +------------------------------------------------+
  |  ESP32 / ESP32-C3                              |
  |    mqtt.cpp      MQTT client, HA discovery     |
  |    radio.cpp     17-byte nRF24 packets         |
  |    sensors.cpp   BME280 + SCD41 on I2C (opt.)  |
  +------------------------------------------------+
        |                          ^
        v                          |
   Xiaomi light bar          original remote
```

板子連上 WiFi 與 MQTT broker。ESP32 從燈條的 `command` 主題收到一則 JSON 指令後，
會轉成原廠遙控器也在送的那種 nRF24 封包。同一顆無線電同時持續監聽遙控器，收到的封包再
以 MQTT 狀態與 `device_automation` 觸發器送出去。MQTT 連線建立時就送出 Home
Assistant 探索訊息，不必手寫 YAML。感測器模組沿用同一條 broker 連線，照自己的
計時器發布。

這片板子是為一個實際部署寫的：ESP32-C3、市電供電、無頭運作，房間同時要做
CO2／溫度／濕度監測。

## 本分支新增內容

- **環境感測器**——I2C 匯流排上的 BME280 + Sensirion SCD41，以獨立的 Home
  Assistant 裝置形式發布，與燈條並列。見[環境感測器](docs/environment-sensors.zh-TW.md)。
- **PlatformIO 建置**，作為本分支自身的開發方式。Arduino IDE 仍然可以使用，
  但並非與上游完全一致：`Lightbar.ino` 現在會無條件 include `sensors.h`，因此
  兩個感測器函式庫無論用 Arduino IDE 還是 PlatformIO 都是建置時的必要項目。見
  [PlatformIO 建置](docs/platformio-build.zh-TW.md)。
- **ESP32-C3 移植**——上游針對一般 ESP32；本分支新增對應 ESP32-C3 原生 USB
  與 GPIO 限制的接腳配置與設定範例。見[ESP32-C3 移植](docs/esp32-c3-port.zh-TW.md)。
- **OTA 韌體更新**，需密碼授權，預設關閉。見[OTA 更新](docs/ota-updates.zh-TW.md)。
- **WiFi／MQTT 穩定性修正**，因為這片板子連續數週待在同一個 AP 上運作，而非
  只跑過一次開發週期。見[修正紀錄](docs/fixes.zh-TW.md#wifi-與-mqtt-穩定性)。
- **一個貨真價實的錯誤修正**，位於 `remote.cpp` 的監聽器移除邏輯，這個問題
  在上游同樣存在。見[修正紀錄](docs/fixes.zh-TW.md#remote-監聽器修正)。

## 這些修正的由來

**WiFi 一直斷線。** ping 延遲在 4 ms 到 157 ms 之間大幅擺動，MQTT 心跳與 TCP ACK
也常掉，一天重連好幾百次。原因是 ESP32 預設的 modem sleep：它在 DTIM beacon 之間
把無線電停掉。板子吃市電，省下來的 ~20 mA 划不來。改用 `WiFi.setSleep(false)` 加
`WiFi.setAutoReconnect(true)` 解決。同一輪追查還查出兩個原因：PubSubClient 預設
15 秒心跳，多數 broker 以 1.5 倍時間計算，只要 WiFi 卡超過約 22 秒就會被當成斷線；
它的發布緩衝區只有 256 位元組，遇到 650–900 位元組的 Home Assistant 探索 payload
只會回傳 `false`，而且哪裡都不留紀錄。現在心跳 60 秒、socket timeout 10 秒，發布
改走 `beginPublish`/`print`/`endPublish` 串流，不經緩衝區。

**遙控器監聽器的移除，其實從來沒比對過任何東西。** 在 PlatformIO 下這一行根本
編不過：`std::function::target<T>()` 需要 RTTI，而 arduino-esp32 用 `-fno-rtti`
編譯。就算打開 RTTI，也只會讓它編過，不會讓它正確。那裡的樣板參數是函式型別，
實際存的卻是 `std::bind` 物件，所以 `target<T>()` 兩邊都回傳 `nullptr`，比對結果
永遠是 `nullptr == nullptr`，第一次迴圈就一定成立。實際上線的行為一直是「丟掉
第一個註冊的監聽器」。本分支把這個行為明確寫出來，而不是硬要還原一個
`std::function` 根本沒有對應運算子的比較。對唯一的呼叫端來說這樣是對的，因為它
每個遙控器只註冊一個監聽器。同一個遙控器要是註冊第二個監聽器，就需要一套真正的
識別機制。

## 建置與執行

需求：

- 一條小米電腦螢幕燈條，型號 MJGJD**01**YL（不含藍牙／WiFi 版本）。MJGJD**02**YL
  無法使用！
- 一片 ESP32（或 ESP32-C3——見[ESP32-C3 移植](docs/esp32-c3-port.zh-TW.md)）
- 一個 nRF24 模組（已測試 nRF24L01）
- *（分支新增、選用）* 同一 I2C 匯流排上的 BME280 與／或 Sensirion SCD4x，用於
  [環境感測器](docs/environment-sensors.zh-TW.md)

PlatformIO 的最短路徑：

```sh
cp config-example-c3.h config.h     # plain ESP32: cp config-example.h config.h
pio run -e esp32-c3-pro-mini -t upload
```

接著開序列埠監控視窗（115200 baud）。板子會印出 device ID 與根主題；按壓或轉動
遙控器就會印出遙控器序號，把它填進 `config.h` 再上傳一次。完整接線表、Arduino IDE
的十個步驟與函式庫版本都在[安裝設定](docs/setup.zh-TW.md)。

## 儲存庫結構

| 路徑 | 內容 |
| :---- | :---- |
| `Lightbar.ino` | 程式進入點：`setup()`、`loop()`、OTA 初始化。 |
| `lightbar.*`、`remote.*` | 燈條與遙控器兩種裝置，以及各自的指令。 |
| `radio.*` | nRF24 初始化、封包組裝與解析、CRC 檢查。 |
| `mqtt.*` | broker 連線、主題、Home Assistant 探索。 |
| `sensors.*` | BME280 + SCD41 模組 *（分支）*。 |
| `constants.h`、`config_defaults.h` | 版本、上限值、預設接腳。 |
| `config-example.h`、`config-example-c3.h` | 設定範本；複製其中一份為 `config.h`（已列入 gitignore）。 |
| `platformio.ini` | PlatformIO 建置環境 *（分支）*。 |
| `docs/` | 深入章節，中英文各一份。 |
| `LICENSE` | MIT，上游與本分支兩行著作權聲明。 |

## 深入說明

| 主題 | English | 繁體中文 |
| :---- | :---- | :---- |
| 接線、Arduino IDE 步驟、配對 | [setup.md](docs/setup.md) | [setup.zh-TW.md](docs/setup.zh-TW.md) |
| MQTT 主題、payload、Home Assistant | [mqtt-topics.md](docs/mqtt-topics.md) | [mqtt-topics.zh-TW.md](docs/mqtt-topics.zh-TW.md) |
| 環境感測器 *（分支）* | [environment-sensors.md](docs/environment-sensors.md) | [environment-sensors.zh-TW.md](docs/environment-sensors.zh-TW.md) |
| PlatformIO 建置 *（分支）* | [platformio-build.md](docs/platformio-build.md) | [platformio-build.zh-TW.md](docs/platformio-build.zh-TW.md) |
| ESP32-C3 移植 *（分支）* | [esp32-c3-port.md](docs/esp32-c3-port.md) | [esp32-c3-port.zh-TW.md](docs/esp32-c3-port.zh-TW.md) |
| OTA 更新 *（分支）* | [ota-updates.md](docs/ota-updates.md) | [ota-updates.zh-TW.md](docs/ota-updates.zh-TW.md) |
| 穩定性修正與監聽器錯誤 *（分支）* | [fixes.md](docs/fixes.md) | [fixes.zh-TW.md](docs/fixes.zh-TW.md) |

## 限制與未解項目

- 燈條不會將自己的狀態回傳給 ESP32。如果你透過原廠控制器變更燈條狀態，ESP32
  並不會知道。這是燈條所使用通訊協定本身的限制。
- 沒有辦法得知燈條目前是開啟還是關閉。因此本專案假設 ESP32 開機時燈條處於開啟
  狀態。如果你（例如透過遙控器）將燈條關閉，Home Assistant 中的狀態可能與實際
  相反。此時只要在 Home Assistant 中將裝置開啟，並將燈條斷電重開即可修正。
- 遙控器上的操作有時不會被 ESP32 辨識到。在 Home Assistant 中建立自動化時，
  不要假設遙控器事件 100% 準確——通常第二次或第三次操作就會成功。因此也建議
  將燈條與原廠遙控器解耦，否則遙控器上的某些操作可能會改變燈條狀態，卻不會在
  Home Assistant 中觸發任何事件。
- 上面的監聽器修正只對「每個遙控器一個監聽器」的情況正確。同一個遙控器要註冊
  第二個監聽器，就需要一套真正的識別機制——例如讓 `registerCommandListener()`
  回傳一個 token。
- `[env:wifitest]` 建置會跳過 `radio.setup()`，無法與燈條通訊，切勿在正式使用時
  燒錄。

## 致謝

本專案大量參考了 [lamperez](https://github.com/lamperez) 的逆向工程成果。可以
參考他們的[儲存庫](https://github.com/lamperez/xiaomi-lightbar-nrf24)，裡面有更多
關於通訊協定、逆向工程過程，以及一個在樹莓派上執行的 Python 版本的資訊。該
儲存庫採用 GNU General Public License v3.0 授權。

我也從 [Ben Allen](https://github.com/benallen-dev) 的[儲存庫](https://github.com/benallen-dev/xiaomi-lightbar)
獲得一些啟發，該儲存庫採用 MIT 授權。

沒有這些人的付出，這個專案不可能誕生！謝謝你們！

## 參與貢獻

如果你發現錯誤或有新功能的想法，歡迎開 issue 或建立 pull request。我很樂見這個
專案持續成長與改進！

我把程式碼設計成可以輕鬆™擴充。~~舉例來說，要新增支援多個燈條或多個遙控器應該
相對容易。~~

## 授權

本專案採用 MIT 授權。詳情請見 [LICENSE](LICENSE) 檔案——其中同時列出原始的
2024 年著作權聲明，以及本分支針對上述新增內容的 2026 年著作權聲明。
