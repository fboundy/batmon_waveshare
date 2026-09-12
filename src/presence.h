// Phone / beacon presence.
//
// A phone is paired once (it bonds with a small GATT service we advertise
// while "pairing" is active); that gives us its Identity Resolving Key.
// From then on the continuous scan resolves the phone's rotating private
// addresses back to that identity, so we know when it is nearby without it
// ever connecting again.  See docs/03-architecture.md "Phone presence".
//
// iBeacon tags (e.g. DX-CP27) are supported too: matched by their UUID
// (+ optional major/minor) rather than an IRK; otherwise treated exactly
// like a phone.
//
// Gate: if at least one phone/beacon is paired and none is present, the
// display goes to standby (screen off) and the BatMon link is released.
#pragma once

#include <stddef.h>
#include <stdint.h>

class NimBLEAddress;

namespace presence {

constexpr int MAX_PHONES = 8;

struct Phone {
    bool     isBeacon;      // iBeacon tag instead of a bonded phone
    char     addr[18];      // phone: identity address
    uint8_t  addrType;
    char     name[16];
    uint8_t  irk[16];       // phone: identity resolving key
    bool     hasIrk;
    uint8_t  uuid[16];      // beacon: iBeacon proximity UUID
    uint16_t major, minor;  // beacon: 0xFFFF = any
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

// From the scan callback (NimBLE host task context).  mfg/mfgLen: the
// advertisement's manufacturer-specific data (for iBeacon parsing).
void onAdvert(const NimBLEAddress& addr, int rssi, const uint8_t* mfg, size_t mfgLen);

// Pairing window: advertise as "BatMon Display" for `ms`.
void     startPairing(uint32_t ms);
void     stopPairing();
bool     pairing();
uint32_t pairingRemainingMs();

bool forget(int i);
void forgetAll();
bool rename(int i, const char* name);

// ---- iBeacon tags ----
struct BeaconCandidate {
    uint8_t  uuid[16];
    uint16_t major, minor;
    int8_t   rssi;
    uint32_t seenMs;
};
constexpr int MAX_CANDIDATES = 6;
// Collect nearby iBeacons for `ms` (they show up in candidates()).
void startBeaconScan(uint32_t ms);
bool beaconScanning();
int  candidateCount();
const BeaconCandidate& candidate(int i);
// Add candidate i as a paired entry; returns its index in the list or -1.
int  addBeacon(int candidateIdx, const char* name);
// Add by UUID (32 hex digits, dashes ignored); major/minor 0xFFFF = any.
int  addBeacon(const char* uuidHex, uint16_t major, uint16_t minor, const char* name);
void formatUuid(const uint8_t uuid[16], char out[37]);

// Presence transitions + relay-follow + pairing timeout.  Call from loop().
void tick();

}  // namespace presence
