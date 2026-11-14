**English** · [繁體中文](README.zh-TW.md)

# lightbar2mqtt

Control a [Xiaomi Mi Computer Monitor Light Bar](https://www.mi.com/global/product/mi-computer-monitor-light-bar/)
with MQTT from an ESP32, and add it to Home Assistant. All you need is an ESP32,
an nRF24 module and a light bar.

This is a fork of [ebinf/lightbar2mqtt](https://github.com/ebinf/lightbar2mqtt)
(MIT, © 2024 Erik Borowski). It is a standalone copy, not a GitHub fork: the
upstream history is preserved in full at the root of this repo — all 14 commits
back to the initial one, by Erik Borowski and the other upstream contributors
(`git log`) — and `LICENSE` carries both copyright lines. Text this fork adds is
marked *(fork)*; the rest documents upstream's base project as it was written.

## At a glance

| | |
| :---- | :---- |
| **Problem** | The light bar speaks only a 2.4 GHz nRF24 protocol of its own. No WiFi, no MQTT, nothing Home Assistant can see or drive. |
| **What I built** | A bridge on an ESP32-C3: MQTT commands in, nRF24 packets out, the original remote's packets back out over MQTT, plus an optional BME280 + SCD41 room-sensor device on the same board. |
| **Result** | Live diagnostics on the deployment: ping latency swung 4 ms to 157 ms and the board reconnected to the broker about 300 times a day — those are the numbers the fixes below came from. Sensor publish every 60 s by default. |
| **Stack** | ESP32 / ESP32-C3, Arduino C++, PlatformIO, nRF24L01, MQTT, Home Assistant, BME280 + Sensirion SCD41 on I2C. |
| **My role** | Fork additions are this fork's own work: sensors, PlatformIO build, ESP32-C3 port, OTA, the reliability fixes and the `remote.cpp` bug fix. The base project is upstream's, by Erik Borowski and contributors. |

## How it works

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

The board joins WiFi and an MQTT broker. A JSON command on the light bar's
`command` topic becomes the same nRF24 packets the original remote sends. The
same radio stays listening for the remote, and its packets go back out as MQTT
state and `device_automation` triggers. Home Assistant discovery messages go out
when the MQTT connection comes up, so no hand-written YAML is needed. The sensor
module reuses that broker connection on a timer of its own.

The board was built for one deployment: an ESP32-C3, mains powered, running
headless in a room that also wanted CO2, temperature and humidity monitoring.

## What this fork adds

- **Environment sensors** — a BME280 + Sensirion SCD41 on the I2C bus,
  published as a second Home Assistant device alongside the light bar. See
  [Environment sensors](docs/environment-sensors.md).
- **PlatformIO build**, used for this fork's own development. Arduino IDE
  still works too, but not fully unmodified from upstream: `Lightbar.ino` now
  includes `sensors.h` unconditionally, so the two sensor libraries are a build
  requirement in the Arduino IDE too. See [PlatformIO build](docs/platformio-build.md).
- **ESP32-C3 port** — upstream targets a plain ESP32; this fork adds a pin
  mapping and config example for the C3's native USB and GPIO constraints. See
  [ESP32-C3 port](docs/esp32-c3-port.md).
- **OTA firmware updates**, password-gated, off by default. See
  [OTA updates](docs/ota-updates.md).
- **WiFi/MQTT reliability fixes** for a board that stayed on the same AP for
  weeks rather than a dev cycle. See [Fixes](docs/fixes.md#wifi-and-mqtt-reliability).
- **A real bug fix** in `remote.cpp`'s listener removal, present in upstream
  too. See [Fixes](docs/fixes.md#remote-listener-fix).

## What the fixes came from

**The WiFi link kept dropping.** Ping latency swung between 4 ms and 157 ms, and
MQTT keepalives and TCP ACKs went missing often enough to cause several hundred
reconnects a day. The cause was modem sleep, the ESP32 default: it parks the
radio between DTIM beacons. The board is mains powered, so the ~20 mA it saved
bought nothing. Fixed with `WiFi.setSleep(false)` and
`WiFi.setAutoReconnect(true)`. Two more causes were fixed in the same pass:
PubSubClient's 15 s keepalive, enforced by most brokers at 1.5×,
made any stall past ~22 s look like a dead connection; and its 256-byte publish
buffer silently returned `false` on Home Assistant discovery payloads of
650–900 bytes. Keepalive is now 60 s, socket timeout 10 s, and publishes stream
through `beginPublish`/`print`/`endPublish` instead of a buffer.

**The remote listener removal never compared anything.** Under PlatformIO the
line did not even compile: `std::function::target<T>()` needs RTTI, and
arduino-esp32 builds with `-fno-rtti`. Turning RTTI on would not have made it
work. The template argument is a function type while what is stored is a
`std::bind` object, so `target<T>()` returns `nullptr` on both sides and the test
is `nullptr == nullptr` — always true on the first loop iteration. Shipped
behaviour has always been "drop the first registered listener". This fork states
that behaviour explicitly instead of faking a comparison `std::function` has no
operator for. That is correct for the only caller, which registers one listener
per remote. A second listener on the same remote would need a real identity
scheme.

## Build / run

Requirements:

- a Xiaomi Mi Computer Monitor Light Bar, Model MJGJD**01**YL (without
  BLE/WiFi). The MJGJD**02**YL will not work!
- an ESP32 (or ESP32-C3 — see [ESP32-C3 port](docs/esp32-c3-port.md))
- a nRF24 module (tested with nRF24L01)
- *(fork, optional)* a BME280 and/or a Sensirion SCD4x on the same I2C bus, for
  [Environment sensors](docs/environment-sensors.md)

Shortest path with PlatformIO:

```sh
cp config-example-c3.h config.h     # plain ESP32: cp config-example.h config.h
pio run -e esp32-c3-pro-mini -t upload
```

Then open the serial monitor at 115200 baud. The board prints its device ID and
root topic; pressing or turning the remote prints the remote's serial, which goes
into `config.h` before the next upload. The full wiring table, the ten Arduino
IDE steps and the library versions are in [Setup](docs/setup.md).

## Repo layout

| Path | What it is |
| :---- | :---- |
| `Lightbar.ino` | Sketch entry point: `setup()`, `loop()`, OTA setup. |
| `lightbar.*`, `remote.*` | The two device types and their commands. |
| `radio.*` | nRF24 setup, packet build/parse, CRC check. |
| `mqtt.*` | Broker connection, topics, Home Assistant discovery. |
| `sensors.*` | BME280 + SCD41 module *(fork)*. |
| `constants.h`, `config_defaults.h` | Version, limits, default pins. |
| `config-example.h`, `config-example-c3.h` | Config templates; copy one to `config.h` (gitignored). |
| `platformio.ini` | PlatformIO environments *(fork)*. |
| `docs/` | The deep sections, in English and zh-TW. |
| `LICENSE` | MIT, upstream and fork copyright lines. |

## Deep dives

| Topic | English | 繁體中文 |
| :---- | :---- | :---- |
| Wiring, Arduino IDE steps, pairing | [setup.md](docs/setup.md) | [setup.zh-TW.md](docs/setup.zh-TW.md) |
| MQTT topics, payloads, Home Assistant | [mqtt-topics.md](docs/mqtt-topics.md) | [mqtt-topics.zh-TW.md](docs/mqtt-topics.zh-TW.md) |
| Environment sensors *(fork)* | [environment-sensors.md](docs/environment-sensors.md) | [environment-sensors.zh-TW.md](docs/environment-sensors.zh-TW.md) |
| PlatformIO build *(fork)* | [platformio-build.md](docs/platformio-build.md) | [platformio-build.zh-TW.md](docs/platformio-build.zh-TW.md) |
| ESP32-C3 port *(fork)* | [esp32-c3-port.md](docs/esp32-c3-port.md) | [esp32-c3-port.zh-TW.md](docs/esp32-c3-port.zh-TW.md) |
| OTA updates *(fork)* | [ota-updates.md](docs/ota-updates.md) | [ota-updates.zh-TW.md](docs/ota-updates.zh-TW.md) |
| Reliability fixes and the listener bug *(fork)* | [fixes.md](docs/fixes.md) | [fixes.zh-TW.md](docs/fixes.zh-TW.md) |

## Limits and open items

- The light bar does not send its state to the ESP32. If you change the state via
  the controller, the ESP32 will not know. This is a limitation of the protocol
  used by the light bar.
- There is no way of knowing whether the light bar is on or off. This project
  assumes it is on when the ESP32 starts. If you turn the light bar off (e.g. via
  the remote), Home Assistant can show an inverted state. Turn the device on in
  Home Assistant and power-cycle the light bar to fix this.
- Sometimes actions taken on the remote are not recognized by the ESP32. Do not
  rely on remote events to be 100% accurate when you build automations; normally
  the second or third try should work. It is therefore also recommended to
  decouple the light bar and original remote, as otherwise some actions on the
  remote might change the state of the light bar but not trigger anything in Home
  Assistant.
- The listener fix above is correct for one listener per remote. A second
  listener on the same remote needs a real identity scheme — e.g. a token handed
  back by `registerCommandListener()`.
- `[env:wifitest]` builds skip `radio.setup()` and cannot talk to the light bar.
  Never flash one for real use.

## Credits

This project is heavily based on the reverse engineering work done by
[lamperez](https://github.com/lamperez). Take a look at their
[repository](https://github.com/lamperez/xiaomi-lightbar-nrf24) for more on the
protocol, the reverse engineering process and a Python version running on
Raspberry Pi. It is licensed under the GNU General Public License v3.0.

I also took some inspiration from [Ben Allen](https://github.com/benallen-dev) and
their [repository](https://github.com/benallen-dev/xiaomi-lightbar), licensed
under the MIT license.

This project would not have been possible without the work of these amazing
people! Thank you!

## Contributing

If you find a bug or have an idea for a new feature, feel free to open an issue
or create a pull request. I'm happy to see this project grow and improve!

I've designed the code to be easily™ extendable. ~~For example, it should be
relatively easy to add support for multiple light bars or multiple remotes.~~

## License

This project is licensed under the MIT License. See the [LICENSE](LICENSE) file
for details — it carries both the original 2024 copyright and this fork's 2026
copyright for the additions listed above.
