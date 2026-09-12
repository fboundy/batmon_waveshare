// LVGL user interface.  Four pages in a tileview, in the order
//   0  Halo   - SoC gauge, main/aux voltage, current, power, temperature,
//               switch state, charge-mismatch alert (the BatMon Halo look)
//   1  Chart  - history of main V / aux V / SoC / current; Hour/Day/Week/Month
//   2  Detail - every raw reading, link stats, relay / switch
//   3  Setup  - battery capacity, brightness, units, pause BLE, forget device
// The page contents come from a per-form-factor layout file
// (ui_layout_round.cpp for 480x480 round, ui_layout_wide.cpp for 320x170);
// all behaviour lives in ui.cpp.
#pragma once

#include <lvgl.h>

#include "../batmon/batmon_state.h"

namespace ui {

void create();

// Push a fresh state snapshot into the widgets.  Call from the LVGL task.
void update(const batmon::State& s);

// Navigation for boards without touch (BOOT button).
void nextPage();
void cycleChartRange();

// Re-read settings into the widgets (after a serial-console change).
void settingsChanged();

}  // namespace ui
