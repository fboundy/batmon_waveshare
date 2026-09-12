// What the application needs from a board.  One implementation per board
// (board_ws_2_1.cpp, board_ws_1_9.cpp), selected by platformio.ini.
#pragma once

#include <stdint.h>

#include "lvgl_port.h"

namespace board {

// Brings up every peripheral and registers the LVGL display / input.
// Returns false on a fatal error (display not found).
bool init();

LvglPort& lvgl();

void setBacklight(uint8_t percent);

// Monotonic seconds counter for history save/restore (RTC).  Returns false
// on boards without one.  clockValid() is false if the clock restarted
// since the last save (power loss) or the board has no clock at all.
bool clock(uint32_t& secs);
bool clockValid();

// Status LED (no-op where absent).
void setStatusLed(uint8_t r, uint8_t g, uint8_t b);

// BOOT button, polled from loop().
enum class ButtonEvent : uint8_t { None, Short, Long };
ButtonEvent pollButton();

}  // namespace board
