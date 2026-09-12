// Application configuration.  Board-specific pins/panel parameters come from
// src/boards/<board>.h, selected by a -D BOARD_* define in platformio.ini.
#pragma once

#include <stdint.h>

// ---------------------------------------------------------------------------
// Firmware identity
// ---------------------------------------------------------------------------
#define FW_NAME     "batmon-display"
#define FW_VERSION  "0.4.0"

// ---------------------------------------------------------------------------
// Board selection
// ---------------------------------------------------------------------------
#if defined(BOARD_WS_LCD_2_1)
#include "boards/ws_lcd_2_1.h"
#elif defined(BOARD_WS_LCD_1_9)
#include "boards/ws_lcd_1_9.h"
#else
#error "Define BOARD_WS_LCD_2_1 or BOARD_WS_LCD_1_9 in platformio.ini"
#endif

#ifndef BOARD_HAS_RGB_LED
#define BOARD_HAS_RGB_LED    0
#endif

#define BL_DEFAULT_PERCENT   80

// ---------------------------------------------------------------------------
// Application behaviour (shared by all boards)
// ---------------------------------------------------------------------------
#define BLE_SCAN_MS               6000   // one scan pass
#define BLE_CONNECT_TIMEOUT_MS    10000
#define BLE_POLL_FAST_MS          1000   // V / I / T / Ah / aux V / switch
#define BLE_POLL_SLOW_MS          30000  // Ah max/min, int T, relay
#define BLE_RECONNECT_BACKOFF_MS  2000
#define BLE_PAUSE_DEFAULT_MS      (5 * 60 * 1000)  // "let the phone app in" pause

#define HISTORY_SAVE_MS           (5 * 60 * 1000)   // flush history to LittleFS
#define UI_REFRESH_MS             250
#define CHART_REFRESH_MS          5000
// Charge-status icon (see docs/03-architecture.md, "Charge icon")
#define CHG_AUX_CHARGING_V        13.2f   // aux above this = a charger is running
#define CHG_MAIN_CURRENT_A        0.05f   // main current above this = charging
#define CHG_FULL_SOC              95.0f   // main considered full: no charge expected
#define CHG_FULL_MAIN_V           14.4f
#define DATA_STALE_MS             10000  // grey-out readings older than this

// Button timing
#define BUTTON_LONG_MS            800
