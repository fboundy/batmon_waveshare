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

    void load();
    void save() const;
    void reset();
};

extern Settings g_settings;
