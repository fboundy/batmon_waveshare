// Board implementation: Waveshare ESP32-S3-Touch-LCD-2.1 (round, RGB panel).
#include "../config.h"
#ifdef BOARD_WS_LCD_2_1

#include <Arduino.h>
#include <Wire.h>

#include "board.h"
#include "button.h"
#include "display_st7701.h"
#include "rtc.h"
#include "tca9554.h"
#include "touch_cst820.h"

namespace board {

static Tca9554  g_io(TCA9554_ADDR);
static Display  g_display;
static Touch    g_touch;
static Rtc      g_rtc;
static LvglPort g_lvgl;
static Button   g_button;

static void flushFrame(int x1, int y1, int x2, int y2, const void* px) {
    g_display.flush(x1, y1, x2, y2, px);
}

static void waitVsync() {
    g_display.waitVsync(40);
}

static bool touchRead(uint16_t& x, uint16_t& y) {
    TouchPoint tp = g_touch.read();
    if (!tp.pressed) return false;
    x = tp.x;
    y = tp.y;
    return true;
}

bool init() {
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_FREQ_HZ);

    // All expander pins as outputs; buzzer off, resets released.
    if (!g_io.begin(0x00)) Serial.println("TCA9554 not found!");
    g_io.setOutput(EXIO_BUZZER, false);
    g_io.setOutput(EXIO_SD_CS, true);

    bool ok = g_display.begin(g_io);
    if (!ok) Serial.println("Display init failed!");
    g_touch.begin(g_io);
    g_rtc.begin();
    g_button.begin(PIN_BUTTON, BUTTON_LONG_MS);

    LvglConfig cfg;
    cfg.width = LCD_H_RES;
    cfg.height = LCD_V_RES;
    cfg.direct = true;
    cfg.buf0 = g_display.frameBuffer(0);
    cfg.buf1 = g_display.frameBuffer(1);
    cfg.bufPixels = (size_t)LCD_H_RES * LCD_V_RES;
    cfg.rotate180 = LCD_ROTATE_180;
    cfg.flush = flushFrame;
    cfg.waitVsync = waitVsync;
    cfg.touchRead = touchRead;
    g_lvgl.begin(cfg);
    return ok;
}

LvglPort& lvgl() { return g_lvgl; }
void setBacklight(uint8_t percent) { g_display.setBacklight(percent); }
void displayResync() { g_display.resync(); }
bool clock(uint32_t& secs) { return g_rtc.now(secs); }
bool clockValid() { return !g_rtc.lostContinuity(); }
void setStatusLed(uint8_t, uint8_t, uint8_t) {}
ButtonEvent pollButton() { return g_button.poll(); }

}  // namespace board

#endif  // BOARD_WS_LCD_2_1
