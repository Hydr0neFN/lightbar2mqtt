**English** · [繁體中文](setup.zh-TW.md)

# Setup

Wiring, the Arduino IDE workflow and pairing. Requirements are in the
[README](../README.md#build--run).

## 1. Hardware

Connect the nRF24 module to the ESP32 as follows. At least these pins work for my
combination of ESP32 and nRF24 module. You can change the CE & CSN pins in the
`config.h` file if you need to. The SPI pins (SCK, MOSI, MISO) might be different
on your ESP32 – check the pinout of your ESP32!

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
[ESP32-C3 port](esp32-c3-port.md).

## 2. Software

Upstream's Arduino IDE workflow (below) still works, with one change that
applies regardless of which board or IDE you use: step 4 now needs two
additional libraries, because `Lightbar.ino` includes the fork's `sensors.h`
unconditionally. This fork itself is built and developed with PlatformIO
instead — see [PlatformIO build](platformio-build.md) if you'd rather use that.

1. Clone this repository
2. Copy the `config-example.h` file to `config.h` (or `config-example-c3.h` for
   an ESP32-C3 — see [ESP32-C3 port](esp32-c3-port.md)) and adjust
   the settings to your needs.
3. Connect your ESP32 to your computer.
4. Open the Arduino IDE and install the required libraries:
   - [Arduino_JSON](https://github.com/arduino-libraries/Arduino_JSON) by Arduino, _Version 0.2.0_
   - [CRC](https://github.com/RobTillaart/CRC) by Rob Tillaart, _Version 1.0.3_
   - [PubSubClient](https://pubsubclient.knolleary.net/) by Nick O'Leary, _Version 2.8_
   - [RF24](https://nrf24.github.io/RF24/) by TMRh20, _Version 1.4.10_
   - *(fork)* [Adafruit BME280 Library](https://github.com/adafruit/Adafruit_BME280_Library) and [Sensirion I2C SCD4x](https://github.com/Sensirion/arduino-i2c-scd4x) — `Lightbar.ino` includes `sensors.h` unconditionally, so these two are build requirements now, not opt-in. It's the physical sensors that are optional: neither chip has to actually be on the bus (see [Environment sensors](environment-sensors.md)), just the libraries have to be installed to compile.
5. Select your serial port and board. Upload the sketch to your ESP32.
6. Inspect the serial monitor (115200 baud). If everything is set up correctly, the ESP32 should connect to your WiFi and MQTT broker.
7. At this point, you probably don't know the serial of your remote. Just press/turn the remote and you should see the serial in the serial monitor. (E.g. `[Radio] Ignoring package with not matching serial: 0x7B7E12`) Copy it and paste it into the `config.h` file in the "Remotes" section.
8. Upload the sketch again.
9. If your Home Assistant has the MQTT integration set up, the light bar should be discovered automatically.
10. Enjoy controlling your light bar via MQTT!

## 3. Pairing Light bar and ESP32 (optional)

If you opt to use different values for the serial and remote in the `config.h`
file, you need to pair the light bar with the ESP32. To do this, power-cycle the
light bar and within 10 seconds either press the "Pair" button in Home Assistant
or send a message to the pair topic (see
[MQTT topics](mqtt-topics.md#pairing)). The light bar should blink a few times
if the pairing was successful.

A serial that matches a remote's keeps the original remote in control of the
light bar. A different serial decouples the two, which gains full control for
the ESP32.
