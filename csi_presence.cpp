#include "csi_presence.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <ping/ping_sock.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#if __has_include(<Update.h>)
#include <Update.h>
#define CSI_HAVE_UPDATE 1
#else
#define CSI_HAVE_UPDATE 0
#endif

// Ping period in ms. Every reply is one CSI frame, so this is the sample rate.
#ifndef CSI_PING_INTERVAL_MS
#define CSI_PING_INTERVAL_MS 40
#endif
#ifndef CSI_PUBLISH_MS
#define CSI_PUBLISH_MS 500
#endif
#ifndef CSI_WINDOW_MS
#define CSI_WINDOW_MS 1000
#endif
#ifndef CSI_MIN_GAP_MS
#define CSI_MIN_GAP_MS 12
#endif
#ifndef CSI_RING
#define CSI_RING 64
#endif
#ifndef CSI_STILL_N
#define CSI_STILL_N 30
#endif
// Never run when the heap is tight; restart only with 8 kB of headroom.
#ifndef CSI_MIN_FREE_HEAP
#define CSI_MIN_FREE_HEAP 32000
#endif

namespace csipresence
{
namespace
{
constexpr int GROUPS = 8;
// L-LTF: bytes 38..63 are -26..-1 and bytes 2..26 are 2..26, 51 subcarriers in
// frequency order. Slots 0/1 are skipped (first_word_invalid may blank them).
constexpr int USABLE = 51;
constexpr uint32_t RETRY_MS = 5000;

// m is the frame mean, gn[] the group means normalised by it.
struct Sample { uint32_t t; float m, gn[GROUPS]; int8_t rssi; };
struct StillRow { float gn[GROUPS]; }; // one row per second

CsiPublishFn g_publish = nullptr;
SemaphoreHandle_t g_mux = nullptr;
char g_node[24], g_topic[48], g_cmdTopic[48];
const char *g_rr = "UNKNOWN";
volatile bool g_running = false, g_paused = false, g_otaHold = false, g_enabled = true;
bool g_crash = false, g_heapOk = true, g_bssidSet = false; // crash: off for this boot
uint8_t g_bssid[6];
uint32_t g_lastCsi = 0, g_lastTry = 0, g_lastPub = 0, g_pubCount = 0, g_pingTarget = 0;
esp_ping_handle_t g_ping = nullptr;
esp_timer_handle_t g_guard = nullptr;
Sample g_ring[CSI_RING];
int g_head = 0, g_count = 0;
StillRow g_still[CSI_STILL_N];
int g_stillHead = 0, g_stillCount = 0;

const struct { esp_reset_reason_t r; const char *n; } RESETS[] = {
    {ESP_RST_POWERON, "POWERON"}, {ESP_RST_EXT, "EXT"}, {ESP_RST_SW, "SW"},
    {ESP_RST_PANIC, "PANIC"}, {ESP_RST_INT_WDT, "INT_WDT"}, {ESP_RST_TASK_WDT, "TASK_WDT"},
    {ESP_RST_WDT, "WDT"}, {ESP_RST_DEEPSLEEP, "DEEPSLEEP"}, {ESP_RST_BROWNOUT, "BROWNOUT"},
    {ESP_RST_SDIO, "SDIO"}};

const char *resetName(esp_reset_reason_t r)
{
    for (const auto &e : RESETS) if (e.r == r) return e.n;
    return "UNKNOWN";
}

// %.1f prints "nan" / "inf" for those, which no JSON parser takes.
float finiteOr(float v, float fallback) { return isfinite(v) ? v : fallback; }

// One subcarrier slot: buf holds [imag, real] as two int8; p is 0..USABLE-1.
void accum(const int8_t *buf, int i, int p, float *ga, uint8_t *cnt, float &sum)
{
    const float im = buf[2 * i], re = buf[2 * i + 1], a = sqrtf(re * re + im * im);
    const int g = (p * GROUPS) / USABLE;
    ga[g] += a, cnt[g]++, sum += a;
}

// WiFi task context: fast only, no allocation, no Serial, no MQTT, no delay.
void csiCb(void *, wifi_csi_info_t *info)
{
    if (!g_running || info == nullptr || info->buf == nullptr || info->len < 128) return;
    if (g_bssidSet && memcmp(info->mac, g_bssid, 6) != 0) return;
    const uint32_t now = millis();
    if (g_lastCsi && now - g_lastCsi < CSI_MIN_GAP_MS) return;

    float sum = 0, ga[GROUPS] = {0};
    uint8_t cnt[GROUPS] = {0};
    int p = 0;
    for (int i = 38; i <= 63; i++, p++) accum(info->buf, i, p, ga, cnt, sum);
    for (int i = 2; i <= 26; i++, p++) accum(info->buf, i, p, ga, cnt, sum);
    const float m = sum / (float)USABLE;
    if (m < 1.0f) return; // too weak to say anything about the channel

    Sample s;
    s.t = now, s.m = m, s.rssi = info->rx_ctrl.rssi;
    for (int g = 0; g < GROUPS; g++)
        s.gn[g] = (ga[g] / (float)cnt[g]) / m; // cancels AGC and tx power

    // Never wait here: a dropped sample beats a stall in the WiFi task.
    if (xSemaphoreTake(g_mux, 0) != pdTRUE) return;
    g_ring[g_head] = s;
    g_head = (g_head + 1) % CSI_RING;
    if (g_count < CSI_RING) g_count++;
    g_lastCsi = now;
    xSemaphoreGive(g_mux);
}

// esp_timer context. ArduinoOTA blocks loop() while it writes flash, so this is
// the only place that can still stop CSI during a transfer.
void guardCb(void *)
{
#if CSI_HAVE_UPDATE
    if (!g_running || !Update.isRunning()) return;
    g_running = false;
    esp_wifi_set_csi(false);
    if (g_ping) esp_ping_stop(g_ping);
    g_otaHold = true;
#endif
}

void stopCsi(const char *why)
{
    g_running = false, esp_wifi_set_csi(false);
    if (g_ping) esp_ping_stop(g_ping);
    Serial.printf("[CSI] stopped (%s)\n", why);
}

bool startFailed(const char *msg)
{
    Serial.printf("[CSI] %s\n", msg);
    esp_wifi_set_csi(false);
    return false;
}

bool startCsi()
{
    // Only frames from our own AP carry the channel we care about.
    uint8_t *bssid = WiFi.BSSID();
    g_bssidSet = bssid != nullptr;
    if (bssid) memcpy(g_bssid, bssid, 6);

    wifi_csi_config_t cfg = {};
    cfg.lltf_en = true; // LLTF only: 51 usable subcarriers, no HT
    cfg.htltf_en = cfg.stbc_htltf2_en = cfg.ltf_merge_en = false;
    cfg.channel_filter_en = false; // keep adjacent subcarriers independent
    cfg.manu_scale = false;        // AGC scaling is fine, gn cancels it
    esp_wifi_set_csi_config(&cfg), esp_wifi_set_csi_rx_cb(csiCb, nullptr);
    if (esp_wifi_set_csi(true) != ESP_OK)
        return startFailed("esp_wifi_set_csi failed, is CONFIG_ESP_WIFI_CSI_ENABLED set?");

    // Pings make the AP answer; the replies (and any other frame the AP sends us) are the CSI frames.
    const IPAddress gw = WiFi.gatewayIP();
    const uint32_t target = (uint32_t)gw;
    if (g_ping && g_pingTarget != target) // DHCP moved us
    {
        esp_ping_stop(g_ping), esp_ping_delete_session(g_ping);
        g_ping = nullptr;
    }
    if (g_ping == nullptr)
    {
        esp_ping_config_t ping = ESP_PING_DEFAULT_CONFIG();
        ping.count = ESP_PING_COUNT_INFINITE, ping.interval_ms = CSI_PING_INTERVAL_MS;
        ping.timeout_ms = 500, ping.data_size = 8;
        ping.task_stack_size = 2560, ping.task_prio = 2;
        IP_ADDR4(&ping.target_addr, gw[0], gw[1], gw[2], gw[3]);
        esp_ping_callbacks_t cbs = {}; // no callbacks needed
        if (esp_ping_new_session(&ping, &cbs, &g_ping) != ESP_OK)
            return startFailed("ping session failed");
        g_pingTarget = target;
    }
    if (esp_ping_start(g_ping) != ESP_OK)
    {
        // A session that cannot be restarted is rebuilt on the next try.
        esp_ping_delete_session(g_ping);
        g_ping = nullptr;
        return startFailed("ping start failed");
    }

    xSemaphoreTake(g_mux, portMAX_DELAY);
    g_head = g_count = 0;
    g_stillHead = g_stillCount = g_pubCount = 0;
    xSemaphoreGive(g_mux);
    g_running = true;
    return true;
}

void publish()
{
    uint32_t now = millis();
    int n = 0, stillN = 0, gstd[GROUPS] = {0};
    float mean = 0, mstd = 0, motion = 0, rssiMean = 0, rssiSd = 0, still = -1.0f;

    if (xSemaphoreTake(g_mux, portMAX_DELAY) == pdTRUE)
    {
        now = millis(); // after the take: a sample stored meanwhile must not be "in the future"
        float sm = 0, sm2 = 0, sr = 0, sr2 = 0, sg[GROUPS] = {0}, sg2[GROUPS] = {0};
        for (int k = 0; k < g_count; k++) // newest first, stop at the first stale
        {
            const Sample &s = g_ring[(g_head - 1 - k + 2 * CSI_RING) % CSI_RING];
            if ((int32_t)(now - s.t) > (int32_t)CSI_WINDOW_MS) break;
            n++, sm += s.m, sm2 += s.m * s.m, sr += s.rssi, sr2 += (float)s.rssi * s.rssi;
            for (int g = 0; g < GROUPS; g++) sg[g] += s.gn[g], sg2[g] += s.gn[g] * s.gn[g];
        }
        if (n > 0)
        {
            mean = sm / n, rssiMean = sr / n;
            const float vm = sm2 / n - mean * mean, vr = sr2 / n - rssiMean * rssiMean;
            mstd = mean > 0 ? sqrtf(vm > 0 ? vm : 0) / mean * 1000.0f : 0;
            rssiSd = sqrtf(vr > 0 ? vr : 0);
        }
        if (n >= 8)
        {
            float tot = 0;
            for (int g = 0; g < GROUPS; g++)
            {
                const float gm = sg[g] / n, v = sg2[g] / n - gm * gm;
                gstd[g] = (int)lroundf(sqrtf(v > 0 ? v : 0) * 1000.0f);
                tot += gstd[g];
            }
            motion = tot / GROUPS;
            // Someone sitting still barely moves the pattern; only slow drift is
            // left, so keep one row per second and watch its spread.
            if (g_running && (++g_pubCount % 2) == 0)
            {
                for (int g = 0; g < GROUPS; g++) g_still[g_stillHead].gn[g] = sg[g] / n;
                g_stillHead = (g_stillHead + 1) % CSI_STILL_N;
                if (g_stillCount < CSI_STILL_N) g_stillCount++;
            }
        }
        stillN = g_stillCount;
        still = stillN >= 10 ? 0 : -1.0f;
        for (int g = 0; g < GROUPS && stillN >= 10; g++)
        {
            float m1 = 0, m2 = 0;
            for (int k = 0; k < stillN; k++)
                m1 += g_still[k].gn[g], m2 += g_still[k].gn[g] * g_still[k].gn[g];
            m1 /= stillN;
            const float v = m2 / stillN - m1 * m1;
            still += sqrtf(v > 0 ? v : 0) / GROUPS * 1000.0f;
        }
        xSemaphoreGive(g_mux);
    }

    const int ok = (g_running && n >= 8) ? 1 : 0;
    if (!ok)
        mean = mstd = motion = rssiMean = rssiSd = 0, still = -1.0f, memset(gstd, 0, sizeof(gstd));
    char buf[384];
    int len = snprintf(buf, sizeof(buf),
                       "{\"on\":%d,\"ok\":%d,\"n\":%d,\"mean\":%.1f,\"motion\":%.1f,\"mstd\":%.1f,"
                       "\"still\":%.1f,\"still_n\":%d,\"gstd\":[",
                       g_running ? 1 : 0, ok, n, finiteOr(mean, 0), finiteOr(motion, 0), finiteOr(mstd, 0),
                       finiteOr(still, -1), stillN);
    for (int g = 0; g < GROUPS && len > 0 && len < (int)sizeof(buf); g++)
        len += snprintf(buf + len, sizeof(buf) - len, "%s%d", g ? "," : "", gstd[g]);
    if (len < 0 || len >= (int)sizeof(buf)) return; // never publish broken JSON
    snprintf(buf + len, sizeof(buf) - len,
             "],\"rssi\":%.1f,\"rssi_sd\":%.1f,\"heap\":%u,\"up\":%lu,\"rr\":\"%s\"}",
             finiteOr(rssiMean, 0), finiteOr(rssiSd, 0), (unsigned)ESP.getFreeHeap(),
             (unsigned long)(now / 1000), g_rr);
    g_publish(g_topic, buf, false);
}
} // namespace

void setup(const char *node, CsiPublishFn publish)
{
    g_publish = publish;
    snprintf(g_node, sizeof(g_node), "%s", node ? node : "node");
    snprintf(g_topic, sizeof(g_topic), "presence/%s/csi", g_node);
    snprintf(g_cmdTopic, sizeof(g_cmdTopic), "presence/%s/cmd", g_node);
    if (g_mux == nullptr) g_mux = xSemaphoreCreateMutex();

    const esp_reset_reason_t why = esp_reset_reason();
    g_rr = resetName(why);
    // Never let CSI put the board in a boot loop: after a panic or watchdog
    // reset the module stays off until the next clean boot.
    g_crash = why == ESP_RST_PANIC || why == ESP_RST_TASK_WDT || why == ESP_RST_INT_WDT || why == ESP_RST_WDT;
    if (g_crash) Serial.printf("[CSI] previous reset was a crash, disabled this boot\n");
    if (g_guard == nullptr)
    {
        esp_timer_create_args_t args = {};
        args.callback = guardCb, args.name = "csi_guard";
        if (esp_timer_create(&args, &g_guard) == ESP_OK)
            esp_timer_start_periodic(g_guard, 250 * 1000);
    }
    Serial.printf("[CSI] node %s, reset %s, %s\n", g_node, g_rr, g_crash ? "disabled" : "armed");
}

void loop()
{
    if (g_publish == nullptr) return;
    const uint32_t now = millis();

#if CSI_HAVE_UPDATE
    if (g_otaHold && !Update.isRunning()) g_otaHold = false, Serial.printf("[CSI] OTA finished\n");
#endif

    // Stop below CSI_MIN_FREE_HEAP, restart only above it plus 8 kB: no flap.
    const uint32_t heap = ESP.getFreeHeap();
    if (heap < CSI_MIN_FREE_HEAP) g_heapOk = false;
    else if (heap > CSI_MIN_FREE_HEAP + 8000) g_heapOk = true;

    const IPAddress gw = WiFi.gatewayIP();
    const bool linked = WiFi.status() == WL_CONNECTED;
    const bool desired = !g_crash && g_enabled && !g_paused && !g_otaHold && linked &&
                         (uint32_t)gw != 0 && g_heapOk;
    if (desired && !g_running)
    {
        if (g_lastTry == 0 || now - g_lastTry >= RETRY_MS)
            g_lastTry = now, startCsi() &&
                Serial.printf("[CSI] running, %d subcarriers, ping %d ms\n", USABLE, CSI_PING_INTERVAL_MS);
    }
    else if (!desired && g_running)
        stopCsi(g_crash ? "crash" : g_otaHold ? "ota" : !g_enabled ? "off" : g_paused ? "paused"
                : !linked ? "wifi" : (uint32_t)gw == 0 ? "gateway" : "heap");
    else if (g_running && (uint32_t)gw != g_pingTarget)
        stopCsi("gateway");

    // While stopped a slow status keeps Home Assistant aware of the state.
    if (now - g_lastPub < (uint32_t)(g_running ? CSI_PUBLISH_MS : 10000)) return;
    g_lastPub = now;
    publish();
}

void subscribe(void (*sub)(const char *topic))
{
    if (sub && g_cmdTopic[0]) sub(g_cmdTopic);
}

bool onMessage(const char *topic, const uint8_t *payload, unsigned int length)
{
    if (topic == nullptr || g_cmdTopic[0] == 0 || strcmp(topic, g_cmdTopic) != 0) return false;
    String cmd;
    for (unsigned int i = 0; i < length; i++) cmd += (char)payload[i];
    cmd.trim();
    cmd.toLowerCase();
    // A BLE module shares this topic but ignores non-lightbar payloads, so
    // "csi_" payloads are ours even when malformed.
    if (!cmd.startsWith("csi_")) return false;
    if (cmd == "csi_on") g_enabled = true;
    else if (cmd == "csi_off") g_enabled = false;
    return true;
}

void setPaused(bool p) { g_paused = p; }

bool running() { return g_running; }
} // namespace csipresence
