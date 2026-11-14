**English** · [繁體中文](fixes.zh-TW.md)

# Fixes *(fork addition)*

Two independent problems found on the same deployment: a WiFi link that kept
dropping, and a listener removal in `remote.cpp` that never compared anything.

## WiFi and MQTT reliability

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

## Remote listener fix

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
