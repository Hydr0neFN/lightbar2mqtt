[English](fixes.md) · **繁體中文**

# 修正紀錄 *（分支新增）*

同一個部署現場抓到兩個互不相干的問題：一直中斷的 WiFi 連線，以及
`remote.cpp` 裡一段從來沒真正比對過任何東西的監聽器移除邏輯。

## WiFi 與 MQTT 穩定性

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
  觸發重新連線。這裡並不需要這麼細的存活偵測。
- **在 `MQTT` 上新增一個 `publish()` 包裝函式**（`mqtt.cpp`／
  `mqtt.h`），改用 PubSubClient 的串流式 `beginPublish`／`print`／
  `endPublish` API，不經過有緩衝區限制的 `publish()`。該緩衝區預設只有
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

## Remote 監聽器修正

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
