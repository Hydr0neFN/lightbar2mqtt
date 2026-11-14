**English** · [繁體中文](platformio-build.zh-TW.md)

# PlatformIO build *(fork addition)*

`platformio.ini`, added by this fork — upstream ships an Arduino IDE sketch
with no build config and no release binaries. `src_dir = .` keeps the sketch
in the repository root so diffs against upstream stay readable.

- Environment: `esp32-c3-pro-mini` (`espressif32` / `esp32-c3-devkitm-1`,
  4 MB flash).
- **Requires C++17** (`build_unflags = -std=gnu++11`, `-std=gnu++17`):
  `radio.h` declares `static constexpr byte preamble[8]` in-class with no
  out-of-line definition. That only links because C++17 makes static
  constexpr members implicitly inline; PlatformIO's `espressif32` platform
  otherwise defaults to `gnu++11`, where it's an ODR-use with no definition —
  `undefined reference to Radio::preamble` at link time. Upstream never hits
  this because the Arduino IDE's arduino-esp32 3.x compiles at `gnu++2a`.
- `-DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=1`: the C3 has no USB-UART
  bridge chip, so `Serial` goes over the built-in USB Serial/JTAG. Without
  these, `Serial.print()` is instead routed to GPIO20/21 and the serial
  monitor stays blank — which breaks step 7 of the installation instructions
  ([Setup](setup.md#2-software), reading the remote's serial off the monitor).
- `lib_deps` pins the same library versions upstream's README specifies, plus
  the two sensor libraries this fork adds.
- `[env:wifitest]` — same firmware with `-DWIFI_TEST_NO_RADIO`, which skips
  `radio.setup()`. Useful because that call blocks for up to 60 s and then
  reboots if no nRF24 answers, which otherwise makes it impossible to test
  WiFi/MQTT before the radio module is physically wired up. Never flash this
  for real use — it cannot talk to the light bar.
- `[env:esp32-c3-pro-mini-ota]` — see [OTA updates](ota-updates.md).

Build and upload the default environment:

```sh
pio run -e esp32-c3-pro-mini -t upload
```
