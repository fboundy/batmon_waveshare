#include "obd_client.h"

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "../batmon/batmon_client.h"
#include "../config.h"
#include "../presence.h"
#include "../settings.h"

static const char* TAG = "obd";

namespace obd {

const char* linkName(Link l) {
    switch (l) {
        case Link::Disabled:     return "Off";
        case Link::Idle:         return "No adapter";
        case Link::Scanning:     return "Looking";
        case Link::Connecting:   return "Connecting";
        case Link::Connected:    return "Connected";
        case Link::Reconnecting: return "Reconnecting";
        case Link::Standby:      return "Standby";
    }
    return "?";
}

// ---------------------------------------------------------------------------
// Known BLE "serial port" layouts of ELM327 dongles.  First match wins;
// otherwise the first notify + first write characteristic found are used.
// ---------------------------------------------------------------------------
struct Layout { const char* name; const char* svc; const char* write; const char* notify; };
static const Layout kLayouts[] = {
    {"FFF0 (Veepeak OBDCheck BLE / most clones)", "fff0", "fff2", "fff1"},
    {"FFE0 (HM-10 style)",                        "ffe0", "ffe1", "ffe1"},
    {"Microchip RN4870 UART (Veepeak BLE+)",
     "49535343-fe7d-4ae5-8fa9-9fafd205e455", "49535343-8841-43f4-a8d4-ecbe34729bb3",
     "49535343-1e4d-4bd9-ba61-23c647249616"},
    {"Nordic UART", "6e400001-b5a3-f393-e0a9-e50e24dcca9e", "6e400002-b5a3-f393-e0a9-e50e24dcca9e",
     "6e400003-b5a3-f393-e0a9-e50e24dcca9e"},
    {"E7810A71 (OBDBLE)", "e7810a71-73ae-499d-8c15-faa9aef0c3f2", "bef8d6c9-9c21-4c9e-b632-bd58c1009f9f",
     "bef8d6c9-9c21-4c9e-b632-bd58c1009f9f"},
};

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
struct Cmd {
    enum Kind : uint8_t { Connect, Forget, Disconnect, Raw } kind;
    int idx;
    char text[48];
};

static State state_;
static SemaphoreHandle_t mtx;
static QueueHandle_t queue_;
static Candidate cands[MAX_CANDIDATES];
static int nCands = 0;
static bool logOn = false;

static NimBLEClient* client_ = nullptr;
static NimBLERemoteCharacteristic* chrWrite_ = nullptr;
static NimBLERemoteCharacteristic* chrNotify_ = nullptr;
static volatile bool linkDropped_ = false;
static bool wantReconnect_ = false;

// Receive buffer filled by notifications; a '>' prompt ends a reply.
static char rx_[512];
static size_t rxLen_ = 0;
static SemaphoreHandle_t rxSem;

static void setLink(Link l) {
    xSemaphoreTake(mtx, portMAX_DELAY);
    state_.link = l;
    xSemaphoreGive(mtx);
}

static void store(Value& v, float f) {
    v.v = f;
    v.updatedMs = millis();
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
State snapshot() {
    State s;
    xSemaphoreTake(mtx, portMAX_DELAY);
    s = state_;
    xSemaphoreGive(mtx);
    return s;
}

void setEnabled(bool on) {
    g_settings.obdEnabled = on;
    g_settings.save();
    if (!on) {
        Cmd c{Cmd::Disconnect, 0, {0}};
        xQueueSend(queue_, &c, 0);
    }
}

void connectTo(int i) {
    Cmd c{Cmd::Connect, i, {0}};
    xQueueSend(queue_, &c, 0);
}

void forget() {
    Cmd c{Cmd::Forget, 0, {0}};
    xQueueSend(queue_, &c, 0);
}

void disconnect() {
    Cmd c{Cmd::Disconnect, 0, {0}};
    xQueueSend(queue_, &c, 0);
}

void sendRaw(const char* cmd) {
    Cmd c{Cmd::Raw, 0, {0}};
    strlcpy(c.text, cmd, sizeof c.text);
    xQueueSend(queue_, &c, 0);
}

void setLog(bool on) { logOn = on; }
bool log() { return logOn; }

int candidateCount() { return nCands; }
const Candidate& candidate(int i) { return cands[i < 0 ? 0 : (i >= MAX_CANDIDATES ? MAX_CANDIDATES - 1 : i)]; }

// ---------------------------------------------------------------------------
// Discovery: anything that looks like an OBD dongle goes in the candidate list
// ---------------------------------------------------------------------------
static bool looksLikeObd(const NimBLEAdvertisedDevice* d) {
    if (d->haveName()) {
        std::string n = d->getName();
        for (char& c : n) c = tolower((unsigned char)c);
        static const char* keys[] = {"obd", "veepeak", "vlink", "v-link", "elm", "carista", "kiwi", "obdlink", "ios-vlink"};
        for (const char* k : keys)
            if (n.find(k) != std::string::npos) return true;
    }
    for (const Layout& l : kLayouts)
        if (d->isAdvertisingService(NimBLEUUID(l.svc))) return true;
    return false;
}

void onAdvert(const NimBLEAdvertisedDevice* d) {
    if (!looksLikeObd(d)) return;
    std::string addr = d->getAddress().toString();
    xSemaphoreTake(mtx, portMAX_DELAY);
    int slot = -1;
    for (int i = 0; i < nCands; i++)
        if (!strcasecmp(cands[i].addr, addr.c_str())) { slot = i; break; }
    if (slot < 0) {
        if (nCands < MAX_CANDIDATES) slot = nCands++;
        else {
            // replace the stalest
            slot = 0;
            for (int i = 1; i < nCands; i++) if (cands[i].seenMs < cands[slot].seenMs) slot = i;
        }
        memset(&cands[slot], 0, sizeof cands[slot]);
        strlcpy(cands[slot].addr, addr.c_str(), sizeof cands[slot].addr);
        cands[slot].addrType = d->getAddress().getType();
    }
    if (d->haveName()) strlcpy(cands[slot].name, d->getName().c_str(), sizeof cands[slot].name);
    cands[slot].rssi = (int8_t)d->getRSSI();
    cands[slot].seenMs = millis();
    xSemaphoreGive(mtx);
}

// ---------------------------------------------------------------------------
// BLE link
// ---------------------------------------------------------------------------
class ClientCb : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient*) override { ESP_LOGI(TAG, "connected"); }
    void onDisconnect(NimBLEClient*, int reason) override {
        ESP_LOGW(TAG, "disconnected, reason=%d", reason);
        linkDropped_ = true;
    }
};
static ClientCb clientCb;

static void onNotify(NimBLERemoteCharacteristic*, uint8_t* data, size_t len, bool) {
    if (logOn) {
        char buf[64];
        size_t n = len < 60 ? len : 60;
        for (size_t i = 0; i < n; i++) buf[i] = (data[i] >= 32 && data[i] < 127) ? (char)data[i] : '.';
        buf[n] = 0;
        Serial.printf("obd <  %s\n", buf);
    }
    for (size_t i = 0; i < len; i++) {
        if (rxLen_ < sizeof rx_ - 1) rx_[rxLen_++] = (char)data[i];
    }
    rx_[rxLen_] = 0;
    if (len && (data[len - 1] == '>' || memchr(data, '>', len))) xSemaphoreGive(rxSem);
}

static bool findCharacteristics() {
    chrWrite_ = chrNotify_ = nullptr;
    const std::vector<NimBLERemoteService*>& services = client_->getServices(true);
    NimBLERemoteCharacteristic* firstWrite = nullptr;
    NimBLERemoteCharacteristic* firstNotify = nullptr;
    ESP_LOGI(TAG, "GATT table:");
    for (NimBLERemoteService* svc : services) {
        ESP_LOGI(TAG, " service %s", svc->getUUID().toString().c_str());
        for (NimBLERemoteCharacteristic* ch : svc->getCharacteristics(true)) {
            ESP_LOGI(TAG, "   chr %s r=%d w=%d wnr=%d n=%d i=%d", ch->getUUID().toString().c_str(), ch->canRead(),
                     ch->canWrite(), ch->canWriteNoResponse(), ch->canNotify(), ch->canIndicate());
            if (!firstWrite && (ch->canWrite() || ch->canWriteNoResponse())) firstWrite = ch;
            if (!firstNotify && ch->canNotify()) firstNotify = ch;
        }
    }
    for (const Layout& l : kLayouts) {
        NimBLERemoteService* svc = nullptr;
        for (NimBLERemoteService* s : services)
            if (s->getUUID() == NimBLEUUID(l.svc)) { svc = s; break; }
        if (!svc) continue;
        NimBLERemoteCharacteristic* w = svc->getCharacteristic(NimBLEUUID(l.write));
        NimBLERemoteCharacteristic* n = svc->getCharacteristic(NimBLEUUID(l.notify));
        if (w && n) {
            chrWrite_ = w;
            chrNotify_ = n;
            ESP_LOGI(TAG, "using layout: %s", l.name);
            break;
        }
    }
    if (!chrWrite_ || !chrNotify_) {
        chrWrite_ = firstWrite;
        chrNotify_ = firstNotify;
        if (chrWrite_ && chrNotify_) ESP_LOGW(TAG, "unknown layout; using first write %s + first notify %s",
                                             chrWrite_->getUUID().toString().c_str(), chrNotify_->getUUID().toString().c_str());
    }
    if (!chrWrite_ || !chrNotify_) {
        ESP_LOGE(TAG, "no usable write/notify characteristics");
        return false;
    }
    if (!chrNotify_->subscribe(true, onNotify)) {
        ESP_LOGE(TAG, "subscribe failed");
        return false;
    }
    return true;
}

static bool connectAdapter() {
    if (!g_settings.obdAddr[0]) return false;
    if (!client_) {
        client_ = NimBLEDevice::createClient();
        client_->setClientCallbacks(&clientCb, false);
        client_->setConnectTimeout(OBD_CONNECT_TIMEOUT_MS);
        client_->setConnectionParams(24, 40, 0, 400);
    }
    linkDropped_ = false;
    NimBLEAddress addr(std::string(g_settings.obdAddr), g_settings.obdAddrType);
    ESP_LOGI(TAG, "connecting to %s", g_settings.obdAddr);
    batmon::g_client.radioAcquire();   // one connection attempt at a time, scan paused
    bool ok = client_->connect(addr, true, false, true);
    if (ok && !findCharacteristics()) {
        client_->disconnect();
        ok = false;
    }
    batmon::g_client.radioRelease();
    if (!ok) ESP_LOGW(TAG, "connect failed");
    return ok;
}

static void dropLink() {
    chrWrite_ = chrNotify_ = nullptr;
    if (client_ && client_->isConnected()) client_->disconnect();
}

// ---------------------------------------------------------------------------
// ELM327 transactions
// ---------------------------------------------------------------------------
// Sends `cmd` + CR, waits for the '>' prompt, returns the reply with the
// echo, CR/LF and prompt stripped.
static bool elm(const char* cmd, char* out, size_t outLen, uint32_t timeoutMs = OBD_REPLY_TIMEOUT_MS) {
    out[0] = 0;
    if (!chrWrite_ || linkDropped_) return false;
    rxLen_ = 0;
    rx_[0] = 0;
    xSemaphoreTake(rxSem, 0);
    char line[64];
    snprintf(line, sizeof line, "%s\r", cmd);
    if (logOn) Serial.printf("obd >  %s\n", cmd);
    bool needResp = !chrWrite_->canWriteNoResponse();
    if (!chrWrite_->writeValue((const uint8_t*)line, strlen(line), needResp)) {
        ESP_LOGW(TAG, "write failed");
        return false;
    }
    bool got = xSemaphoreTake(rxSem, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
    // Clean up: drop echo of the command, CR/LF, prompt, and collapse
    // multi-line replies to '|'-separated lines.
    size_t o = 0;
    const char* p = rx_;
    size_t cmdLen = strlen(cmd);
    if (!strncasecmp(p, cmd, cmdLen)) p += cmdLen;
    bool lastSep = true;
    for (; *p && o < outLen - 1; p++) {
        char c = *p;
        if (c == '>') break;
        if (c == '\r' || c == '\n') {
            if (!lastSep) { out[o++] = '|'; lastSep = true; }
            continue;
        }
        out[o++] = c;
        lastSep = false;
    }
    while (o && out[o - 1] == '|') o--;
    out[o] = 0;
    if (!got) ESP_LOGW(TAG, "timeout waiting for '>' after %s (got '%s')", cmd, out);
    xSemaphoreTake(mtx, portMAX_DELAY);
    strlcpy(state_.lastRaw, out, sizeof state_.lastRaw);
    xSemaphoreGive(mtx);
    return got;
}

static int hexv(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Finds the "41 <pid>" line in a reply and returns its data bytes.
static int pidData(const char* reply, uint8_t pid, uint8_t* data, int maxN) {
    char want[3];
    snprintf(want, sizeof want, "%02X", pid);
    const char* line = reply;
    while (line && *line) {
        const char* end = strchr(line, '|');
        // pack hex digits of this line, ignoring spaces
        uint8_t bytes[24];
        int n = 0, hi = -1;
        for (const char* q = line; *q && (!end || q < end) && n < (int)sizeof bytes; q++) {
            int v = hexv(*q);
            if (v < 0) continue;
            if (hi < 0) hi = v; else { bytes[n++] = (uint8_t)(hi << 4 | v); hi = -1; }
        }
        if (n >= 3 && bytes[0] == 0x41 && bytes[1] == pid) {
            int cnt = n - 2 < maxN ? n - 2 : maxN;
            memcpy(data, bytes + 2, cnt);
            return cnt;
        }
        line = end ? end + 1 : nullptr;
    }
    return 0;
}

static bool pidSupported(const State& s, uint8_t pid) {
    if (pid == 0 || pid > 0x80) return false;
    int grp = (pid - 1) / 32, bit = 31 - ((pid - 1) % 32);
    return (s.supported[grp] >> bit) & 1;
}

int supportedPids(const State& s, char* out, size_t outLen) {
    int cnt = 0;
    size_t o = 0;
    for (int pid = 1; pid <= 0x80; pid++) {
        if (!pidSupported(s, pid)) continue;
        cnt++;
        if (o + 3 < outLen) o += snprintf(out + o, outLen - o, "%02X ", pid);
    }
    if (outLen) out[o] = 0;
    return cnt;
}

static bool readPidBitmap(uint8_t base, uint32_t& mask) {
    char cmd[6], reply[96];
    snprintf(cmd, sizeof cmd, "01%02X", base);
    if (!elm(cmd, reply, sizeof reply)) return false;
    uint8_t d[8];
    int n = pidData(reply, base, d, 4);
    if (n < 4) return false;
    mask = (uint32_t)d[0] << 24 | (uint32_t)d[1] << 16 | (uint32_t)d[2] << 8 | d[3];
    return true;
}

static bool initElm() {
    char r[96];
    elm("ATZ", r, sizeof r, 3000);           // reset (some adapters answer slowly)
    vTaskDelay(pdMS_TO_TICKS(500));
    elm("ATE0", r, sizeof r);                // echo off
    elm("ATL0", r, sizeof r);                // linefeeds off
    elm("ATS0", r, sizeof r);                // spaces off
    elm("ATH0", r, sizeof r);                // headers off
    elm("ATSP0", r, sizeof r);               // auto protocol
    if (elm("ATI", r, sizeof r)) {
        xSemaphoreTake(mtx, portMAX_DELAY);
        strlcpy(state_.elmVersion, r, sizeof state_.elmVersion);
        xSemaphoreGive(mtx);
    }
    // Supported PIDs (also wakes the protocol search; may take a few seconds)
    uint32_t m = 0;
    bool ecu = readPidBitmap(0x00, m);
    xSemaphoreTake(mtx, portMAX_DELAY);
    state_.ecuResponding = ecu;
    state_.supported[0] = ecu ? m : 0;
    state_.supported[1] = state_.supported[2] = state_.supported[3] = 0;
    xSemaphoreGive(mtx);
    if (ecu) {
        for (int g = 1; g < 4; g++) {
            if (!pidSupported(state_, g * 0x20)) break;       // PID 20/40/60 = "next range supported"
            if (!readPidBitmap(g * 0x20, m)) break;
            xSemaphoreTake(mtx, portMAX_DELAY);
            state_.supported[g] = m;
            xSemaphoreGive(mtx);
        }
        if (elm("ATDP", r, sizeof r)) {
            xSemaphoreTake(mtx, portMAX_DELAY);
            strlcpy(state_.protocol, r, sizeof state_.protocol);
            xSemaphoreGive(mtx);
        }
    }
    char list[200];
    int n = supportedPids(state_, list, sizeof list);
    ESP_LOGI(TAG, "ELM '%s' ecu=%d protocol '%s' pids(%d): %s", state_.elmVersion, ecu, state_.protocol, n, list);
    return true;
}

// One PID read -> float via formula.  Returns false on no data.
static bool readPid(uint8_t pid, float& val) {
    char cmd[6], reply[96];
    snprintf(cmd, sizeof cmd, "01%02X", pid);
    if (!elm(cmd, reply, sizeof reply)) return false;
    uint8_t d[8];
    int n = pidData(reply, pid, d, sizeof d);
    if (n < 1) return false;
    switch (pid) {
        case 0x04: val = d[0] * 100.0f / 255.0f; return true;          // engine load %
        case 0x05: val = d[0] - 40.0f; return true;                    // coolant C
        case 0x0C: if (n < 2) return false; val = (d[0] * 256 + d[1]) / 4.0f; return true;   // rpm
        case 0x0D: val = d[0]; return true;                            // km/h
        case 0x0F: val = d[0] - 40.0f; return true;                    // intake C
        case 0x10: if (n < 2) return false; val = (d[0] * 256 + d[1]) / 100.0f; return true; // MAF g/s
        case 0x11: val = d[0] * 100.0f / 255.0f; return true;          // throttle %
        case 0x2F: val = d[0] * 100.0f / 255.0f; return true;          // fuel %
        case 0x42: if (n < 2) return false; val = (d[0] * 256 + d[1]) / 1000.0f; return true; // module V
        case 0x46: val = d[0] - 40.0f; return true;                    // ambient C
        case 0x5C: val = d[0] - 40.0f; return true;                    // oil C
        default: return false;
    }
}

static void pollOnce() {
    struct { uint8_t pid; Value State::*field; } table[] = {
        {0x0C, &State::rpm},      {0x0D, &State::speedKph}, {0x05, &State::coolantC},
        {0x42, &State::voltage},  {0x2F, &State::fuelPct},  {0x04, &State::loadPct},
        {0x11, &State::throttlePct}, {0x0F, &State::intakeC}, {0x46, &State::ambientC},
        {0x5C, &State::oilC},     {0x10, &State::mafGs},
    };
    State s = snapshot();
    bool any = false;
    for (auto& e : table) {
        if (linkDropped_) return;
        if (!pidSupported(s, e.pid)) continue;
        float v;
        bool ok = readPid(e.pid, v);
        xSemaphoreTake(mtx, portMAX_DELAY);
        if (ok) { store(state_.*(e.field), v); state_.okCount++; any = true; }
        else state_.errCount++;
        xSemaphoreGive(mtx);
    }
    // Adapter's own battery-voltage reading works with the engine off.
    char r[32];
    if (elm("ATRV", r, sizeof r)) {
        float v = atof(r);
        if (v > 0) {
            xSemaphoreTake(mtx, portMAX_DELAY);
            store(state_.adapterVolts, v);
            xSemaphoreGive(mtx);
        }
    }
    (void)any;
}

// ---------------------------------------------------------------------------
// Commands + task
// ---------------------------------------------------------------------------
static void handleCommands(uint32_t waitMs) {
    Cmd c;
    TickType_t wait = pdMS_TO_TICKS(waitMs);
    while (xQueueReceive(queue_, &c, wait) == pdTRUE) {
        wait = 0;
        switch (c.kind) {
            case Cmd::Connect: {
                xSemaphoreTake(mtx, portMAX_DELAY);
                bool ok = c.idx >= 0 && c.idx < nCands;
                if (ok) {
                    strlcpy(g_settings.obdAddr, cands[c.idx].addr, sizeof g_settings.obdAddr);
                    g_settings.obdAddrType = cands[c.idx].addrType;
                    strlcpy(state_.name, cands[c.idx].name[0] ? cands[c.idx].name : "OBD", sizeof state_.name);
                }
                xSemaphoreGive(mtx);
                if (ok) {
                    g_settings.obdEnabled = true;
                    g_settings.save();
                    wantReconnect_ = true;
                }
                break;
            }
            case Cmd::Forget:
                dropLink();
                g_settings.obdAddr[0] = 0;
                g_settings.save();
                xSemaphoreTake(mtx, portMAX_DELAY);
                state_ = State{};
                xSemaphoreGive(mtx);
                wantReconnect_ = true;
                break;
            case Cmd::Disconnect:
                dropLink();
                wantReconnect_ = true;
                break;
            case Cmd::Raw: {
                char r[96];
                bool ok = elm(c.text, r, sizeof r, 4000);
                Serial.printf("obd: %s -> %s%s\n", c.text, r[0] ? r : "(empty)", ok ? "" : "  [no prompt]");
                break;
            }
        }
    }
}

static void task(void*) {
    for (;;) {
        if (!g_settings.obdEnabled) {
            if (snapshot().link != Link::Disabled) { dropLink(); setLink(Link::Disabled); }
            handleCommands(500);
            continue;
        }
        if (!g_settings.obdAddr[0]) {
            if (snapshot().link != Link::Idle) { dropLink(); setLink(Link::Idle); }
            handleCommands(500);
            continue;
        }
        if (!presence::gateOpen()) {
            if (snapshot().link != Link::Standby) { dropLink(); setLink(Link::Standby); }
            handleCommands(500);
            continue;
        }

        setLink(Link::Connecting);
        wantReconnect_ = false;
        if (!connectAdapter()) {
            setLink(Link::Reconnecting);
            handleCommands(OBD_RECONNECT_BACKOFF_MS);
            continue;
        }
        xSemaphoreTake(mtx, portMAX_DELAY);
        strlcpy(state_.addr, g_settings.obdAddr, sizeof state_.addr);
        if (!state_.name[0]) strlcpy(state_.name, "OBD", sizeof state_.name);
        state_.link = Link::Connected;
        xSemaphoreGive(mtx);

        initElm();

        uint32_t fails = 0;
        while (!linkDropped_ && !wantReconnect_ && g_settings.obdEnabled && presence::gateOpen()) {
            uint32_t t0 = millis();
            State before = snapshot();
            pollOnce();
            State after = snapshot();
            fails = (after.errCount > before.errCount && after.okCount == before.okCount) ? fails + 1 : 0;
            if (fails >= 10) {
                // ECU may have gone to sleep (ignition off); re-run init so the
                // supported-PID map is refreshed when it comes back.
                ESP_LOGW(TAG, "no data for a while, re-initialising");
                initElm();
                fails = 0;
            }
            if (client_) {
                xSemaphoreTake(mtx, portMAX_DELAY);
                state_.rssi = (int8_t)client_->getRssi();
                xSemaphoreGive(mtx);
            }
            uint32_t elapsed = millis() - t0;
            handleCommands(elapsed < OBD_POLL_MS ? OBD_POLL_MS - elapsed : 1);
        }
        dropLink();
        setLink(Link::Reconnecting);
        handleCommands(OBD_RECONNECT_BACKOFF_MS);
    }
}

void begin() {
    mtx = xSemaphoreCreateMutex();
    rxSem = xSemaphoreCreateBinary();
    queue_ = xQueueCreate(8, sizeof(Cmd));
    xTaskCreatePinnedToCore(task, "obd_ble", 8192, nullptr, 2, nullptr, 0);
}

}  // namespace obd
