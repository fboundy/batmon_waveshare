// History of the main readings for the chart page.
//
// Two tiers of ring buffers in PSRAM, both averaged from the 1 s BLE polls:
//   tier 0: one sample / 15 s,  24 hours deep  (Hour view, scroll back 24 h)
//   tier 1: one sample / 6 min, 30 days deep   (Day / Week / Month views)
// Gaps (disconnected) are stored as NaN and drawn as breaks in the line.
//
// Persistence: the buffers are flushed to LittleFS (/history.bin, ~200 KB)
// every HISTORY_SAVE_MS from a low-priority task and restored at boot.  The
// PCF85063 RTC measures the time the display was off so the restored data is
// pushed back by the right number of NaN samples; if the RTC lost power too
// the gap is unknown and assumed to be zero (see docs/03-architecture.md).
#pragma once

#include <stdint.h>

namespace history {

struct Sample {
    float mainV;
    float auxV;
    float soc;      // %, NaN when capacity not configured
    float current;  // A
};

enum class Range : uint8_t { Hour = 0, Day, Week, Month };

constexpr int POINTS = 240;   // points per chart window

// Clock used to timestamp saves: seconds from any fixed epoch, monotonic
// across resets.  Returns false when unavailable.
typedef bool (*ClockFn)(uint32_t& secs);

// Allocates buffers, mounts LittleFS and restores the previous session.
// `clockValid` = false means the clock restarted since the last save, so
// no gap can be computed.
void begin(ClockFn clock, bool clockValid);

// One observation from the BLE poll loop.  Fields may be NaN.
void push(const Sample& s);

// Commits elapsed intervals (with NaN if nothing was pushed) and schedules
// a save when due.  Call often from any task; cheap when nothing is due.
void tick();

// Force a save now (e.g. before a deliberate reboot).  Returns when queued.
void saveNow();

// Fills out[POINTS] with the window `offset` ranges back (0 = most recent,
// right-most point is "now" only when offset == 0).  Missing data is NaN.
void window(Range r, int offset, Sample out[POINTS]);

// How far back the user can scroll for this range (in units of the range).
int maxOffset(Range r);

// Number of seconds covered by one chart point for this range.
uint32_t secondsPerPoint(Range r);

// Stats for the Details page.
uint32_t lastSaveMs();      // millis() of the last successful save, 0 = never
uint32_t restoredSamples(); // tier-0 samples restored at boot

}  // namespace history
