// Diagnostics log: a small text log (GATT tables, ELM327 replies, ...) that
// survives reboots in LittleFS (/diag.txt) and can be fetched without a PC:
//   * on screen (OBD page -> Log)
//   * on the serial console (`obd diag`)
//   * over BLE from any phone with nRF Connect / LightBlue: characteristic
//     b47a0003-... on the "BatMon Display" service (advertised while the
//     pairing window is open).  Subscribe to notifications to have the
//     whole log streamed in chunks, or read repeatedly for 500-byte pages.
#pragma once

#include <stddef.h>
#include <stdint.h>

class NimBLEService;

namespace diag {

constexpr size_t MAX_BYTES = 16 * 1024;

void begin();                              // after LittleFS is mounted; loads the file
void attach(NimBLEService* svc);           // adds the BLE characteristic (before svc->start())
void log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));   // appends a line (also to the serial log)
void clear();
size_t size();
size_t read(size_t offset, char* out, size_t maxLen);   // copy a slice; returns bytes copied

}  // namespace diag
