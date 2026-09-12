// User settings persisted in NVS (Arduino Preferences, namespace "batmon").
#pragma once

#include <stdint.h>

struct Settings {
    // Battery bank size in Ah; 0 = unknown (SoC not shown).
    float capacityAh = 0;
    // Preferred BatMon.  Empty = connect to the first one seen.
    char  deviceAddr[18] = {0};
    uint8_t deviceAddrType = 0;
    // Display
    uint8_t brightness = 80;     // 0..100
    bool    fahrenheit = false;
    // BLE polling period for the fast group (ms)
    uint16_t pollMs = 1000;
    // Chart page: bit0 main V, bit1 aux V, bit2 SoC, bit3 current; range 0..3
    uint8_t chartMask = 0x01;
    uint8_t chartRange = 0;
    // Relay on when a paired phone arrives, off when the last one leaves.
    bool relayFollowsPhone = false;
    // A phone counts as present this long after its last advertisement.
    uint16_t presenceTimeoutS = 90;

    void load();
    void save() const;
    void reset();
};

extern Settings g_settings;
