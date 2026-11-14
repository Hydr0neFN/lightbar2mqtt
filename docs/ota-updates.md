**English** · [繁體中文](ota-updates.zh-TW.md)

# OTA updates *(fork addition)*

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
