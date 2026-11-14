[English](esp32-c3-port.md) · **繁體中文**

# ESP32-C3 移植 *（分支新增）*

`config-example-c3.h`——針對 ESP32-C3 開發板（已在絲印為 `ESP32-C3_MINI_V1` 的
ESP32-C3 Pro Mini 上測試）的設定範例，與上游原有、針對一般 ESP32 的
`config-example.h` 並存。兩者的主要差異在於接腳選擇，這是因為 C3 可用的 GPIO
遠比一般 ESP32 少：

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
——這正是上游自己所建議、能讓 Home Assistant 狀態值得信賴的作法（見讀我檔的
[限制與未解項目](../README.zh-TW.md#限制與未解項目)）。
