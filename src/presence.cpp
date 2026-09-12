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
        pr.remove(key);
        pr.end();
    }
}

// ---------------------------------------------------------------------------
// Bond list -> phones[]
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
    return seen != 0 && (millis() - seen) < PRESENCE_TIMEOUT_MS;
}

bool anyPresent() {
    for (int i = 0; i < nPhones; i++)
        if (present(i)) return true;
    return false;
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

void onAdvert(const NimBLEAddress& addr, int rssi) {
    if (nPhones == 0 || rssi < PRESENCE_MIN_RSSI) return;
    const uint8_t* val = addr.getVal();
    uint8_t type = addr.getType();
    bool isRpa = (type & 1) && (val[5] & 0xC0) == 0x40;   // random, resolvable
    std::string s;
    if (!isRpa) s = addr.toString();

    xSemaphoreTake(mtx, portMAX_DELAY);
    for (int i = 0; i < nPhones; i++) {
        bool hit;
        if (isRpa) hit = phones[i].hasIrk && rpaMatches(val, phones[i].irk);
        else       hit = !strcasecmp(s.c_str(), phones[i].addr);   // controller already resolved it
        if (hit) {
            phones[i].lastSeenMs = millis();
            phones[i].rssi = (int8_t)rssi;
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

bool forget(int i) {
    if (i < 0 || i >= nPhones) return false;
    NimBLEAddress a(std::string(phones[i].addr), phones[i].addrType);
    eraseName(phones[i]);
    bool ok = NimBLEDevice::deleteBond(a);
    reload();
    return ok;
}

void forgetAll() {
    for (int i = 0; i < nPhones; i++) eraseName(phones[i]);
    NimBLEDevice::deleteAllBonds();
    reload();
}

bool rename(int i, const char* name) {
    if (i < 0 || i >= nPhones || !name || !name[0]) return false;
    xSemaphoreTake(mtx, portMAX_DELAY);
    strlcpy(phones[i].name, name, sizeof phones[i].name);
    saveName(phones[i]);
    xSemaphoreGive(mtx);
    return true;
}

void tick() {
    uint32_t now = millis();

    if (pairingUntilMs && !pairing()) stopPairing();

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
