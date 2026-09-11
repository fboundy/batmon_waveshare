// BatMon BLE wire protocol — encode/decode only, no BLE dependency.
//
// Reverse engineered from the BatMon Home Assistant integration
// (https://github.com/ringonotts/batmon_ha, custom_components/batmon_bm/batmon.py).
// See docs/02-ble-protocol.md for the full write-up.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace batmon {

// ---- Discovery -------------------------------------------------------------
// Advertised 128-bit service UUID and Bluetooth SIG manufacturer ID.
constexpr const char* ADV_SERVICE_UUID = "00000000-cc7a-482a-984a-7f2ed5b3e58f";
constexpr uint16_t    MANUFACTURER_ID  = 4077;  // 0x0FED
// Device names are "BK-<name>"; the app strips the prefix.
constexpr const char* NAME_PREFIX = "BK-";

// ---- GATT characteristics --------------------------------------------------
// Write a 3-byte request, then read the same characteristic for the reply.
constexpr const char* UUID_SENSOR_CMD = "00000303-8e22-4541-9d4c-21edae82ed19";
// Write-only "device API" RPC characteristic (relay / switch control).
constexpr const char* UUID_DEVICE_API = "00000105-8e22-4541-9d4c-21edae82ed19";

// ---- Sensor request ----------------------------------------------------------
enum class SensorType : uint8_t {
    BatVolts    = 0,
    ExtVolts    = 1,
    IntTemp     = 2,  // CPU / board temperature (degC)
    ExtTemp     = 3,  // external thermistor lead (degC)
    BatCurrent  = 4,  // amps, +charge / -discharge
    BatAmpHours = 5,  // coulomb counter (Ah)
    RelayPin    = 6,  // 0/1
    SwitchPin   = 7,  // 0/1
};

enum class Mode : uint8_t {
    Value       = 0,
    Min         = 1,
    Max         = 2,
    LinEqu      = 20,  // present in the HA enum, semantics unknown
    TempCo      = 21,
    Threshold   = 22,
    ResetMinMax = 23,
};

constexpr size_t SENSOR_REQ_LEN = 3;

// Builds the 3-byte request written to UUID_SENSOR_CMD: [type][mode][len=0].
void encodeSensorRequest(SensorType type, Mode mode, uint8_t out[SENSOR_REQ_LEN]);

// Decoded reply from UUID_SENSOR_CMD.
struct SensorReply {
    SensorType type;
    Mode       mode;
    uint8_t    len;
    float      value;     // Mode::Value / Min / Max
    uint32_t   epoch;     // Min / Max only: unix time of the extreme
    bool       valid;
};

// Parses a reply. Returns false (and valid=false) on short / malformed data.
bool decodeSensorReply(const uint8_t* data, size_t len, SensorReply& out);

// ---- Device API (RPC) ----------------------------------------------------------
enum class IoType : int32_t {
    Relay  = 2,
    Switch = 3,
};

constexpr uint16_t API_SET_IO = 606;
constexpr size_t   API_SET_IO_LEN = 2 + 1 + 4 + 1 + 4 + 1;  // 13 bytes

// Builds the payload written to UUID_DEVICE_API to drive the relay or the
// switch output: [u16 api=606][0x01][i32 io_type][0x01][i32 on][0x00].
void encodeSetIo(IoType io, bool on, uint8_t out[API_SET_IO_LEN]);

// ---- Derived values ----------------------------------------------------------
// State of charge exactly as the HA integration computes it:
//   100 + ((amp_hours - max_amp_hours) / capacity_ah) * 100
// where max_amp_hours is the Mode::Max reading of BatAmpHours (the "full"
// reference) and capacity_ah is the user-configured bank size.
// Result is clamped to 0..100.  Returns -1 if capacity is not positive.
float stateOfCharge(float ampHours, float maxAmpHours, float capacityAh);

// Human-readable name (matches HA entity names where possible).
const char* sensorName(SensorType t);

}  // namespace batmon
