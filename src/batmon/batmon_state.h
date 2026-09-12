// Snapshot of everything the UI needs, produced by the BLE task and consumed
// by the UI task.  Copied under a mutex; no pointers inside.
#pragma once

#include <stdint.h>

namespace batmon {

enum class LinkState : uint8_t {
    Idle,
    Scanning,
    Connecting,
    Connected,
    Reconnecting,   // lost link, backing off before rescan
    Paused,         // user asked us to release the BatMon (phone app)
    Standby,        // no paired phone present: link released, screen off
};

const char* linkStateName(LinkState s);

struct Reading {
    float    value = 0;
    uint32_t updatedMs = 0;   // millis() of last successful read, 0 = never
    bool valid() const { return updatedMs != 0; }
};

struct State {
    LinkState link = LinkState::Idle;
    char      deviceName[32] = {0};   // without the "BK-" prefix
    char      deviceAddr[18] = {0};
    int8_t    rssi = 0;
    uint32_t  connectedSinceMs = 0;
    uint32_t  pollErrors = 0;
    uint32_t  pollOk = 0;
    uint32_t  pauseUntilMs = 0;

    // Fast-polled
    Reading volts;
    Reading current;      // +charge / -discharge
    Reading extTemp;
    Reading ampHours;

    // Slow-polled
    Reading ampHoursMax;  // "full" reference for SoC
    Reading ampHoursMin;
    Reading extVolts;
    Reading intTemp;
    Reading relay;        // 0 / 1
    Reading sw;           // 0 / 1

    // Derived (computed by the BLE task after each fast poll)
    float watts = 0;
    float soc = -1;          // -1 = capacity not configured
    float hoursRemaining = -1;   // time to empty (discharging) or to full (charging)
};

}  // namespace batmon
