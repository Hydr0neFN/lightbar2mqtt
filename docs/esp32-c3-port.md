**English** · [繁體中文](esp32-c3-port.zh-TW.md)

# ESP32-C3 port *(fork addition)*

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
[Known Issues](../README.md#limits-and-open-items) in the README).
