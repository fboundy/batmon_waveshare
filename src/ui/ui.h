// LVGL user interface.  Three horizontally-swipeable pages on a 480x480 round
// panel:
//   0  Halo   - SoC dial, voltage, current, power, temperature (the BatMon Halo look)
//   1  Detail - every raw reading, link stats, relay / switch control
//   2  Setup  - battery capacity, brightness, units, pause BLE, forget device
#pragma once

#include <lvgl.h>

#include "../batmon/batmon_state.h"

namespace board { class Display; }

namespace ui {

void create(board::Display& display);

// Push a fresh state snapshot into the widgets.  Call from the LVGL task.
void update(const batmon::State& s);

}  // namespace ui
