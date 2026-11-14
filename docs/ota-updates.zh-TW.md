[English](ota-updates.md) · **繁體中文**

# OTA 更新 *（分支新增）*

在 `config.h` 中定義 `OTA_PASSWORD` 即可啟用
[ArduinoOTA](https://docs.espressif.com/projects/arduino-esp32/en/latest/tutorials/ota_web_update.html)；
不定義的話，OTA 相關程式碼完全不會被編譯進去——這是選用（opt-in）功能，預設
不啟用。`OTA_PORT` 預設為 `3232`。

**這一條是安全性問題，別把它當成形式上的免責聲明：**這份韌體會把 WiFi 密碼（PSK）與
MQTT 密碼直接編譯進映像檔裡。這也是為什麼 `dist/`（建置出來的 `firmware.bin`
所在位置）會被列入 `.gitignore`（見 `.gitignore`）：只要 `config.h` 中含有
真實金鑰，從該工作目錄建置出的任何產出物，都會把這兩組金鑰以能被還原出來的
形式，帶入任何分享出去的檔案裡。未經身分驗證的 OTA 連接埠，同樣會把這兩組
金鑰交給區域網路上的任何人——這就是為什麼一旦啟用 OTA，`OTA_PASSWORD` 就是
必要項目，沒有它不行。

用 `esp32-c3-pro-mini-ota` 這個 PlatformIO 環境燒錄：

```sh
pio run -e esp32-c3-pro-mini-ota -t upload
```

`platformio.ini` 中的 `upload_port` 刻意留為預留位置（`<device-ip>`）——
請自行換成你裝置實際的 IP 位址，因為裝置的區域網路位址不應該寫死在一份會
分享給其他人使用的檔案裡。OTA 密碼本身則是從環境變數
`LIGHTBAR_OTA_PASSWORD` 讀取（`platformio.ini` 受 git 版本控制，`config.h`
則否），只需設定一次：

```sh
setx LIGHTBAR_OTA_PASSWORD "<same value as OTA_PASSWORD in config.h>"
```

`espota` 會讓裝置反過來對主機開啟一條 TCP 連線，使用固定連接埠（`3233`），
而 Windows 防火牆預設會擋下這條連線——只需跑一次的 `New-NetFirewallRule`
指令，請見 `platformio.ini` 中的註解。
