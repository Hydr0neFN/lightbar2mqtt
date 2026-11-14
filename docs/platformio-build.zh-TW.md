[English](platformio-build.md) · **繁體中文**

# PlatformIO 建置 *（分支新增）*

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
  `Serial.print()` 會改走 GPIO20/21，序列埠監控視窗就會沒有任何輸出——這樣
  一來，[安裝設定](setup.zh-TW.md#2-軟體)的第 7 步（從監控視窗讀取遙控器序號）
  就做不到了。
- `lib_deps` 所釘選的函式庫版本，與上游 README 所指定的版本相同，並加上本
  分支新增的兩個感測器函式庫。
- `[env:wifitest]`——與正式版相同的韌體，但加上 `-DWIFI_TEST_NO_RADIO`，會
  跳過 `radio.setup()`。這個呼叫在找不到任何 nRF24 回應時會阻塞長達 60 秒
  並重開機，若不跳過它，在 nRF24 模組實際接線完成之前就無法測試 WiFi／
  MQTT。切勿在正式使用時燒錄此環境——它無法與燈條通訊。
- `[env:esp32-c3-pro-mini-ota]`——見 [OTA 更新](ota-updates.zh-TW.md)。

建置並上傳預設環境：

```sh
pio run -e esp32-c3-pro-mini -t upload
```
