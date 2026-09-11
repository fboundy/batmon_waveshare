// In-RAM history of the main readings for the chart page.
//
// Two tiers of ring buffers in PSRAM, both averaged from the 1 s BLE polls:
//   tier 0: one sample / 15 s,  24 hours deep  (Hour view, scroll back 24 h)
//   tier 1: one sample / 6 min, 30 days deep   (Day / Week / Month views)
// Gaps (disconnected) are stored as NaN and drawn as breaks in the line.
// Timing is millis()-based, so windows are "relative to now"; history is
// lost on reboot (SD-card persistence is on the roadmap).
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

void begin();

// One observation from the BLE poll loop.  Fields may be NaN.
void push(const Sample& s);

// Commits elapsed intervals (with NaN if nothing was pushed).  Call often
// from any task; cheap when nothing is due.
void tick();

// Fills out[POINTS] with the window `offset` ranges back (0 = most recent,
// right-most point is "now" only when offset == 0).  Missing data is NaN.
void window(Range r, int offset, Sample out[POINTS]);

// How far back the user can scroll for this range (in units of the range).
int maxOffset(Range r);

// Number of seconds covered by one chart point for this range.
uint32_t secondsPerPoint(Range r);

}  // namespace history
