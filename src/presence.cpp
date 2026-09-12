#include "presence.h"

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <Preferences.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nimble/nimble/host/include/host/ble_store.h"
#include "mbedtls/aes.h"

#include "batmon/batmon_client.h"
#include "config.h"
#include "settings.h"

static const char* TAG = "presence";

namespace presence {

// Pairing service: reading the characteristic requires encryption, which
// makes the phone start pairing (and bonding, per setSecurityAuth).
static const char* SVC_UUID = "b47a0001-9f21-4d9e-a1c3-5ab9d2f0c001";
static const char* CHR_UUID = "b47a0002-9f21-4d9e-a1c3-5ab9d2f0c001";
static const char* ADV_NAME = "BatMon Display";
static const char* NS = "phones";
static const char* NS_BEACONS = "beacons";

static Phone phones[MAX_PHONES];
static int   nPhones = 0;
static SemaphoreHandle_t mtx;
static NimBLEServer* server = nullptr;
static uint32_t pairingUntilMs = 0;
static uint32_t bootMs = 0;
static uint32_t absentSinceMs = 0;      // when the last phone left; 0 = not in leave-hold
static bool     wasPresent = false;
static int8_t   pendingRelay = 0;       // +1 on / -1 off waiting for a BatMon link
static uint32_t pendingDisconnectHandle = 0;
static uint32_t disconnectAtMs = 0;
static BeaconCandidate cands[MAX_CANDIDATES];
static int nCands = 0;
static uint32_t beaconScanUntilMs = 0;

// ---------------------------------------------------------------------------
// Names in NVS (NimBLE keeps the bonds themselves)
// ---------------------------------------------------------------------------
static void addrKey(const char* addr, char out[16]) {
    // "aa:bb:cc:dd:ee:ff" -> "aabbccddeeff" (NVS keys are max 15 chars)
    int j = 0;
    for (int i = 0; addr[i] && j < 12; i++)
        if (addr[i] != ':') out[j++] = addr[i];
    out[j] = 0;
}

static void loadName(Phone& p) {
    Preferences pr;
    char key[16];
    addrKey(p.addr, key);
    if (pr.begin(NS, true)) {
        pr.getString(key, p.name, sizeof p.name);
        pr.end();
    }
    if (!p.name[0]) snprintf(p.name, sizeof p.name, "Phone");
}

static void saveName(const Phone& p) {
    Preferences pr;
    char key[16];
    addrKey(p.addr, key);
    if (pr.begin(NS, false)) {
        pr.putString(key, p.name);
        pr.end();
    }
}

static void eraseName(const Phone& p) {
    Preferences pr;
    char key[16];
    addrKey(p.addr, key);
    if (pr.begin(NS, false)) {
        if (pr.isKey(key)) pr.remove(key);
        pr.end();
    }
}

// ---------------------------------------------------------------------------
// iBeacon tags in NVS: n, u<i> (uuid hex), mj<i>, mn<i>, nm<i>
// ---------------------------------------------------------------------------
static bool hexToBytes(const char* hex, uint8_t* out, size_t n) {
    size_t k = 0;
    uint8_t cur = 0;
    int nib = 0;
    for (const char* p = hex; *p && k < n; p++) {
        if (*p == '-') continue;
        int v;
        if (*p >= '0' && *p <= '9') v = *p - '0';
        else if (*p >= 'a' && *p <= 'f') v = *p - 'a' + 10;
        else if (*p >= 'A' && *p <= 'F') v = *p - 'A' + 10;
        else return false;
        cur = (cur << 4) | v;
        if (++nib == 2) { out[k++] = cur; nib = 0; cur = 0; }
    }
    return k == n;
}

void formatUuid(const uint8_t u[16], char out[37]) {
    snprintf(out, 37, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             u[0], u[1], u[2], u[3], u[4], u[5], u[6], u[7], u[8], u[9], u[10], u[11], u[12], u[13], u[14], u[15]);
}

static int loadBeacons(Phone* out, int maxN) {
    Preferences pr;
    if (!pr.begin(NS_BEACONS, true)) return 0;
    int n = pr.getUChar("n", 0);
    int got = 0;
    for (int i = 0; i < n && got < maxN; i++) {
        char key[8], hex[40] = {0};
        snprintf(key, sizeof key, "u%d", i);
        pr.getString(key, hex, sizeof hex);
        Phone& p = out[got];
        memset(&p, 0, sizeof p);
        if (!hexToBytes(hex, p.uuid, 16)) continue;
        p.isBeacon = true;
        snprintf(key, sizeof key, "mj%d", i); p.major = pr.getUShort(key, 0xFFFF);
        snprintf(key, sizeof key, "mn%d", i); p.minor = pr.getUShort(key, 0xFFFF);
        snprintf(key, sizeof key, "nm%d", i); pr.getString(key, p.name, sizeof p.name);
        if (!p.name[0]) snprintf(p.name, sizeof p.name, "Beacon %d", i + 1);
        formatUuid(p.uuid, hex);
        snprintf(p.addr, sizeof p.addr, "%.8s", hex);   // short id for display/logs
        got++;
    }
    pr.end();
    return got;
}

static void saveBeacons() {
    Preferences pr;
    if (!pr.begin(NS_BEACONS, false)) return;
    pr.clear();
    int n = 0;
    for (int i = 0; i < nPhones; i++) {
        const Phone& p = phones[i];
        if (!p.isBeacon) continue;
        char key[8], hex[40];
        formatUuid(p.uuid, hex);
        snprintf(key, sizeof key, "u%d", n);  pr.putString(key, hex);
        snprintf(key, sizeof key, "mj%d", n); pr.putUShort(key, p.major);
        snprintf(key, sizeof key, "mn%d", n); pr.putUShort(key, p.minor);
        snprintf(key, sizeof key, "nm%d", n); pr.putString(key, p.name);
        n++;
    }
    pr.putUChar("n", n);
    pr.end();
}

// ---------------------------------------------------------------------------
// Bond list + beacons -> phones[]
// ---------------------------------------------------------------------------
static bool readIrk(const NimBLEAddress& addr, uint8_t irk[16]) {
    ble_store_key_sec key;
    memset(&key, 0, sizeof key);
    key.peer_addr = *addr.getBase();
    ble_store_value_sec val;
    memset(&val, 0, sizeof val);
    if (ble_store_read_peer_sec(&key, &val) != 0 || !val.irk_present) return false;
    memcpy(irk, val.irk, 16);
    return true;
}

static void reload() {
    xSemaphoreTake(mtx, portMAX_DELAY);
    Phone old[MAX_PHONES];
    int nOld = nPhones;
    memcpy(old, phones, sizeof old);

    nPhones = 0;
    int n = NimBLEDevice::getNumBonds();
    for (int i = 0; i < n && nPhones < MAX_PHONES; i++) {
        NimBLEAddress a = NimBLEDevice::getBondedAddress(i);
        Phone& p = phones[nPhones];
        memset(&p, 0, sizeof p);
        strlcpy(p.addr, a.toString().c_str(), sizeof p.addr);
        p.addrType = a.getType();
        p.hasIrk = readIrk(a, p.irk);
        loadName(p);
        // keep presence timing across reloads
        for (int k = 0; k < nOld; k++)
            if (!strcasecmp(old[k].addr, p.addr)) { p.lastSeenMs = old[k].lastSeenMs; p.rssi = old[k].rssi; }
        ESP_LOGI(TAG, "paired phone %d: %s '%s' irk=%d", nPhones, p.addr, p.name, p.hasIrk);
        nPhones++;
    }
    Phone beacons[MAX_PHONES];
    int nb = loadBeacons(beacons, MAX_PHONES);
    for (int b = 0; b < nb && nPhones < MAX_PHONES; b++) {
        Phone& p = phones[nPhones];
        p = beacons[b];
        for (int k = 0; k < nOld; k++)
            if (old[k].isBeacon && !memcmp(old[k].uuid, p.uuid, 16) && old[k].major == p.major && old[k].minor == p.minor) {
                p.lastSeenMs = old[k].lastSeenMs; p.rssi = old[k].rssi; p.firstSeenMs = old[k].firstSeenMs;
            }
        ESP_LOGI(TAG, "beacon %d: %s.. %u/%u '%s'", nPhones, p.addr, p.major, p.minor, p.name);
        nPhones++;
    }
    xSemaphoreGive(mtx);
}

// ---------------------------------------------------------------------------
// GATT server callbacks
// ---------------------------------------------------------------------------
class ServerCb : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer*, NimBLEConnInfo& info) override {
        ESP_LOGI(TAG, "phone connected %s", info.getAddress().toString().c_str());
    }
    void onDisconnect(NimBLEServer*, NimBLEConnInfo& info, int reason) override {
        ESP_LOGI(TAG, "phone disconnected (%d)", reason);
        // Keep advertising while the pairing window is open.
        if (pairing()) NimBLEDevice::getAdvertising()->start(0);
    }
    void onAuthenticationComplete(NimBLEConnInfo& info) override {
        if (!info.isBonded()) {
            ESP_LOGW(TAG, "pairing finished without bonding");
            return;
        }
        NimBLEAddress id = info.getIdAddress();
        ESP_LOGI(TAG, "bonded with %s", id.toString().c_str());
        reload();
        // It is obviously here right now.
        xSemaphoreTake(mtx, portMAX_DELAY);
        for (int i = 0; i < nPhones; i++)
            if (!strcasecmp(phones[i].addr, id.toString().c_str())) {
                phones[i].firstSeenMs = 0;
                if (!strcmp(phones[i].name, "Phone")) {
                    snprintf(phones[i].name, sizeof phones[i].name, "Phone %d", i + 1);
                    saveName(phones[i]);
                }
                phones[i].lastSeenMs = millis();
            }
        xSemaphoreGive(mtx);
        stopPairing();
        // Drop the link shortly; presence comes from advertisements from now on.
        pendingDisconnectHandle = info.getConnHandle();
        disconnectAtMs = millis() + 1500;
    }
};
static ServerCb serverCb;

// ---------------------------------------------------------------------------
// Public
// ---------------------------------------------------------------------------
void begin() {
    mtx = xSemaphoreCreateMutex();
    bootMs = millis();

    // Just-works pairing with bonding and LE Secure Connections.
    NimBLEDevice::setSecurityAuth(true, false, true);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

    server = NimBLEDevice::createServer();
    server->setCallbacks(&serverCb);
    server->advertiseOnDisconnect(false);
    NimBLEService* svc = server->createService(SVC_UUID);
    NimBLECharacteristic* chr =
        svc->createCharacteristic(CHR_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::READ_ENC);
    chr->setValue("paired");
    svc->start();
    server->start();   // registers the services with the host; advertising asserts without it

    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->setName(ADV_NAME);
    adv->addServiceUUID(SVC_UUID);

    reload();
}

int count() { return nPhones; }
const Phone& phone(int i) { return phones[i < 0 ? 0 : (i >= MAX_PHONES ? MAX_PHONES - 1 : i)]; }
bool anyPaired() { return nPhones > 0; }

bool present(int i) {
    if (i < 0 || i >= nPhones) return false;
    uint32_t seen = phones[i].lastSeenMs;
    return seen != 0 && (millis() - seen) < (uint32_t)g_settings.presenceTimeoutS * 1000UL;
}

bool anyPresent() {
    for (int i = 0; i < nPhones; i++)
        if (present(i)) return true;
    return false;
}

int nearestPresent() {
    int best = -1;
    for (int i = 0; i < nPhones; i++)
        if (present(i) && (best < 0 || phones[i].rssi > phones[best].rssi)) best = i;
    return best;
}

bool gateOpen() {
    if (nPhones == 0) return true;
    if (millis() - bootMs < BOOT_GRACE_MS) return true;
    if (anyPresent()) return true;
    if (absentSinceMs && millis() - absentSinceMs < LEAVE_HOLD_MS) return true;
    return false;
}

// Resolvable private address check: hash = ah(IRK, prand).
// Byte order follows NimBLE's ble_hs_resolv_rpa(): address bytes are
// little-endian (val[5] is the first printed octet), the AES key is the
// byte-reversed IRK, and prand goes into the last three plaintext bytes.
static bool rpaMatches(const uint8_t* val, const uint8_t* irk) {
    if ((val[5] & 0xC0) != 0x40) return false;   // not an RPA
    uint8_t key[16];
    for (int i = 0; i < 16; i++) key[i] = irk[15 - i];
    uint8_t pt[16] = {0}, ct[16];
    pt[13] = val[5];
    pt[14] = val[4];
    pt[15] = val[3];
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_enc(&aes, key, 128);
    mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, pt, ct);
    mbedtls_aes_free(&aes);
    return ct[15] == val[0] && ct[14] == val[1] && ct[13] == val[2];
}

// Marks entry i as seen now, with the return debounce.
static void noteSeen(Phone& p, int rssi, uint32_t now) {
    p.rssi = (int8_t)rssi;
    bool wasPresent = p.lastSeenMs != 0 && (now - p.lastSeenMs) < (uint32_t)g_settings.presenceTimeoutS * 1000UL;
    if (wasPresent) {
        p.lastSeenMs = now;
    } else if (p.firstSeenMs && now - p.firstSeenMs < PRESENCE_CONFIRM_MS) {
        p.lastSeenMs = now;       // second sighting within the window: really back
        p.firstSeenMs = 0;
    } else {
        p.firstSeenMs = now;      // one stray advert must not wake the display
    }
}

// Apple iBeacon: company 0x004C, type 0x02, len 0x15, uuid[16], major, minor, txpower
static bool parseIBeacon(const uint8_t* m, size_t n, const uint8_t*& uuid, uint16_t& major, uint16_t& minor) {
    if (!m || n < 25) return false;
    if (m[0] != 0x4C || m[1] != 0x00 || m[2] != 0x02 || m[3] != 0x15) return false;
    uuid = m + 4;
    major = (uint16_t)(m[20] << 8 | m[21]);
    minor = (uint16_t)(m[22] << 8 | m[23]);
    return true;
}

void onAdvert(const NimBLEAddress& addr, int rssi, const uint8_t* mfg, size_t mfgLen) {
    if (rssi < PRESENCE_MIN_RSSI) return;
    uint32_t now = millis();

    const uint8_t* buuid = nullptr;
    uint16_t bmajor = 0, bminor = 0;
    bool isBeacon = parseIBeacon(mfg, mfgLen, buuid, bmajor, bminor);

    // Beacon discovery window: remember what is around.
    if (isBeacon && beaconScanning()) {
        xSemaphoreTake(mtx, portMAX_DELAY);
        int slot = -1;
        for (int i = 0; i < nCands; i++)
            if (!memcmp(cands[i].uuid, buuid, 16) && cands[i].major == bmajor && cands[i].minor == bminor) { slot = i; break; }
        if (slot < 0 && nCands < MAX_CANDIDATES) slot = nCands++;
        if (slot >= 0) {
            memcpy(cands[slot].uuid, buuid, 16);
            cands[slot].major = bmajor;
            cands[slot].minor = bminor;
            cands[slot].rssi = (int8_t)rssi;
            cands[slot].seenMs = now;
        }
        xSemaphoreGive(mtx);
    }

    if (nPhones == 0) return;
    const uint8_t* val = addr.getVal();
    uint8_t type = addr.getType();
    bool isRpa = (type & 1) && (val[5] & 0xC0) == 0x40;   // random, resolvable
    std::string s;
    if (!isRpa) s = addr.toString();

    xSemaphoreTake(mtx, portMAX_DELAY);
    for (int i = 0; i < nPhones; i++) {
        bool hit;
        if (phones[i].isBeacon) {
            hit = isBeacon && !memcmp(phones[i].uuid, buuid, 16) &&
                  (phones[i].major == 0xFFFF || phones[i].major == bmajor) &&
                  (phones[i].minor == 0xFFFF || phones[i].minor == bminor);
        } else if (isRpa) {
            hit = phones[i].hasIrk && rpaMatches(val, phones[i].irk);
        } else {
            hit = !strcasecmp(s.c_str(), phones[i].addr);   // controller already resolved it
        }
        if (hit) {
            noteSeen(phones[i], rssi, now);
            break;
        }
    }
    xSemaphoreGive(mtx);
}

void startPairing(uint32_t ms) {
    pairingUntilMs = millis() + ms;
    NimBLEDevice::getAdvertising()->start(0);
    ESP_LOGI(TAG, "pairing window open for %lu s", (unsigned long)ms / 1000);
}

void stopPairing() {
    pairingUntilMs = 0;
    NimBLEDevice::getAdvertising()->stop();
}

bool pairing() { return pairingUntilMs != 0 && (int32_t)(millis() - pairingUntilMs) < 0; }

uint32_t pairingRemainingMs() {
    if (!pairing()) return 0;
    return pairingUntilMs - millis();
}

// ble_gap_unpair() refuses (BLE_HS_EBUSY) to drop a bond that carries an
// IRK while scanning or advertising is active, and our scan runs all the
// time.  Pause both around the delete.
static bool deleteBondQuiet(const NimBLEAddress& a) {
    NimBLEScan* scan = NimBLEDevice::getScan();
    bool wasScanning = scan->isScanning();
    bool wasAdv = NimBLEDevice::getAdvertising()->isAdvertising();
    if (wasScanning) scan->stop();
    if (wasAdv) NimBLEDevice::getAdvertising()->stop();
    bool ok = NimBLEDevice::deleteBond(a);
    if (!ok) ESP_LOGW(TAG, "deleteBond %s failed", a.toString().c_str());
    if (wasAdv) NimBLEDevice::getAdvertising()->start(0);
    if (wasScanning) scan->start(0, false, true);
    return ok;
}

bool forget(int i) {
    if (i < 0 || i >= nPhones) return false;
    if (phones[i].isBeacon) {
        xSemaphoreTake(mtx, portMAX_DELAY);
        for (int k = i; k + 1 < nPhones; k++) phones[k] = phones[k + 1];
        nPhones--;
        saveBeacons();
        xSemaphoreGive(mtx);
        reload();
        return true;
    }
    NimBLEAddress a(std::string(phones[i].addr), phones[i].addrType);
    bool ok = deleteBondQuiet(a);
    if (ok) eraseName(phones[i]);
    reload();
    return ok;
}

void forgetAll() {
    for (int i = 0; i < nPhones; i++) if (!phones[i].isBeacon) eraseName(phones[i]);
    xSemaphoreTake(mtx, portMAX_DELAY);
    nPhones = 0;
    saveBeacons();
    xSemaphoreGive(mtx);
    NimBLEScan* scan = NimBLEDevice::getScan();
    bool wasScanning = scan->isScanning();
    if (wasScanning) scan->stop();
    NimBLEDevice::getAdvertising()->stop();
    NimBLEDevice::deleteAllBonds();
    if (wasScanning) scan->start(0, false, true);
    reload();
}

bool rename(int i, const char* name) {
    if (i < 0 || i >= nPhones || !name || !name[0]) return false;
    xSemaphoreTake(mtx, portMAX_DELAY);
    strlcpy(phones[i].name, name, sizeof phones[i].name);
    if (phones[i].isBeacon) saveBeacons(); else saveName(phones[i]);
    xSemaphoreGive(mtx);
    return true;
}

// ---------------------------------------------------------------------------
// iBeacon tags
// ---------------------------------------------------------------------------
void startBeaconScan(uint32_t ms) {
    xSemaphoreTake(mtx, portMAX_DELAY);
    nCands = 0;
    beaconScanUntilMs = millis() + ms;
    xSemaphoreGive(mtx);
    ESP_LOGI(TAG, "beacon discovery for %lu s", (unsigned long)ms / 1000);
}

bool beaconScanning() { return beaconScanUntilMs != 0 && (int32_t)(millis() - beaconScanUntilMs) < 0; }
int candidateCount() { return nCands; }
const BeaconCandidate& candidate(int i) { return cands[i < 0 ? 0 : (i >= MAX_CANDIDATES ? MAX_CANDIDATES - 1 : i)]; }

static int addBeaconRaw(const uint8_t* uuid, uint16_t major, uint16_t minor, const char* name) {
    xSemaphoreTake(mtx, portMAX_DELAY);
    for (int i = 0; i < nPhones; i++)
        if (phones[i].isBeacon && !memcmp(phones[i].uuid, uuid, 16) && phones[i].major == major && phones[i].minor == minor) {
            xSemaphoreGive(mtx);
            return i;   // already there
        }
    if (nPhones >= MAX_PHONES) { xSemaphoreGive(mtx); return -1; }
    Phone& p = phones[nPhones];
    memset(&p, 0, sizeof p);
    p.isBeacon = true;
    memcpy(p.uuid, uuid, 16);
    p.major = major;
    p.minor = minor;
    char hex[40];
    formatUuid(p.uuid, hex);
    snprintf(p.addr, sizeof p.addr, "%.8s", hex);
    if (name && name[0]) strlcpy(p.name, name, sizeof p.name);
    else snprintf(p.name, sizeof p.name, "Beacon %d", nPhones + 1);
    p.lastSeenMs = millis();   // it was just seen
    int idx = nPhones++;
    saveBeacons();
    xSemaphoreGive(mtx);
    beaconScanUntilMs = 0;
    ESP_LOGI(TAG, "added beacon '%s' %s.. %u/%u", p.name, p.addr, major, minor);
    return idx;
}

int addBeacon(int c, const char* name) {
    if (c < 0 || c >= nCands) return -1;
    BeaconCandidate cand = cands[c];
    return addBeaconRaw(cand.uuid, cand.major, cand.minor, name);
}

int addBeacon(const char* uuidHex, uint16_t major, uint16_t minor, const char* name) {
    uint8_t uuid[16];
    if (!hexToBytes(uuidHex, uuid, 16)) return -1;
    return addBeaconRaw(uuid, major, minor, name);
}

void tick() {
    uint32_t now = millis();

    if (pairingUntilMs && !pairing()) stopPairing();
    if (beaconScanUntilMs && !beaconScanning()) beaconScanUntilMs = 0;

    if (pendingDisconnectHandle && (int32_t)(now - disconnectAtMs) >= 0) {
        if (server) server->disconnect((uint16_t)pendingDisconnectHandle);
        pendingDisconnectHandle = 0;
    }

    if (nPhones == 0) {
        wasPresent = false;
        absentSinceMs = 0;
        return;
    }

    bool nowPresent = anyPresent();
    if (nowPresent != wasPresent) {
        wasPresent = nowPresent;
        ESP_LOGI(TAG, "phone %s", nowPresent ? "arrived" : "left");
        absentSinceMs = nowPresent ? 0 : now;
        if (g_settings.relayFollowsPhone) {
            if (nowPresent) {
                pendingRelay = +1;                     // sent once the BatMon is connected
            } else {
                // Link is still up during LEAVE_HOLD_MS: send it straight away.
                batmon::g_client.requestSetIo(batmon::IoType::Relay, false);
                pendingRelay = 0;
            }
        }
    }

    if (pendingRelay && batmon::g_client.snapshot().link == batmon::LinkState::Connected) {
        batmon::g_client.requestSetIo(batmon::IoType::Relay, pendingRelay > 0);
        pendingRelay = 0;
    }
}

}  // namespace presence
