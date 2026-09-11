#include "batmon_protocol.h"

#include <string.h>

namespace batmon {

// All multi-byte fields on the wire are little-endian.  The HA code's
// popFlt() looks big-endian at first glance, but it re-packs the 32-bit
// word with native (LE) byte order before unpacking as big-endian, which
// nets out to little-endian.  See docs/02-ble-protocol.md (Byte order).
static uint32_t rdU32LE(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static float rdF32LE(const uint8_t* p) {
    uint32_t u = rdU32LE(p);
    float f;
    memcpy(&f, &u, sizeof f);
    return f;
}

static void wrI32LE(uint8_t* p, int32_t v) {
    uint32_t u = (uint32_t)v;
    p[0] = u & 0xff;
    p[1] = (u >> 8) & 0xff;
    p[2] = (u >> 16) & 0xff;
    p[3] = (u >> 24) & 0xff;
}

void encodeSensorRequest(SensorType type, Mode mode, uint8_t out[SENSOR_REQ_LEN]) {
    out[0] = (uint8_t)type;
    out[1] = (uint8_t)mode;
    out[2] = 0;  // len: always 0 for a request
}

bool decodeSensorReply(const uint8_t* data, size_t len, SensorReply& out) {
    out = SensorReply{};
    if (!data || len < 3) return false;
    out.type = (SensorType)data[0];
    out.mode = (Mode)data[1];
    out.len  = data[2];

    const uint8_t* p = data + 3;
    size_t avail = len - 3;

    switch (out.mode) {
        case Mode::Value:
            if (avail < 4) return false;
            out.value = rdF32LE(p);
            out.valid = true;
            return true;
        case Mode::Min:
        case Mode::Max:
            if (avail < 4) return false;
            out.value = rdF32LE(p);
            // Epoch is not used by the HA integration; tolerate its absence.
            out.epoch = (avail >= 8) ? rdU32LE(p + 4) : 0;
            out.valid = true;
            return true;
        default:
            // Unknown mode: header only.
            out.valid = false;
            return false;
    }
}

void encodeSetIo(IoType io, bool on, uint8_t out[API_SET_IO_LEN]) {
    out[0] = API_SET_IO & 0xff;
    out[1] = (API_SET_IO >> 8) & 0xff;
    out[2] = 0x01;                       // arg tag: int32 follows
    wrI32LE(out + 3, (int32_t)io);
    out[7] = 0x01;                       // arg tag: int32 follows
    wrI32LE(out + 8, on ? 1 : 0);
    out[12] = 0x00;                      // terminator
}

float stateOfCharge(float ampHours, float maxAmpHours, float capacityAh) {
    if (!(capacityAh > 0.0f)) return -1.0f;
    float ref = (maxAmpHours > 0.0f) ? maxAmpHours : 0.0f;  // HA: tmp_ah
    float soc = 100.0f + ((ampHours - ref) / capacityAh) * 100.0f;
    if (soc < 0.0f) soc = 0.0f;
    if (soc > 100.0f) soc = 100.0f;
    return soc;
}

const char* sensorName(SensorType t) {
    switch (t) {
        case SensorType::BatVolts:    return "Voltage";
        case SensorType::ExtVolts:    return "External Voltage";
        case SensorType::IntTemp:     return "CPU Temperature";
        case SensorType::ExtTemp:     return "External Temperature";
        case SensorType::BatCurrent:  return "Current";
        case SensorType::BatAmpHours: return "Amp Hours";
        case SensorType::RelayPin:    return "Relay";
        case SensorType::SwitchPin:   return "Switch";
    }
    return "?";
}

}  // namespace batmon
