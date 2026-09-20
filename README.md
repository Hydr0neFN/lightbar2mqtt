**English** · [繁體中文](README.zh-TW.md)

# lightbar2mqtt (fork)

> Based on [ebinf/lightbar2mqtt](https://github.com/ebinf/lightbar2mqtt) (MIT,
> © 2024 Erik Borowski). This is a standalone copy, not a GitHub fork — the
> upstream history is preserved in full at the root of this repo — all 14 commits
> back to the initial one, by Erik Borowski and the other upstream contributors
> (`git log`) — and `LICENSE` carries both copyright lines. [What this fork adds](#what-this-fork-adds)
> below is new; the rest of this README is upstream's own documentation of the
> base project, left as written, with `*(fork)*` markers added inline wherever
> this fork changes something in an otherwise-upstream section.

Control your [Xiaomi Mi Computer Monitor Light Bar](https://www.mi.com/global/product/mi-computer-monitor-light-bar/) with MQTT and add it to Home Assistant! All you need is a ESP32, a nRF24 module and a light bar of course.

## What this fork adds

Everything below was written for a specific deployment: an ESP32-C3, mains
powered, running headless in a room that also wanted CO2/temperature/humidity
monitoring. The code changes are this fork's own — including the last bullet,
which fixes a bug that exists in upstream but is only fixed here. See
[Acknowledgements](#acknowledgements) for upstream's own credits, which this
fork keeps untouched.

- **Environment sensors** — a BME280 + Sensirion SCD41 on the I2C bus,
  published as a second Home Assistant device alongside the light bar. See
  [Environment sensors](#environment-sensors-fork-addition).
- **PlatformIO build**, used for this fork's own development. Arduino IDE
  still works too (see [PlatformIO build](#platformio-build-fork-addition)),
  but not fully unmodified from upstream: `Lightbar.ino` now includes
  `sensors.h` unconditionally, so the two sensor libraries below are a build
  requirement in the Arduino IDE too, not just under PlatformIO.
- **ESP32-C3 port** — upstream targets a plain ESP32; this fork adds a pin
  mapping and config example for the ESP32-C3's native USB and GPIO
  constraints. See [ESP32-C3 port](#esp32-c3-port-fork-addition).
- **OTA firmware updates**, password-gated, off by default. See
  [OTA updates](#ota-updates-fork-addition).
- **WiFi/MQTT reliability fixes** for a board that stayed on the same AP for
  weeks rather than a dev cycle — see [Reliability fixes](#wifi--mqtt-reliability-fixes-fork-addition).
- **A real bug fix** in `remote.cpp`'s listener removal, present in upstream
  too — see [Remote listener fix](#remote-listener-fix-fork-addition).

## Acknowledgements

This project is heavily based on the amazing reverse engineering work done by [lamperez](https://github.com/lamperez). Take a look at their [repository](https://github.com/lamperez/xiaomi-lightbar-nrf24) for more information on the protocol used, the reverse engineering process and a Python version running on Raspberry Pi. It is licensed under the GNU General Public License v3.0.

I also took some inspiration from [Ben Allen](https://github.com/benallen-dev) and their [repository](https://github.com/benallen-dev/xiaomi-lightbar), licensed under the MIT license.

This project would not have been possible without the work of these amazing people! Thank you!

## Features

- Control power state, brightness and color temperature via simple MQTT messages
- Receive state updates of the remote via MQTT as well
- Either use the controller aditionally or decouple light bar and remote to gain full control
- Integrates with Home Assistant – either manually or with zero effort using the automatic discovery feature
  - The light bar is represented as a `light` entity
  - The remote is represented as a `sensor` entity
  - Actions taken on the remote also trigger `device_automation`s. This allows you to trigger automations in Home Assistant based on actions taken on the remote
- Use multiple light bars or remotes with ease! Each one can be controlled/monitored individually and will also have separate entities in Home Assistant.
- *(fork)* Optional BME280/SCD41 environment sensors published as their own Home Assistant device.
- *(fork)* Optional password-gated OTA firmware updates.

## Requirements

- a Xiaomi Mi Computer Monitor Light Bar, Model MJGJD**01**YL (without BLE/WiFi). The MJGJD**02**YL will not work!
- an ESP32 (or ESP32-C3 — see [ESP32-C3 port](#esp32-c3-port-fork-addition))
- a nRF24 module (tested with nRF24L01)
- *(fork, optional)* a BME280 and/or a Sensirion SCD4x on the same I2C bus, for [Environment sensors](#environment-sensors-fork-addition)

## Installation

### 1. Hardware

Connect the nRF24 module to the ESP32 as follows. At least these pins work for my combination of ESP32 and nRF24 module. You can change the CE & CSN pins in the `config.h` file if you need to. The SPI pins (SCK, MOSI, MISO) might be different on your ESP32 – check the pinout of your ESP32!

| nRF24 |  ESP32 |
| :---- | -----: |
| VCC   |    3V3 |
| GND   |    GND |
| CE    |  Pin 4 |
| CSN   |  Pin 5 |
| SCK   | Pin 18 |
| MOSI  | Pin 23 |
| MISO  | Pin 19 |

For the ESP32-C3 pin mapping this fork uses instead, see
[ESP32-C3 port](#esp32-c3-port-fork-addition).

### 2. Software

Upstream's Arduino IDE workflow (below) still works, with one change that
applies regardless of which board or IDE you use: step 4 now needs two
additional libraries, because `Lightbar.ino` includes the fork's `sensors.h`
unconditionally. This fork itself is built and developed with PlatformIO
instead — see [PlatformIO build](#platformio-build-fork-addition) if you'd
rather use that.

1. Clone this repository
2. Copy the `config-example.h` file to `config.h` (or `config-example-c3.h` for
   an ESP32-C3 — see [ESP32-C3 port](#esp32-c3-port-fork-addition)) and adjust
   the settings to your needs.
3. Connect your ESP32 to your computer.
4. Open the Arduino IDE and install the required libraries:
   - [Arduino_JSON](https://github.com/arduino-libraries/Arduino_JSON) by Arduino, _Version 0.2.0_
   - [CRC](https://github.com/RobTillaart/CRC) by Rob Tillaart, _Version 1.0.3_
   - [PubSubClient](https://pubsubclient.knolleary.net/) by Nick O'Leary, _Version 2.8_
   - [RF24](https://nrf24.github.io/RF24/) by TMRh20, _Version 1.4.10_
   - *(fork)* [Adafruit BME280 Library](https://github.com/adafruit/Adafruit_BME280_Library) and [Sensirion I2C SCD4x](https://github.com/Sensirion/arduino-i2c-scd4x) — `Lightbar.ino` includes `sensors.h` unconditionally, so these two are build requirements now, not opt-in. It's the physical sensors that are optional: neither chip has to actually be on the bus (see [Environment sensors](#environment-sensors-fork-addition)), just the libraries have to be installed to compile.
5. Select your serial port and board. Upload the sketch to your ESP32.
6. Inspect the serial monitor (115200 baud). If everything is set up correctly, the ESP32 should connect to your WiFi and MQTT broker.
7. At this point, you probably don't know the serial of your remote. Just press/turn the remote and you should see the serial in the serial monitor. (E.g. `[Radio] Ignoring package with not matching serial: 0x7B7E12`) Copy it and paste it into the `config.h` file in the "Remotes" section.
8. Upload the sketch again.
9. If your Home Assistant has the MQTT integration set up, the light bar should be discovered automatically.
10. Enjoy controlling your light bar via MQTT!

### 3. Pairing Light bar and ESP32 (optional)

If you opt to use different values for the serial and remote in the `config.h` file, you need to pair the light bar with the ESP32. To do this, power-cycle the light bar and within 10 seconds either press the "Pair" button in Home Assistant or send a message to the pair topic (see below). The light bar should blink a few times if the pairing was successful.

## Usage

### MQTT Topics

All MQTT topics are prefixed with a root topic. You can set this root topic in the `config.h` file. The default root topic is `lightbar2mqtt`. The root topic is followed by the MAC address of your ESP32 in the format `l2m_<MAC of your ESP32>`, e.g. `l2m_1234567890AB`. Just take a look at the serial monitor of your ESP32 to find out the used root topic and MAC address, it will be printed right after the startup message:

```text
...
[MQTT] Device ID: l2m_1234567890AB
[MQTT] Root Topic: lightbar2mqtt/l2m_12345679890AB
...
```

In order to be able to control multiple light bars or remotes, each one has their own sub-topics, starting with the chosen serial (all lower case).

#### Light Bar

To control the light bar, you can send messages to the following topic: `<MQTT_ROOT_TOPIC>/l2m_<MAC of your ESP32>/0x<Serial of the light bar>/command` e.g. `lightbar2mqtt/l2m_1234567890AB/0xabcdef/command`. The payload should be a JSON object with the following keys:

- `state`: `"ON"` or `"OFF"`
- `brightness`: `0` (off) to `15` (full brightness)
- `color_temp`: Mireds – `153` (cold) to `370` (warm)

Example:

```json
{
  "state": "ON",
  "brightness": 15,
  "color_temp": 153
}
```

#### Remote

The remote sends its state to the following topic: `<MQTT_ROOT_TOPIC>/l2m_<MAC of your ESP32>/0x<Serial of the remote>/state` e.g. `lightbar2mqtt/l2m_1234567890AB/0x123456/state`. The payload is a plain string with one the following values:

- `press`
- `turn_clockwise`
- `turn_counterclockwise`
- `hold`
- `press_turn_clockwise`
- `press_turn_counterclockwise`

#### Pairing

To pair the light bar with the ESP32, send a message to the following topic: `<MQTT_ROOT_TOPIC>/l2m_<MAC of your ESP32>/0x<Serial of the light bar>/pair` e.g. `lightbar2mqtt/l2m_1234567890AB/0xabcdef/pair`. The payload can be anything, it will be ignored.

Please note that the light bar needs to be power-cycled within 10 seconds _before_ sending the pairing message.

#### Availability

The ESP32 sends its availability to the following topic: `<MQTT_ROOT_TOPIC>/l2m_<MAC of your ESP32>/availability` e.g. `lightbar2mqtt/l2m_1234567890AB/availability`. The payload is either `online` or `offline`.

#### Environment sensors *(fork addition)*

If enabled, the sensor module publishes to two further topics under the same
root — see [Environment sensors](#environment-sensors-fork-addition) for the
payload shape:

- `<MQTT_ROOT_TOPIC>/l2m_<MAC>/env/state` — sensor readings, every `SENSOR_UPDATE_INTERVAL_MS`
- `<MQTT_ROOT_TOPIC>/l2m_<MAC>/env/config` — the SCD41's configuration as read back at boot, retained

### Home Assistant

If your Home Assistant has the MQTT integration set up, the light bar(s) and remote(s) should be discovered automatically.

Additionally, the above mentioned events from the remote are also available as `device_automation` triggers. You can use these triggers to create automations in Home Assistant based on the actions taken on the remote. To do so, create a new automation in Home Assistant and select "Device" as the trigger type. Select the corresponding remote entity and the desired trigger, e.g. `"press" action`.

### Known Issues / Limitations

- The light bar does not send its state to the ESP32. This means that if you change the state of the light bar via the controller, the ESP32 will not know about it. This is a limitation of the protocol used by the light bar.
- There is no way of knowing whether the light bar is currently on or off. Therefore this project assumes that the light bar is on when the ESP32 starts. If you turn off the light bar (e.g. via the remote), this might lead to an inverted state in Home Assistant. Just turn the device on in Home Assistant and power-cycle the light bar to fix this.
- Sometimes actions taken on the remote are not recognized by the ESP32. When building automations in Home Assistant, don't rely on the remote events to be 100% accurate. Normally, the second or third try should work. It is therefore also recommended to decouple the light bar and original remote, as otherwise some actions on the remote might change the state of the light bar but not trigger anything in Home Assistant.

## Environment sensors *(fork addition)*

An optional BME280 (temperature/humidity/pressure) and Sensirion SCD41
(CO2/temperature/humidity) on one I2C bus, in `sensors.cpp`/`sensors.h`. Not
part of upstream. Both sensors are independently optional — the module probes
for each at boot and only publishes entities for the ones it finds.

- Published as a **separate Home Assistant device** ("Room Environment", ID
  `<client id>_env`), distinct from the light bar's own device.
- Non-blocking by design: `Radio::loop()` polls the nRF24 for remote packets
  every cycle, and a packet missed there is a lost remote press. The sensor
  module's `loop()` is timer-gated (`SENSOR_UPDATE_INTERVAL_MS`, default 60 s)
  and only the SCD41's `stopPeriodicMeasurement()` at boot blocks (~500 ms);
  everything else returns immediately.
- The BME280 feeds its pressure reading to the SCD41 (`setAmbientPressure()`),
  since NDIR CO2 measurement is pressure-dependent — this is the reason the
  two sensors share a bus rather than being independent add-ons.
- The BME280 is probed at both `0x76` and `0x77`, since that differs between
  breakout board vendors depending on how the `SDO` pin is tied.
- `SENSOR_SCD4X_TEMP_OFFSET` compensates the SCD41 self-heating next to a WiFi SoC
  (default 4 °C, Sensirion's own default); the right value depends on the
  enclosure and is meant to be tuned in place. The sensor's own temperature/
  humidity and the delta against the BME280 (`t_delta`) are published as
  diagnostic entities specifically so that tuning has something to read.
- Every `env/state` update also carries `rssi`, `uptime_s`, `mqtt_connects`
  and `reset_reason` (`POWERON` / `BROWNOUT` / `PANIC` / …, from
  `esp_reset_reason()`) as diagnostic entities — link-quality telemetry
  riding on the same publish cycle, useful for a headless board on a
  marginal WiFi link. (`env/config`, below, is a separate, boot-only
  message and doesn't carry these.)
- Payload example (`.../env/state`):
  ```json
  {"temperature":21.34,"humidity":47.02,"pressure":1013.25,"co2":612,"scd_temperature":24.88,"scd_humidity":41.10,"t_delta":3.54,"rssi":-58,"uptime_s":86412,"mqtt_connects":1,"reset_reason":"POWERON"}
  ```
- Config macros (all optional, in `config.h`): `SENSOR_PIN_SDA`,
  `SENSOR_PIN_SCL`, `SENSOR_I2C_CLOCK` (100 kHz default — the SCD4x's own
  ceiling; running the bus faster does not fail cleanly, it shows up as
  intermittent NACKs and implausible readings), `SENSOR_UPDATE_INTERVAL_MS`,
  `SENSOR_SCD4X_TEMP_OFFSET`.

## PlatformIO build *(fork addition)*

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
  above (reading the remote's serial off the monitor).
- `lib_deps` pins the same library versions upstream's README specifies, plus
  the two sensor libraries this fork adds.
- `[env:wifitest]` — same firmware with `-DWIFI_TEST_NO_RADIO`, which skips
  `radio.setup()`. Useful because that call blocks for up to 60 s and then
  reboots if no nRF24 answers, which otherwise makes it impossible to test
  WiFi/MQTT before the radio module is physically wired up. Never flash this
  for real use — it cannot talk to the light bar.
- `[env:esp32-c3-pro-mini-ota]` — see [OTA updates](#ota-updates-fork-addition).

## ESP32-C3 port *(fork addition)*

`config-example-c3.h` — a config example for an ESP32-C3 board (tested on an
ESP32-C3 Pro Mini, silkscreen `ESP32-C3_MINI_V1`), alongside upstream's own
`config-example.h` for a plain ESP32. The two differ mainly in pin choice,
forced by the C3 having far fewer usable GPIOs:

| Signal      | Pin (C3) | Why |
| :---------- | -------: | :-- |
| `RADIO_PIN_SCK`  | GPIO 4  | native FSPI pin |
| `RADIO_PIN_MISO` | GPIO 5  | native FSPI pin |
| `RADIO_PIN_MOSI` | GPIO 6  | native FSPI pin |
| `RADIO_PIN_CSN`  | GPIO 7  | native FSPI pin |
| `RADIO_PIN_CE`   | GPIO 10 | last fully unencumbered pin |
| *(sensors)* `SENSOR_PIN_SDA` | GPIO 0 | not GPIO8 — see below |
| *(sensors)* `SENSOR_PIN_SCL` | GPIO 1 | not GPIO9 — see below |

Deliberately avoided: GPIO8/GPIO9 (strapping pins — GPIO8 also drives the
onboard LED, GPIO9 is the BOOT button; a sensor holding SDA/SCL low during a
bus hang at power-up would prevent the board from booting at all, which would
look like a dead board rather than a sensor fault) and GPIO20/GPIO21 (UART,
needed for the serial monitor in step 7 of the installation instructions).

The example config also documents a specific decoupled setup: the light bar's
serial in `LIGHTBARS[]` is deliberately *not* the physical remote's, so once
paired, the ESP becomes the only thing that can change the bar's state — the
mode upstream itself recommends for a trustworthy Home Assistant state (see
[Known Issues](#known-issues--limitations) above).

## OTA updates *(fork addition)*

Defining `OTA_PASSWORD` in `config.h` enables [ArduinoOTA](https://docs.espressif.com/projects/arduino-esp32/en/latest/tutorials/ota_web_update.html);
leaving it undefined compiles no OTA code in at all — this is opt-in, not a
default. `OTA_PORT` defaults to `3232`.

**Security note, not a formality:** this firmware compiles the WiFi PSK and
the MQTT password directly into the image. That is *also* why `dist/` — where
a built `firmware.bin` would end up — is gitignored (see `.gitignore`): a
build artefact from a tree with real secrets in `config.h` carries those
secrets, extractable, into anything the file gets shared with. An
unauthenticated OTA port would hand the same two secrets to anyone on the LAN,
which is why `OTA_PASSWORD` is mandatory the moment OTA is turned on, not
optional.

Flash it with the `esp32-c3-pro-mini-ota` PlatformIO environment:

```sh
pio run -e esp32-c3-pro-mini-ota -t upload
```

`upload_port` in `platformio.ini` is left as a placeholder (`<device-ip>`) —
replace it with your device's actual IP, since a device's LAN address has no
business being hardcoded in a file this fork ships to other people. The OTA
password itself is read from the `LIGHTBAR_OTA_PASSWORD` environment variable
(`platformio.ini` is tracked by git; `config.h` is not), set once with:

```sh
setx LIGHTBAR_OTA_PASSWORD "<same value as OTA_PASSWORD in config.h>"
```

`espota` has the device open a TCP connection back to the host machine on a
pinned port (`3233`), which Windows Firewall blocks by default — see the
comment in `platformio.ini` for the one-time `New-NetFirewallRule` needed to
allow it.

## WiFi / MQTT reliability fixes *(fork addition)*

Found and fixed running one of these boards mains-powered on a marginal WiFi
link for weeks, rather than a dev cycle:

- **`WiFi.setSleep(false)`** — modem sleep (the ESP32 default) parks the radio
  between DTIM beacons, which is why ping latency swung between 4 ms and
  157 ms and why MQTT keepalives and TCP ACKs went missing often enough to
  cause several hundred reconnects a day. The board is mains powered, so the
  ~20 mA saved by sleeping bought nothing against that cost. Paired with
  `WiFi.setAutoReconnect(true)`.
- **MQTT keepalive widened to 60 s**, socket timeout set to 10 s
  (`mqtt.cpp`) — PubSubClient's 15 s default keepalive, enforced by most
  brokers at 1.5×, meant any WiFi stall past ~22 s looked like a dead
  connection and triggered a reconnect. Nothing here needs sub-minute
  liveness detection.
- **A `publish()` wrapper on `MQTT`** (`mqtt.cpp`/`mqtt.h`) using
  PubSubClient's streaming `beginPublish`/`print`/`endPublish` API instead of
  its buffered `publish()`. That buffer defaults to 256 bytes and is never
  enlarged anywhere in this project; Home Assistant discovery payloads run
  650–900 bytes, so the buffered call would just return `false` — with
  nothing logged anywhere to say why. This is also what makes the sensor
  module's publishes possible, since `MQTT`'s underlying `PubSubClient` was
  otherwise private to it.
- **A connection counter** (`MQTT::getConnectCount()`) — how many times
  `setup()` has completed a broker connection. The first is boot; every one
  after that is a reconnect, which is the metric that actually matters when
  the WiFi link is the suspect. Surfaced in the sensor module's diagnostics
  (`mqtt_connects`) precisely because that's what turned up ~300 reconnects a
  day on this deployment with no way, until then, to see why.

## Remote listener fix *(fork addition)*

`Remote::unregisterCommandListener()` in `remote.cpp` did not compile under
PlatformIO's build: upstream's comparison uses `std::function::target<T>()`,
which needs RTTI, and arduino-esp32 builds with `-fno-rtti`.

Investigating why turned up a pre-existing bug, not just a portability gap:
enabling RTTI would only have made the line *compile*, not work. Upstream's
template argument there is a function type, while what's actually stored is a
`std::bind` object — so `target<T>()` returns `nullptr` on *both* sides
regardless of RTTI, and the comparison is `nullptr == nullptr`, always true on
the very first loop iteration. The comparison has therefore never actually
compared anything; the real, shipped behaviour has always been "drop the first
registered listener".

This fork's fix (see the comment at `remote.cpp:59`) makes that existing
behaviour explicit rather than trying to restore a comparison `std::function`
has no operator for. It's correct for the only caller
(`MQTT::removeRemote()`), which registers exactly one listener per remote. If
a second listener is ever registered on the same remote, this needs a real
identity scheme — e.g. a token handed back by `registerCommandListener()` —
not another attempt to compare the callables.

## Contributing

If you find a bug or have an idea for a new feature, feel free to open an issue or create a pull request. I'm happy to see this project grow and improve!

I've designed the code to be easily™ extendable. ~~For example, it should be relatively easy to add support for multiple light bars or multiple remotes.~~

## License

This project is licensed under the MIT License. See the [LICENSE](LICENSE)
file for details — it carries both the original 2024 copyright and this
fork's 2026 copyright for the additions listed above.
