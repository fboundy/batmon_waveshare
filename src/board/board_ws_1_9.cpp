// Board implementation: Waveshare ESP32-S3-LCD-1.9 (landscape, SPI panel,
// no touch, no RTC, WS2812 status LED, BOOT button for navigation).
#include "../config.h"
#ifdef BOARD_WS_LCD_1_9

#include <Arduino.h>
#include <Wire.h>

#include "board.h"
#include "button.h"
#include "display_st7789.h"

namespace board {

static DisplaySt7789 g_display;
static LvglPort      g_lvgl;
static Button        g_button;

static void flushArea(int x1, int y1, int x2, int y2, const void* px) {
    g_display.flush(x1, y1, x2, y2, px);
}

bool init() {
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_FREQ_HZ);   // IMU (unused) / touch variant

    bool ok = g_display.begin(LvglPort::flushDone);
    if (!ok) Serial.println("Display init failed!");
    g_button.begin(PIN_BUTTON, BUTTON_LONG_MS);
#if BOARD_HAS_RGB_LED
    rgbLedWrite(PIN_STATUS_LED, 0, 0, 0);
#endif

    LvglConfig cfg;
    cfg.width = LCD_H_RES;
    cfg.height = LCD_V_RES;
    cfg.direct = false;
    cfg.buf0 = g_display.drawBuffer(0);
    cfg.buf1 = g_display.drawBuffer(1);
    cfg.bufPixels = g_display.drawBufferPixels();
    cfg.rotate180 = false;          // done in the panel (MADCTL), see display_st7789.cpp
    cfg.flushAsync = true;
    cfg.flush = flushArea;
    cfg.touchRead = nullptr;        // non-touch variant
    g_lvgl.begin(cfg);
    return ok;
}

LvglPort& lvgl() { return g_lvgl; }
void setBacklight(uint8_t percent) { g_display.setBacklight(percent); }
void displayResync() {}
bool clock(uint32_t&) { return false; }
bool clockValid() { return false; }

void setStatusLed(uint8_t r, uint8_t g, uint8_t b) {
#if BOARD_HAS_RGB_LED
    static uint32_t last = 0xFFFFFFFF;
    uint32_t v = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
    if (v == last) return;
    last = v;
    // Keep it dim; the WS2812s are bright.
    rgbLedWrite(PIN_STATUS_LED, r / 8, g / 8, b / 8);
#else
    (void)r; (void)g; (void)b;
#endif
}

ButtonEvent pollButton() { return g_button.poll(); }

}  // namespace board

#endif  // BOARD_WS_LCD_1_9
