**English** · [繁體中文](mqtt-topics.zh-TW.md)

# MQTT topics and Home Assistant

You can control power state, brightness and color temperature with simple MQTT
messages, and receive the remote's state over MQTT as well. Every topic below
sits under the root topic from `config.h` (default `lightbar2mqtt`) plus
`l2m_<MAC of your ESP32>`.

All MQTT topics are prefixed with a root topic. You can set this root topic in
the `config.h` file. The default root topic is `lightbar2mqtt`. The root topic
is followed by the MAC address of your ESP32 in the format
`l2m_<MAC of your ESP32>`, e.g. `l2m_1234567890AB`. Just take a look at the
serial monitor of your ESP32 to find out the used root topic and MAC address, it
will be printed right after the startup message:

```text
...
[MQTT] Device ID: l2m_1234567890AB
[MQTT] Root Topic: lightbar2mqtt/l2m_12345679890AB
...
```

In order to be able to control multiple light bars or remotes, each one has
their own sub-topics, starting with the chosen serial (all lower case).

## Light Bar

To control the light bar, you can send messages to the following topic:
`<MQTT_ROOT_TOPIC>/l2m_<MAC of your ESP32>/0x<Serial of the light bar>/command`
e.g. `lightbar2mqtt/l2m_1234567890AB/0xabcdef/command`. The payload should be a
JSON object with the following keys:

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

## Remote

The remote sends its state to the following topic:
`<MQTT_ROOT_TOPIC>/l2m_<MAC of your ESP32>/0x<Serial of the remote>/state` e.g.
`lightbar2mqtt/l2m_1234567890AB/0x123456/state`. The payload is a plain string
with one the following values:

- `press`
- `turn_clockwise`
- `turn_counterclockwise`
- `hold`
- `press_turn_clockwise`
- `press_turn_counterclockwise`

## Pairing

To pair the light bar with the ESP32, send a message to the following topic:
`<MQTT_ROOT_TOPIC>/l2m_<MAC of your ESP32>/0x<Serial of the light bar>/pair` e.g.
`lightbar2mqtt/l2m_1234567890AB/0xabcdef/pair`. The payload can be anything, it
will be ignored.

Please note that the light bar needs to be power-cycled within 10 seconds _before_
sending the pairing message.

## Availability

The ESP32 sends its availability to the following topic:
`<MQTT_ROOT_TOPIC>/l2m_<MAC of your ESP32>/availability` e.g.
`lightbar2mqtt/l2m_1234567890AB/availability`. The payload is either `online` or
`offline`.

## Environment sensors *(fork addition)*

If enabled, the sensor module publishes to two further topics under the same
root — see [Environment sensors](environment-sensors.md) for the payload shape:

- `<MQTT_ROOT_TOPIC>/l2m_<MAC>/env/state` — sensor readings, every `SENSOR_UPDATE_INTERVAL_MS`
- `<MQTT_ROOT_TOPIC>/l2m_<MAC>/env/config` — the SCD41's configuration as read back at boot, retained

## Home Assistant

If your Home Assistant has the MQTT integration set up, the light bar(s) and
remote(s) should be discovered automatically.

- The light bar is represented as a `light` entity
- The remote is represented as a `sensor` entity
- Multiple light bars or remotes are no problem: each one can be
  controlled/monitored individually and has separate entities in Home Assistant.

Additionally, the above mentioned events from the remote are also available as
`device_automation` triggers. You can use these triggers to create automations
in Home Assistant based on the actions taken on the remote. To do so, create a
new automation in Home Assistant and select "Device" as the trigger type. Select
the corresponding remote entity and the desired trigger, e.g. `"press" action`.
