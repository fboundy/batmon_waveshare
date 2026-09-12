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

// Call after any flash write (NVS / LittleFS): RGB panels that stream from
// PSRAM can lose sync while flash is busy.  No-op on other panels.
void displayResync();

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

// millis() of the last touch or button press (for waking from standby).
uint32_t lastInputMs();
void noteInput();

// Raw touch diagnostics: when enabled, every pressed sample is logged.
void setTouchLog(bool on);
bool touchLog();

}  // namespace board
