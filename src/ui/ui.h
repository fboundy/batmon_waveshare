// LVGL user interface.  Four horizontally-swipeable pages on a 480x480 round
// panel:
//   0  Halo   - SoC dial, main/aux voltage, current, power, temperature,
//               switch control, charge-mismatch alert (the BatMon Halo look)
//   1  Detail - every raw reading, link stats, relay / switch control
//   2  Chart  - history of main V / aux V / SoC / current; Hour/Day/Week/Month
//   3  Setup  - battery capacity, brightness, units, pause BLE, forget device
#pragma once

#include <lvgl.h>

#include "../batmon/batmon_state.h"

namespace board { class Display; }

namespace ui {

void create(board::Display& display);

// Push a fresh state snapshot into the widgets.  Call from the LVGL task.
void update(const batmon::State& s);

}  // namespace ui
