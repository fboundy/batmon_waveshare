// Phone presence via BLE bonding.
//
// A phone is paired once (it bonds with a small GATT service we advertise
// while "pairing" is active); that gives us its Identity Resolving Key.
// From then on the continuous scan resolves the phone's rotating private
// addresses back to that identity, so we know when it is nearby without it
// ever connecting again.  See docs/03-architecture.md "Phone presence".
//
// Gate: if at least one phone is paired and none is present, the display
// goes to standby (screen off) and the BatMon link is released.
#pragma once

#include <stdint.h>

class NimBLEAddress;

namespace presence {

constexpr int MAX_PHONES = 8;

struct Phone {
    char     addr[18];      // identity address
    uint8_t  addrType;
    char     name[16];
    uint8_t  irk[16];
    bool     hasIrk;
    uint32_t lastSeenMs;    // 0 = never
    uint32_t firstSeenMs;   // first sighting of the current "return" (debounce)
    int8_t   rssi;
};

// Call after NimBLEDevice::init().  Creates the pairing service, loads the
// bond list and the names.
void begin();

int          count();
const Phone& phone(int i);
bool         anyPaired();
bool         present(int i);
bool         anyPresent();
// Index of the strongest present phone, or -1.
int          nearestPresent();

// True when the BatMon link and the screen should be active:
// no phones paired, or a phone present, or still within the boot grace
// period, or within the short hold after the last phone left (so the
// relay-off command can go out first).
bool gateOpen();

// From the scan callback (NimBLE host task context).
void onAdvert(const NimBLEAddress& addr, int rssi);

// Pairing window: advertise as "BatMon Display" for `ms`.
void     startPairing(uint32_t ms);
void     stopPairing();
bool     pairing();
uint32_t pairingRemainingMs();

bool forget(int i);
void forgetAll();
bool rename(int i, const char* name);

// Presence transitions + relay-follow + pairing timeout.  Call from loop().
void tick();

}  // namespace presence
