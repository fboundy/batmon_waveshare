#include "batmon_client.h"

#include <Arduino.h>
#include <math.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "../config.h"
#include "../history.h"
#include "../obd/obd_client.h"
#include "../presence.h"
#include "../settings.h"

static const char* TAG = "batmon";

namespace batmon {

Client g_client;

const char* linkStateName(LinkState s) {
    switch (s) {
        case LinkState::Idle:         return "Idle";
        case LinkState::Scanning:     return "Scanning";
        case LinkState::Connecting:   return "Connecting";
        case LinkState::Connected:    return "Connected";
        case LinkState::Reconnecting: return "Reconnecting";
        case LinkState::Paused:       return "Paused";
        case LinkState::Standby:      return "Standby";
    }
    return "?";
}

// ---------------------------------------------------------------------------
// Public API (called from the UI task)
// ---------------------------------------------------------------------------
void Client::begin() {
    mutex_ = xSemaphoreCreateMutex();
    candMutex_ = xSemaphoreCreateMutex();
    radioMutex_ = xSemaphoreCreateRecursiveMutex();
    queue_ = xQueueCreate(8, sizeof(Cmd));

    NimBLEDevice::init("BatMon Display");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    presence::begin();   // pairing service + security settings; needs init() first

    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setScanCallbacks(this, true);   // every advert, not just the first per device
    scan->setActiveScan(true);            // we want the scan response (names)
    scan->setInterval(80);
    scan->setWindow(30);
    scan->setMaxResults(0);               // callbacks only, keep no list
    scan->setDuplicateFilter(false);
    startScan();

    xTaskCreatePinnedToCore(taskEntry, "batmon_ble", 8192, this, 2, nullptr, 0);
}

State Client::snapshot() {
    State s;
    xSemaphoreTake((SemaphoreHandle_t)mutex_, portMAX_DELAY);
    s = state_;
    xSemaphoreGive((SemaphoreHandle_t)mutex_);
    return s;
}

void Client::requestSetIo(IoType io, bool on) {
    Cmd c{Cmd::SetIo, io, on, 0};
    xQueueSend((QueueHandle_t)queue_, &c, 0);
}

void Client::pause(uint32_t ms) {
    Cmd c{Cmd::Pause, IoType::Relay, false, ms};
    xQueueSend((QueueHandle_t)queue_, &c, 0);
}

void Client::resume() {
    Cmd c{Cmd::Resume, IoType::Relay, false, 0};
    xQueueSend((QueueHandle_t)queue_, &c, 0);
}

void Client::forgetDevice() {
    Cmd c{Cmd::Forget, IoType::Relay, false, 0};
    xQueueSend((QueueHandle_t)queue_, &c, 0);
}

void Client::reconnect() {
    Cmd c{Cmd::Reconnect, IoType::Relay, false, 0};
    xQueueSend((QueueHandle_t)queue_, &c, 0);
}

// ---------------------------------------------------------------------------
// State helpers
// ---------------------------------------------------------------------------
void Client::setLink(LinkState s) {
    xSemaphoreTake((SemaphoreHandle_t)mutex_, portMAX_DELAY);
    state_.link = s;
    xSemaphoreGive((SemaphoreHandle_t)mutex_);
}

void Client::onConnect(NimBLEClient*) {
    ESP_LOGI(TAG, "connected");
}

void Client::onDisconnect(NimBLEClient*, int reason) {
    ESP_LOGW(TAG, "disconnected, reason=%d", reason);
    linkDropped_ = true;
}

// ---------------------------------------------------------------------------
// Discovery
// ---------------------------------------------------------------------------
static bool isBatMon(const NimBLEAdvertisedDevice* d) {
    if (d->isAdvertisingService(NimBLEUUID(ADV_SERVICE_UUID))) return true;
    std::string md = d->getManufacturerData();
    if (md.size() >= 2) {
        uint16_t company = (uint8_t)md[0] | ((uint8_t)md[1] << 8);
        if (company == MANUFACTURER_ID) return true;
    }
    if (d->haveName() && d->getName().rfind(NAME_PREFIX, 0) == 0) return true;
    return false;
}

void Client::startScan() {
    if (scanning_) return;
    // Someone else (an OBD connect attempt) owns the radio: they restart the
    // scan when they are done, don't block a poll loop waiting for it.
    if (xSemaphoreTakeRecursive((SemaphoreHandle_t)radioMutex_, 0) != pdTRUE) return;
    scanning_ = NimBLEDevice::getScan()->start(0, false, true);   // 0 = until stopped
    if (!scanning_) ESP_LOGW(TAG, "scan start failed");
    xSemaphoreGiveRecursive((SemaphoreHandle_t)radioMutex_);
}

void Client::stopScan() {
    xSemaphoreTakeRecursive((SemaphoreHandle_t)radioMutex_, portMAX_DELAY);
    if (scanning_) {
        NimBLEDevice::getScan()->stop();
        scanning_ = false;
    }
    xSemaphoreGiveRecursive((SemaphoreHandle_t)radioMutex_);
}

bool Client::scanWanted() const {
    return presence::anyPaired() || g_settings.obdEnabled;
}

void Client::radioAcquire() {
    xSemaphoreTakeRecursive((SemaphoreHandle_t)radioMutex_, portMAX_DELAY);
    stopScan();
}

void Client::radioRelease() {
    if (scanWanted()) startScan();
    xSemaphoreGiveRecursive((SemaphoreHandle_t)radioMutex_);
}

// NimBLE host task: every advertisement report.
void Client::onResult(const NimBLEAdvertisedDevice* d) {
    std::string mfg = d->getManufacturerData();
    presence::onAdvert(d->getAddress(), d->getRSSI(), (const uint8_t*)mfg.data(), mfg.size());
    if (g_settings.obdEnabled) obd::onAdvert(d);

    if (!isBatMon(d)) return;
    ESP_LOGD(TAG, "BatMon adv %s rssi=%d name='%s'", d->getAddress().toString().c_str(),
             d->getRSSI(), d->haveName() ? d->getName().c_str() : "");

    bool havePreferred = g_settings.deviceAddr[0] != 0;
    if (havePreferred && strcasecmp(d->getAddress().toString().c_str(), g_settings.deviceAddr) != 0) return;

    xSemaphoreTake((SemaphoreHandle_t)candMutex_, portMAX_DELAY);
    // Prefer the strongest signal seen in the last few seconds; a named
    // report (scan response) refreshes the same device.
    bool same = cand_.valid && cand_.addr == d->getAddress();
    bool replace = !cand_.valid || same || d->getRSSI() > cand_.rssi || millis() - cand_.seenMs > 3000;
    if (replace) {
        if (!same || d->haveName()) cand_.name = d->getName();
        cand_.valid = true;
        cand_.addr = d->getAddress();
        cand_.rssi = d->getRSSI();
        cand_.seenMs = millis();
    }
    xSemaphoreGive((SemaphoreHandle_t)candMutex_);
}

void Client::onScanEnd(const NimBLEScanResults&, int reason) {
    scanning_ = false;   // stopped by us, by connect(), or by the stack
}

bool Client::takeCandidate(NimBLEAddress& addr, std::string& name, int& rssi) {
    xSemaphoreTake((SemaphoreHandle_t)candMutex_, portMAX_DELAY);
    bool ok = cand_.valid && millis() - cand_.seenMs < 10000;
    if (ok) {
        addr = cand_.addr;
        name = cand_.name;
        rssi = cand_.rssi;
    }
    cand_.valid = false;
    xSemaphoreGive((SemaphoreHandle_t)candMutex_);
    return ok;
}

// ---------------------------------------------------------------------------
// Connection
// ---------------------------------------------------------------------------
bool Client::connectTo(const NimBLEAddress& addr) {
    if (!client_) {
        client_ = NimBLEDevice::createClient();
        client_->setClientCallbacks(this, false);
        client_->setConnectTimeout(BLE_CONNECT_TIMEOUT_MS);
        // 15..30 ms interval keeps the write/read round trips snappy.
        client_->setConnectionParams(12, 24, 0, 400);
    }
    linkDropped_ = false;
    radioAcquire();   // the controller can't initiate a connection while scanning
    bool ok = client_->connect(addr, true, false, true);
    if (!ok) ESP_LOGW(TAG, "connect() failed");
    if (ok && !findCharacteristics()) {
        client_->disconnect();
        ok = false;
    }
    // Keep scanning while connected only if presence / OBD discovery need it.
    radioRelease();
    if (!ok) startScan();
    return ok;
}

void Client::disconnect() {
    chrSensor_ = nullptr;
    chrApi_ = nullptr;
    if (client_ && client_->isConnected()) client_->disconnect();
}

bool Client::findCharacteristics() {
    // The HA integration addresses the characteristics by UUID only, so the
    // enclosing service UUID is not documented.  Walk every service and log
    // the table once so it ends up in the docs.
    chrSensor_ = nullptr;
    chrApi_ = nullptr;
    const std::vector<NimBLERemoteService*>& services = client_->getServices(true);
    for (NimBLERemoteService* svc : services) {
        ESP_LOGI(TAG, "service %s", svc->getUUID().toString().c_str());
        const std::vector<NimBLERemoteCharacteristic*>& chrs = svc->getCharacteristics(true);
        for (NimBLERemoteCharacteristic* ch : chrs) {
            ESP_LOGI(TAG, "   chr %s r=%d w=%d n=%d", ch->getUUID().toString().c_str(),
                     ch->canRead(), ch->canWrite(), ch->canNotify());
            if (ch->getUUID() == NimBLEUUID(UUID_SENSOR_CMD)) chrSensor_ = ch;
            if (ch->getUUID() == NimBLEUUID(UUID_DEVICE_API)) chrApi_ = ch;
        }
    }
    if (!chrSensor_) {
        ESP_LOGE(TAG, "sensor characteristic %s not found", UUID_SENSOR_CMD);
        return false;
    }
    if (!chrApi_) ESP_LOGW(TAG, "device API characteristic not found; relay control disabled");
    return true;
}

// ---------------------------------------------------------------------------
// Polling
// ---------------------------------------------------------------------------
bool Client::readSensor(SensorType type, Mode mode, SensorReply& out) {
    if (!chrSensor_ || linkDropped_) return false;
    uint8_t req[SENSOR_REQ_LEN];
    encodeSensorRequest(type, mode, req);
    if (!chrSensor_->writeValue(req, sizeof req, true)) {
        ESP_LOGW(TAG, "write %s/%d failed", sensorName(type), (int)mode);
        return false;
    }
    NimBLEAttValue v = chrSensor_->readValue();
    if (!decodeSensorReply(v.data(), v.size(), out)) {
        ESP_LOGW(TAG, "bad reply for %s/%d (%u bytes)", sensorName(type), (int)mode, (unsigned)v.size());
        return false;
    }
    if (out.type != type) {
        ESP_LOGW(TAG, "reply type mismatch: asked %d got %d", (int)type, (int)out.type);
        return false;
    }
    return true;
}

static void store(Reading& r, const SensorReply& rep) {
    r.value = rep.value;
    r.updatedMs = millis();
}

bool Client::pollFast() {
    SensorReply rep;
    bool ok = true;
    Reading v, i, t, ah, ev, sw;
    if (readSensor(SensorType::BatVolts, Mode::Value, rep))    store(v, rep);  else ok = false;
    if (readSensor(SensorType::BatCurrent, Mode::Value, rep))  store(i, rep);  else ok = false;
    if (readSensor(SensorType::ExtTemp, Mode::Value, rep))     store(t, rep);  else ok = false;
    if (readSensor(SensorType::BatAmpHours, Mode::Value, rep)) store(ah, rep); else ok = false;
    if (readSensor(SensorType::ExtVolts, Mode::Value, rep))    store(ev, rep); else ok = false;
    if (readSensor(SensorType::SwitchPin, Mode::Value, rep))   store(sw, rep); else ok = false;

    xSemaphoreTake((SemaphoreHandle_t)mutex_, portMAX_DELAY);
    if (v.valid())  state_.volts = v;
    if (i.valid())  state_.current = i;
    if (t.valid())  state_.extTemp = t;
    if (ah.valid()) state_.ampHours = ah;
    if (ev.valid()) state_.extVolts = ev;
    if (sw.valid()) state_.sw = sw;
    if (ok) state_.pollOk++; else state_.pollErrors++;
    if (client_) state_.rssi = client_->getRssi();
    xSemaphoreGive((SemaphoreHandle_t)mutex_);
    return ok;
}

bool Client::pollSlow() {
    SensorReply rep;
    bool ok = true;
    Reading ahMax, ahMin, it, rl;
    if (readSensor(SensorType::BatAmpHours, Mode::Max, rep))   store(ahMax, rep); else ok = false;
    if (readSensor(SensorType::BatAmpHours, Mode::Min, rep))   store(ahMin, rep); else ok = false;
    if (readSensor(SensorType::IntTemp, Mode::Value, rep))     store(it, rep);    else ok = false;
    if (readSensor(SensorType::RelayPin, Mode::Value, rep))    store(rl, rep);    else ok = false;

    xSemaphoreTake((SemaphoreHandle_t)mutex_, portMAX_DELAY);
    if (ahMax.valid()) state_.ampHoursMax = ahMax;
    if (ahMin.valid()) state_.ampHoursMin = ahMin;
    if (it.valid())    state_.intTemp = it;
    if (rl.valid())    state_.relay = rl;
    xSemaphoreGive((SemaphoreHandle_t)mutex_);
    return ok;
}

void Client::computeDerived() {
    xSemaphoreTake((SemaphoreHandle_t)mutex_, portMAX_DELAY);
    State& s = state_;
    s.watts = (s.volts.valid() && s.current.valid()) ? s.volts.value * s.current.value : 0;

    float cap = g_settings.capacityAh;
    if (cap > 0 && s.ampHours.valid()) {
        float maxAh = s.ampHoursMax.valid() ? s.ampHoursMax.value : 0;
        s.soc = stateOfCharge(s.ampHours.value, maxAh, cap);
        // Runtime estimate from the instantaneous current.
        float remainingAh = s.soc / 100.0f * cap;
        if (s.current.valid() && s.current.value < -0.05f) {
            s.hoursRemaining = remainingAh / -s.current.value;
        } else if (s.current.valid() && s.current.value > 0.05f) {
            s.hoursRemaining = (cap - remainingAh) / s.current.value;
        } else {
            s.hoursRemaining = -1;
        }
    } else {
        s.soc = -1;
        s.hoursRemaining = -1;
    }
    history::Sample hs;
    hs.mainV   = s.volts.valid()    ? s.volts.value    : NAN;
    hs.auxV    = s.extVolts.valid() ? s.extVolts.value : NAN;
    hs.soc     = s.soc >= 0         ? s.soc            : NAN;
    hs.current = s.current.valid()  ? s.current.value  : NAN;
    xSemaphoreGive((SemaphoreHandle_t)mutex_);
    history::push(hs);
}

bool Client::execSetIo(IoType io, bool on) {
    if (!chrApi_ || linkDropped_) return false;
    uint8_t buf[API_SET_IO_LEN];
    encodeSetIo(io, on, buf);
    if (!chrApi_->writeValue(buf, sizeof buf, true)) {
        ESP_LOGW(TAG, "set io write failed");
        return false;
    }
    // Read back the pin so the UI reflects reality, as the HA switch does.
    SensorReply rep;
    SensorType pin = (io == IoType::Relay) ? SensorType::RelayPin : SensorType::SwitchPin;
    if (readSensor(pin, Mode::Value, rep)) {
        xSemaphoreTake((SemaphoreHandle_t)mutex_, portMAX_DELAY);
        store(io == IoType::Relay ? state_.relay : state_.sw, rep);
        xSemaphoreGive((SemaphoreHandle_t)mutex_);
    }
    return true;
}

// Drains the command queue, blocking up to waitMs for the first item.
void Client::handleCommands(uint32_t waitMs) {
    Cmd c;
    TickType_t wait = pdMS_TO_TICKS(waitMs);
    while (xQueueReceive((QueueHandle_t)queue_, &c, wait) == pdTRUE) {
        wait = 0;
        switch (c.kind) {
            case Cmd::SetIo:
                execSetIo(c.io, c.on);
                break;
            case Cmd::Pause:
                disconnect();
                xSemaphoreTake((SemaphoreHandle_t)mutex_, portMAX_DELAY);
                state_.pauseUntilMs = millis() + c.ms;
                state_.link = LinkState::Paused;
                xSemaphoreGive((SemaphoreHandle_t)mutex_);
                break;
            case Cmd::Resume:
                xSemaphoreTake((SemaphoreHandle_t)mutex_, portMAX_DELAY);
                state_.pauseUntilMs = 0;
                xSemaphoreGive((SemaphoreHandle_t)mutex_);
                wantReconnect_ = true;
                break;
            case Cmd::Forget:
                g_settings.deviceAddr[0] = 0;
                g_settings.save();
                wantReconnect_ = true;
                break;
            case Cmd::Reconnect:
                wantReconnect_ = true;
                break;
        }
    }
}

// ---------------------------------------------------------------------------
// Main loop
// ---------------------------------------------------------------------------
void Client::taskEntry(void* arg) {
    static_cast<Client*>(arg)->task();
}

void Client::task() {
    for (;;) {
        history::tick();

        // ---- standby: paired phones exist but none is here ----
        if (!presence::gateOpen()) {
            if (snapshot().link != LinkState::Standby) {
                ESP_LOGI(TAG, "no phone present: releasing BatMon");
                disconnect();
                setLink(LinkState::Standby);
            }
            startScan();            // presence needs the scan running
            handleCommands(500);
            continue;
        }

        // ---- paused? ----
        State s = snapshot();
        if (s.link == LinkState::Paused) {
            if ((int32_t)(millis() - s.pauseUntilMs) < 0 && !wantReconnect_) {
                handleCommands(500);
                continue;
            }
            wantReconnect_ = false;
            xSemaphoreTake((SemaphoreHandle_t)mutex_, portMAX_DELAY);
            state_.pauseUntilMs = 0;
            xSemaphoreGive((SemaphoreHandle_t)mutex_);
        }

        // ---- scan: wait for the continuous scan to report a BatMon ----
        setLink(LinkState::Scanning);
        startScan();
        NimBLEAddress addr;
        std::string name;
        int rssi = 0;
        bool found = false;
        uint32_t tScan = millis();
        while (!found && millis() - tScan < BLE_SCAN_MS && presence::gateOpen()) {
            handleCommands(250);
            found = takeCandidate(addr, name, rssi);
        }
        if (!found) {
            ESP_LOGI(TAG, "no BatMon found");
            handleCommands(BLE_RECONNECT_BACKOFF_MS);
            continue;
        }

        // ---- connect ----
        setLink(LinkState::Connecting);
        ESP_LOGI(TAG, "connecting to %s '%s'", addr.toString().c_str(), name.c_str());
        if (!connectTo(addr)) {
            setLink(LinkState::Reconnecting);
            handleCommands(BLE_RECONNECT_BACKOFF_MS);
            continue;
        }

        // Remember it as the preferred device.
        if (g_settings.deviceAddr[0] == 0) {
            strlcpy(g_settings.deviceAddr, addr.toString().c_str(), sizeof g_settings.deviceAddr);
            g_settings.deviceAddrType = addr.getType();
            g_settings.save();
        }

        xSemaphoreTake((SemaphoreHandle_t)mutex_, portMAX_DELAY);
        const char* n = name.c_str();
        if (strncmp(n, NAME_PREFIX, strlen(NAME_PREFIX)) == 0) n += strlen(NAME_PREFIX);
        strlcpy(state_.deviceName, n[0] ? n : "BatMon", sizeof state_.deviceName);
        strlcpy(state_.deviceAddr, addr.toString().c_str(), sizeof state_.deviceAddr);
        state_.rssi = rssi;
        state_.connectedSinceMs = millis();
        state_.link = LinkState::Connected;
        xSemaphoreGive((SemaphoreHandle_t)mutex_);

        // ---- poll until the link drops ----
        lastSlowPollMs_ = 0;
        wantReconnect_ = false;
        uint32_t consecutiveFail = 0;
        while (!linkDropped_ && !wantReconnect_ && presence::gateOpen()) {
            history::tick();
            if (scanWanted()) startScan();   // a phone/OBD may have been enabled meanwhile
            uint32_t t0 = millis();
            bool ok = pollFast();
            if (ok && (lastSlowPollMs_ == 0 || millis() - lastSlowPollMs_ >= BLE_POLL_SLOW_MS)) {
                pollSlow();
                lastSlowPollMs_ = millis();
            }
            computeDerived();
            consecutiveFail = ok ? 0 : consecutiveFail + 1;
            if (consecutiveFail >= 5) {
                ESP_LOGW(TAG, "too many poll failures, reconnecting");
                break;
            }
            uint32_t elapsed = millis() - t0;
            uint32_t period = g_settings.pollMs ? g_settings.pollMs : BLE_POLL_FAST_MS;
            handleCommands(elapsed < period ? period - elapsed : 1);
            if (snapshot().link == LinkState::Paused) break;
        }

        if (snapshot().link == LinkState::Paused) continue;
        disconnect();
        setLink(LinkState::Reconnecting);
        handleCommands(BLE_RECONNECT_BACKOFF_MS);
    }
}

}  // namespace batmon
