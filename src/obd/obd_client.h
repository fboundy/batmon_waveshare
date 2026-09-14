// ELM327-compatible OBD-II adapter over BLE (target: Veepeak OBDCheck BLE).
//
// These adapters expose a "BLE serial port": one characteristic to write
// ASCII ELM327 commands to, one that notifies the reply.  Which UUIDs is
// adapter-specific, so the client dumps the whole GATT table on connect and
// picks the pair from a table of known layouts, falling back to the first
// notify + first write characteristic it finds.  Everything it does is
// visible on the serial console (`obd log 1`, `obd send <cmd>`), because the
// first job with a new adapter is finding out what it answers.
// See docs/10-obd.md.
#pragma once

#include <stddef.h>
#include <stdint.h>

class NimBLEAdvertisedDevice;

namespace obd {

enum class Link : uint8_t { Disabled, Idle, Scanning, Connecting, Connected, Reconnecting, Standby };
const char* linkName(Link l);

struct Value {
    float    v = 0;
    uint32_t updatedMs = 0;
    bool valid() const { return updatedMs != 0; }
};

struct State {
    Link  link = Link::Disabled;
    char  name[32] = {0};
    char  addr[18] = {0};
    int8_t rssi = 0;
    char  elmVersion[40] = {0};   // ATI
    char  protocol[40] = {0};     // ATDPN / ATDP
    bool  ecuResponding = false;  // 0100 answered
    uint32_t supported[4] = {0};  // PID bitmaps from 0100 / 0120 / 0140 / 0160 (bit 31 = PID 01)
    uint32_t okCount = 0, errCount = 0;
    Value rpm, speedKph, coolantC, intakeC, ambientC, voltage, adapterVolts, fuelPct, loadPct, throttlePct, oilC, mafGs;
    char  lastRaw[96] = {0};      // last raw reply (for `obd send`)
};

// Adapters seen by the continuous scan that look like OBD dongles.
struct Candidate {
    char name[32];
    char addr[18];
    char services[80];   // advertised service UUIDs (diagnostics)
    uint8_t addrType;
    int8_t rssi;
    uint32_t seenMs;
};
constexpr int MAX_CANDIDATES = 6;

void begin();                 // starts the task; connects only if enabled + address known
State snapshot();

void setEnabled(bool on);     // persisted
void connectTo(int candidateIdx);   // remember + connect
void forget();                // drop the remembered adapter, disconnect
void disconnect();            // drop the link (stays enabled; reconnects after backoff)

int  candidateCount();
const Candidate& candidate(int i);

// Raw ELM327 command (without CR); reply lands in State::lastRaw and on the
// serial console.  Executed on the OBD task between polls.
void sendRaw(const char* cmd);
void setLog(bool on);         // echo every command/reply to serial
bool log();

// From the scan callback (NimBLE host task context).
void onAdvert(const NimBLEAdvertisedDevice* dev);

// Decoded list of supported PIDs into `out` ("0C 0D 05 ..."), returns count.
int supportedPids(const State& s, char* out, size_t outLen);

}  // namespace obd
