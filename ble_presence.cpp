#include "ble_presence.h"

#include <NimBLEDevice.h>
#include <mbedtls/aes.h>
#include <string.h>
#include <math.h>
#include <esp_system.h>

#ifndef BLE_PUBLISH_FN_API
#include "mqtt.h"
#endif

// Node name used in topics: presence/<BLE_NODE_NAME>/... Override in config.h.
#ifndef BLE_NODE_NAME
#define BLE_NODE_NAME "pcdesk"
#endif
#ifndef BLE_PUBLISH_MS
#define BLE_PUBLISH_MS 3000
#endif
// Scan duty ~30 %: interval 100 ms, window 30 ms (0.625 ms units). Low enough
// that the WiFi/MQTT link and the nRF24 polling keep working.
#ifndef BLE_SCAN_INTERVAL
#define BLE_SCAN_INTERVAL 160
#endif
#ifndef BLE_SCAN_WINDOW
#define BLE_SCAN_WINDOW 48
#endif
// Stationary-device filter (laptops, tablets that never move): an Apple advert whose
// address keeps the same RSSI band (30 s window means within STAT_SPREAD_DB of each other)
// for STAT_MIN_AGE_MS is "stationary" and ignored for the fallback signal. The band is
// remembered, so the next rotated address of the same device is ignored at once.
#ifndef STAT_WINDOW_MS
#define STAT_WINDOW_MS 30000
#endif
#ifndef STAT_SPREAD_DB
#define STAT_SPREAD_DB 6
#endif
#ifndef STAT_MIN_AGE_MS
#define STAT_MIN_AGE_MS 300000
#endif
#ifndef STAT_BAND_DB
#define STAT_BAND_DB 3
#endif
#ifndef STAT_BAND_TTL_MS
#define STAT_BAND_TTL_MS 10800000UL
#endif
// Never scan when the heap gets tight; WiFi + MQTT matter more.
#ifndef BLE_MIN_FREE_HEAP
#define BLE_MIN_FREE_HEAP 40000
#endif

namespace blepresence
{
namespace
{
constexpr int MAX_IRK = 4;
constexpr int CACHE_SIZE = 24;
constexpr int TRACKS = 48;
constexpr int BANDS = 8;
constexpr int STAT_MIN_WINDOWS = 8;

#ifdef BLE_PUBLISH_FN_API
BlePublishFn g_publish = nullptr;
#else
MQTT *g_mqtt = nullptr;
#endif
// A FreeRTOS mutex, not a spinlock: the AES in resolveAddr() takes the hardware
// AES lock and must not run inside a critical section.
SemaphoreHandle_t g_mux = nullptr; // created in setup()
#define LOCK() do { if (g_mux) xSemaphoreTake(g_mux, portMAX_DELAY); } while (0)
#define UNLOCK() do { if (g_mux) xSemaphoreGive(g_mux); } while (0)
bool g_enabled = true;
bool g_started = false;
uint32_t g_lastPublish = 0;

// IRK list (set from MQTT). Each IRK is stored in both byte orders because the
// order in which the owner pastes it is not known up front.
uint8_t g_irk[MAX_IRK][2][16];
char g_irkId[MAX_IRK][9]; // "phone", "watch", ...
int g_irkCount = 0;

// Address -> resolution cache (RPAs rotate every ~15 min).
struct CacheEntry
{
    uint8_t addr[6];
    int8_t irk; // -1 = no match, >= 0 = IRK index
    uint32_t last; // 0 = empty slot
};
CacheEntry g_cache[CACHE_SIZE];
int g_cacheNext = 0;

struct Agg
{
    int32_t sum = 0;
    int16_t best = -127;
    uint16_t n = 0;
    void add(int rssi)
    {
        sum += rssi;
        if (rssi > best)
            best = rssi;
        n++;
    }
};
Agg g_apple; // every Apple advert
Agg g_mov;   // only those not judged stationary
Agg g_irkAgg[MAX_IRK];

struct Track
{
    uint8_t addr[6];
    bool used;
    bool moving; // sticky: window means spread more than STAT_SPREAD_DB
    bool stat;   // steady for STAT_MIN_AGE_MS
    bool prov;   // matches the band of a known stationary device
    uint32_t first, last, winStart;
    int32_t wSum;
    uint16_t wN;
    uint16_t nWin;
    float wMin, wMax; // min / max of closed-window means
};
Track g_tracks[TRACKS];
struct Band
{
    float center;
    uint32_t last; // 0 = empty
};
Band g_bands[BANDS];

bool bandMatches(float cur, uint32_t now)
{
    for (int i = 0; i < BANDS; i++)
    {
        if (!g_bands[i].last)
            continue;
        if (now - g_bands[i].last > STAT_BAND_TTL_MS)
        {
            g_bands[i].last = 0;
            continue;
        }
        if (fabsf(g_bands[i].center - cur) <= STAT_BAND_DB)
            return true;
    }
    return false;
}

void addBand(float center, uint32_t now)
{
    int slot = 0;
    for (int i = 0; i < BANDS; i++)
    {
        if (g_bands[i].last && now - g_bands[i].last > STAT_BAND_TTL_MS)
            g_bands[i].last = 0; // expired
        if (g_bands[i].last && fabsf(g_bands[i].center - center) <= STAT_BAND_DB)
        {
            g_bands[i].center = 0.7f * g_bands[i].center + 0.3f * center;
            g_bands[i].last = now;
            return;
        }
        if (!g_bands[i].last || (g_bands[slot].last && now - g_bands[i].last > now - g_bands[slot].last))
            slot = i;
    }
    g_bands[slot].center = center;
    g_bands[slot].last = now;
}

// Caller holds the mutex. Returns true when this advert is from a stationary device.
bool observeStationary(const uint8_t *addr, int rssi, uint32_t now)
{
    now |= 1;
    Track *t = nullptr;
    Track *victim = &g_tracks[0];
    for (int i = 0; i < TRACKS; i++)
    {
        if (g_tracks[i].used && !memcmp(g_tracks[i].addr, addr, 6))
        {
            t = &g_tracks[i];
            break;
        }
        if (!g_tracks[i].used || (victim->used && now - g_tracks[i].last > now - victim->last))
            victim = &g_tracks[i];
    }
    if (!t)
    {
        t = victim;
        memset(t, 0, sizeof(*t));
        memcpy(t->addr, addr, 6);
        t->used = true;
        t->first = t->last = t->winStart = now;
    }
    if (now - t->winStart >= STAT_WINDOW_MS)
    {
        if (t->wN >= 3)
        {
            const float avg = (float)t->wSum / t->wN;
            if (t->nWin == 0 || avg < t->wMin)
                t->wMin = avg;
            if (t->nWin == 0 || avg > t->wMax)
                t->wMax = avg;
            t->nWin++;
        }
        t->winStart = now;
        t->wSum = 0;
        t->wN = 0;
        if (t->nWin >= 1)
        {
            if (t->wMax - t->wMin > STAT_SPREAD_DB)
                { t->prov = t->stat = false; t->moving = true; }
            else if (!t->moving && now - t->first >= STAT_MIN_AGE_MS && t->nWin >= STAT_MIN_WINDOWS)
            {
                t->stat = true;
                addBand((t->wMin + t->wMax) / 2.0f, now);
            }
        }
    }
    t->wSum += rssi;
    t->wN++;
    t->last = now;
    if (!t->moving && !t->stat && (t->wN >= 4 || t->nWin >= 1))
        t->prov = bandMatches((float)t->wSum / t->wN, now);
    return (t->stat || t->prov) && !t->moving;
}

// Caller holds the mutex.
int staticCount(uint32_t now)
{
    int n = 0;
    for (int i = 0; i < TRACKS; i++)
        if (g_tracks[i].used && g_tracks[i].stat && !g_tracks[i].moving && now - g_tracks[i].last <= 60000)
            n++;
    return n;
}

// Bluetooth ah(): AES-128(k, 0^104 || prand), low 24 bits compared to hash.
// addrLE is NimBLE's little-endian address (addrLE[5] is the MSB).
bool resolves(const uint8_t *key, const uint8_t *addrLE)
{
    uint8_t in[16] = {0};
    in[13] = addrLE[5];
    in[14] = addrLE[4];
    in[15] = addrLE[3];
    uint8_t out[16];
    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    mbedtls_aes_setkey_enc(&ctx, key, 128);
    mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_ENCRYPT, in, out);
    mbedtls_aes_free(&ctx);
    return out[13] == addrLE[2] && out[14] == addrLE[1] && out[15] == addrLE[0];
}

// Caller holds the mutex.
int resolveAddr(const uint8_t *addrLE)
{
    // Only resolvable private addresses: top two bits of the MSB are 01.
    if ((addrLE[5] & 0xC0) != 0x40)
        return -1;
    uint32_t now = millis() | 1;
    for (int i = 0; i < CACHE_SIZE; i++)
    {
        if (g_cache[i].last && !memcmp(g_cache[i].addr, addrLE, 6))
        {
            g_cache[i].last = now;
            return g_cache[i].irk;
        }
    }
    int found = -1;
    for (int k = 0; k < g_irkCount && found < 0; k++)
        for (int o = 0; o < 2; o++)
            if (resolves(g_irk[k][o], addrLE))
            {
                found = k;
                break;
            }
    CacheEntry &e = g_cache[g_cacheNext];
    g_cacheNext = (g_cacheNext + 1) % CACHE_SIZE;
    memcpy(e.addr, addrLE, 6);
    e.irk = found;
    e.last = now;
    return found;
}

class ScanCb : public NimBLEScanCallbacks
{
    void onResult(const NimBLEAdvertisedDevice *dev) override
    {
        const std::string md = dev->getManufacturerData();
        const int rssi = dev->getRSSI();
        const uint8_t *a = dev->getAddress().getVal();
        const bool apple = md.size() >= 3 && (uint8_t)md[0] == 0x4C && (uint8_t)md[1] == 0x00;
        LOCK();
        const int irk = g_irkCount ? resolveAddr(a) : -1;
        if (irk >= 0)
            g_irkAgg[irk].add(rssi);
        // Continuity types: 0x10 Nearby Info, 0x0C Handoff, 0x0F Nearby Action.
        if (apple)
        {
            const uint8_t t = (uint8_t)md[2];
            if (t == 0x10 || t == 0x0C || t == 0x0F)
            {
                g_apple.add(rssi);
                // IRK-resolved devices (phone, watch) have their own irk[] entry and
                // stay out of the mov_* fallback aggregate.
                if (irk < 0 && !observeStationary(a, rssi, millis()))
                    g_mov.add(rssi);
            }
        }
        UNLOCK();
    }
};
ScanCb g_cb;

String topicBase() { return String("presence/") + BLE_NODE_NAME; }

void startScan()
{
    NimBLEScan *scan = NimBLEDevice::getScan();
    scan->setScanCallbacks(&g_cb, true); // true: want duplicates (every RSSI sample)
    scan->setActiveScan(false);
    scan->setInterval(BLE_SCAN_INTERVAL);
    scan->setWindow(BLE_SCAN_WINDOW);
    scan->setMaxResults(0); // do not store devices
    scan->start(0, false);   // 0 = until stopped
}

int hexNib(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Extracts the first 32 hex nibbles of s[0..len) into out; "0x" prefixes and any
// other non-hex characters are separators. Returns false when there are fewer than 32.
bool parseKey(const char *s, unsigned int len, uint8_t *out)
{
    int n = 0;
    for (unsigned int i = 0; i < len; i++)
    {
        if (s[i] == '0' && i + 1 < len && (s[i + 1] == 'x' || s[i + 1] == 'X') && n % 2 == 0)
        {
            i++; // skip the "0x" prefix
            continue;
        }
        const int v = hexNib(s[i]);
        if (v < 0)
            continue;
        if (n % 2 == 0)
            out[n / 2] = v << 4;
        else
            out[n / 2] |= v;
        if (++n == 32)
            return true;
    }
    return false;
}

// Reads a quoted string at s[i] (s[i] == '"'); returns the index after the closing quote
// and sets start/slen, or -1.
int readQuoted(const char *s, unsigned int len, unsigned int i, unsigned int *start, unsigned int *slen)
{
    if (i >= len || s[i] != '"')
        return -1;
    unsigned int j = i + 1;
    while (j < len && s[j] != '"')
        j++;
    if (j >= len)
        return -1;
    *start = i + 1;
    *slen = j - i - 1;
    return j + 1;
}

// Loads IRKs. Labelled form: {"phone":"<hex>","watch":"<hex>"}. Plain form: hex keys in
// any separator style; the first is "phone", the others "irk2".."irk4" (old format).
void parseIrks(const char *s, unsigned int len)
{
    uint8_t tmp[MAX_IRK][16];
    char ids[MAX_IRK][9];
    int count = 0;
    unsigned int p = 0;
    while (p < len && (s[p] == ' ' || s[p] == '\n' || s[p] == '\r' || s[p] == '\t'))
        p++;
    if (p < len && s[p] == '{')
    {
        unsigned int i = p + 1;
        while (i < len && count < MAX_IRK)
        {
            while (i < len && s[i] != '"')
                i++;
            unsigned int ks, kl, vs, vl;
            int a = readQuoted(s, len, i, &ks, &kl);
            if (a < 0)
                break;
            while ((unsigned)a < len && (s[a] == ' ' || s[a] == ':'))
                a++;
            int b = readQuoted(s, len, a, &vs, &vl);
            if (b < 0)
                break;
            i = b;
            if (parseKey(s + vs, vl, tmp[count]))
            {
                unsigned int m = kl < 8 ? kl : 8;
                memcpy(ids[count], s + ks, m);
                ids[count][m] = 0;
                // The label goes into JSON unescaped: keep it to [A-Za-z0-9_-].
                for (unsigned int q = 0; q < m; q++)
                {
                    const char c = ids[count][q];
                    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-'))
                        ids[count][q] = '_';
                }
                count++;
            }
        }
    }
    else
    {
        // Plain hex: every 32 nibbles make one key.
        unsigned int i = 0;
        while (i < len && count < MAX_IRK)
        {
            unsigned int j = i, n = 0;
            while (j < len && n < 32)
            {
                if (s[j] == '0' && j + 1 < len && (s[j + 1] == 'x' || s[j + 1] == 'X') && n % 2 == 0)
                    j++;
                else if (hexNib(s[j]) >= 0)
                    n++;
                j++;
            }
            if (n < 32 || !parseKey(s + i, j - i, tmp[count]))
                break;
            if (count == 0)
                strcpy(ids[count], "phone");
            else
                snprintf(ids[count], sizeof(ids[count]), "irk%d", count + 1);
            count++;
            i = j;
        }
    }
    LOCK();
    g_irkCount = count;
    for (int k = 0; k < count; k++)
    {
        memcpy(g_irk[k][0], tmp[k], 16);
        for (int j = 0; j < 16; j++)
            g_irk[k][1][j] = tmp[k][15 - j];
        memcpy(g_irkId[k], ids[k], 9);
    }
    memset(g_cache, 0, sizeof(g_cache));
    for (int k = 0; k < MAX_IRK; k++)
        g_irkAgg[k] = Agg();
    UNLOCK();
    Serial.printf("[BLE] %d IRK(s) loaded\n", count);
}
} // namespace

#ifdef BLE_PUBLISH_FN_API
void setup(BlePublishFn publish)
{
    g_publish = publish;
#else
void setup(MQTT *mqtt)
{
    g_mqtt = mqtt;
#endif
    g_mux = xSemaphoreCreateMutex();
    // Never let the BLE controller put the board in a boot loop: after a panic or
    // watchdog reset the scanner stays off until the next clean boot.
    const esp_reset_reason_t why = esp_reset_reason();
    if (why == ESP_RST_PANIC || why == ESP_RST_TASK_WDT || why == ESP_RST_INT_WDT || why == ESP_RST_WDT)
    {
        Serial.println("[BLE] previous reset was a crash, scanner disabled this boot");
        return;
    }
    if (ESP.getFreeHeap() < BLE_MIN_FREE_HEAP + 30000)
    {
        Serial.println("[BLE] not enough heap, scanner disabled");
        return;
    }
    NimBLEDevice::init("");
    g_started = true;
    if (g_enabled)
        startScan();
    Serial.printf("[BLE] scanner up, free heap %u\n", ESP.getFreeHeap());
}

void subscribe(void (*sub)(const char *topic))
{
    sub("presence/config/irk");
    sub((topicBase() + "/cmd").c_str());
}

bool onMessage(const char *topic, const uint8_t *payload, unsigned int length)
{
    if (strncmp(topic, "presence/", 9) != 0)
        return false;
    if (!strcmp(topic, "presence/config/irk"))
    {
        parseIrks((const char *)payload, length);
        return true;
    }
    if (String(topic) == topicBase() + "/cmd")
    {
        String cmd;
        for (unsigned int i = 0; i < length; i++)
            cmd += (char)payload[i];
        cmd.trim();
        cmd.toLowerCase();
        if (cmd == "off" || cmd == "scan_off")
        {
            g_enabled = false;
            if (g_started)
                NimBLEDevice::getScan()->stop();
        }
        else if (cmd == "on" || cmd == "scan_on")
        {
            g_enabled = true;
            if (g_started && !NimBLEDevice::getScan()->isScanning())
                startScan();
        }
        return true;
    }
    return true; // other presence/ topics are never light bar commands
}

void loop()
{
    if (!g_started)
        return;
    const uint32_t now = millis();
    if (now - g_lastPublish < BLE_PUBLISH_MS)
        return;
    g_lastPublish = now;

    // Keep the scan alive, and drop it when memory is short.
    NimBLEScan *scan = NimBLEDevice::getScan();
    // Stop below BLE_MIN_FREE_HEAP, restart only 10 kB above it (no flapping).
    const uint32_t freeHeap = ESP.getFreeHeap();
    if (g_enabled && freeHeap < BLE_MIN_FREE_HEAP)
    {
        if (scan->isScanning())
            scan->stop();
    }
    else if (g_enabled && !scan->isScanning() && freeHeap > BLE_MIN_FREE_HEAP + 10000)
        startScan();

    Agg apple, mov;
    Agg irk[MAX_IRK];
    char ids[MAX_IRK][9];
    int irkCount, staticN;
    LOCK();
    apple = g_apple;
    g_apple = Agg();
    mov = g_mov;
    g_mov = Agg();
    staticN = staticCount(millis() | 1);
    memcpy(ids, g_irkId, sizeof(ids));
    irkCount = g_irkCount;
    for (int k = 0; k < MAX_IRK; k++)
    {
        irk[k] = g_irkAgg[k];
        g_irkAgg[k] = Agg();
    }
    UNLOCK();

    char buf[512];
    int len = snprintf(buf, sizeof(buf),
                       "{\"scan\":%d,\"apple_best\":%d,\"apple_avg\":%d,\"apple_n\":%u,"
                       "\"mov_best\":%d,\"mov_avg\":%d,\"mov_n\":%u,\"static_n\":%d,\"irk_n\":%d,\"irk\":[",
                       (g_enabled && scan->isScanning()) ? 1 : 0,
                       apple.n ? apple.best : -127,
                       apple.n ? (int)(apple.sum / apple.n) : -127,
                       apple.n,
                       mov.n ? mov.best : -127,
                       mov.n ? (int)(mov.sum / mov.n) : -127,
                       mov.n, staticN, irkCount);
    for (int k = 0; k < irkCount && len < (int)sizeof(buf) - 90; k++)
        len += snprintf(buf + len, sizeof(buf) - len, "%s{\"id\":\"%s\",\"best\":%d,\"avg\":%d,\"n\":%u}", k ? "," : "",
                        ids[k], irk[k].n ? irk[k].best : -127, irk[k].n ? (int)(irk[k].sum / irk[k].n) : -127, irk[k].n);
    snprintf(buf + len, sizeof(buf) - len, "],\"heap\":%u,\"up\":%lu}", ESP.getFreeHeap(), (unsigned long)(now / 1000));
#ifdef BLE_PUBLISH_FN_API
    g_publish((topicBase() + "/apple").c_str(), buf, false);
#else
    g_mqtt->publish((topicBase() + "/apple").c_str(), buf, false);
#endif
}
} // namespace blepresence
