#include "ble_presence.h"

#include <NimBLEDevice.h>
#include <mbedtls/aes.h>
#include <string.h>
#include <esp_system.h>

#include "mqtt.h"

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

MQTT *g_mqtt = nullptr;
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
Agg g_apple;
Agg g_irkAgg[MAX_IRK];

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
                g_apple.add(rssi);
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

// Parses IRKs: every 32 hex nibbles make one key; any other character (comma,
// space, colon, newline) is a separator and "0x" prefixes are ignored.
void parseIrks(const char *s, unsigned int len)
{
    uint8_t tmp[MAX_IRK][16];
    int count = 0;
    int n = 0;
    uint8_t b[16];
    for (unsigned int i = 0; i < len && count < MAX_IRK; i++)
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
            b[n / 2] = v << 4;
        else
            b[n / 2] |= v;
        if (++n == 32)
        {
            memcpy(tmp[count++], b, 16);
            n = 0;
        }
    }
    LOCK();
    g_irkCount = count;
    for (int k = 0; k < count; k++)
    {
        memcpy(g_irk[k][0], tmp[k], 16);
        for (int j = 0; j < 16; j++)
            g_irk[k][1][j] = tmp[k][15 - j];
    }
    memset(g_cache, 0, sizeof(g_cache));
    for (int k = 0; k < MAX_IRK; k++)
        g_irkAgg[k] = Agg();
    UNLOCK();
    Serial.printf("[BLE] %d IRK(s) loaded\n", count);
}
} // namespace

void setup(MQTT *mqtt)
{
    g_mqtt = mqtt;
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

    Agg apple;
    Agg irk[MAX_IRK];
    int irkCount;
    LOCK();
    apple = g_apple;
    g_apple = Agg();
    irkCount = g_irkCount;
    for (int k = 0; k < MAX_IRK; k++)
    {
        irk[k] = g_irkAgg[k];
        g_irkAgg[k] = Agg();
    }
    UNLOCK();

    char buf[256];
    int len = snprintf(buf, sizeof(buf),
                       "{\"scan\":%d,\"apple_best\":%d,\"apple_avg\":%d,\"apple_n\":%u,\"irk_n\":%d,\"irk\":[",
                       (g_enabled && scan->isScanning()) ? 1 : 0,
                       apple.n ? apple.best : -127,
                       apple.n ? (int)(apple.sum / apple.n) : -127,
                       apple.n, irkCount);
    for (int k = 0; k < irkCount && len < (int)sizeof(buf) - 60; k++)
        len += snprintf(buf + len, sizeof(buf) - len, "%s{\"best\":%d,\"avg\":%d,\"n\":%u}", k ? "," : "",
                        irk[k].n ? irk[k].best : -127, irk[k].n ? (int)(irk[k].sum / irk[k].n) : -127, irk[k].n);
    snprintf(buf + len, sizeof(buf) - len, "],\"heap\":%u,\"up\":%lu}", ESP.getFreeHeap(), (unsigned long)(now / 1000));
    g_mqtt->publish((topicBase() + "/apple").c_str(), buf, false);
}
} // namespace blepresence
