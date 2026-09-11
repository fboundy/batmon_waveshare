// BLE central that finds a BatMon, stays connected and polls it.
//
// Runs in its own FreeRTOS task (NimBLE calls block).  The UI reads a copy of
// the State via snapshot() and sends control requests through the small
// command API below.  See docs/03-architecture.md.
#pragma once

#include <NimBLEDevice.h>

#include "batmon_protocol.h"
#include "batmon_state.h"

namespace batmon {

class Client : public NimBLEClientCallbacks {
public:
    void begin();

    // Thread-safe copy of the latest state.
    State snapshot();

    // Queue a relay / switch change; executed on the BLE task.
    void requestSetIo(IoType io, bool on);

    // Drop the connection for `ms` so the phone app can get in (BatMon only
    // accepts one central at a time).  resume() cancels early.
    void pause(uint32_t ms);
    void resume();

    // Forget the preferred device and reconnect to whatever is found next.
    void forgetDevice();

    // Force a disconnect + rescan (e.g. after settings change).
    void reconnect();

private:
    struct Cmd {
        enum Kind : uint8_t { SetIo, Pause, Resume, Forget, Reconnect } kind;
        IoType io;
        bool on;
        uint32_t ms;
    };

    static void taskEntry(void* arg);
    void task();

    bool scanAndPick(NimBLEAddress& addr, std::string& name, int& rssi);
    bool connectTo(const NimBLEAddress& addr);
    void disconnect();
    bool findCharacteristics();

    bool readSensor(SensorType type, Mode mode, SensorReply& out);
    bool pollFast();
    bool pollSlow();
    void computeDerived();
    bool execSetIo(IoType io, bool on);
    void handleCommands(uint32_t waitMs);

    void setLink(LinkState s);

    // NimBLEClientCallbacks
    void onConnect(NimBLEClient* c) override;
    void onDisconnect(NimBLEClient* c, int reason) override;

    NimBLEClient* client_ = nullptr;
    NimBLERemoteCharacteristic* chrSensor_ = nullptr;
    NimBLERemoteCharacteristic* chrApi_ = nullptr;

    State state_;
    void* mutex_ = nullptr;   // SemaphoreHandle_t
    void* queue_ = nullptr;   // QueueHandle_t
    volatile bool linkDropped_ = false;
    bool wantReconnect_ = false;
    uint32_t lastSlowPollMs_ = 0;
};

extern Client g_client;

}  // namespace batmon
