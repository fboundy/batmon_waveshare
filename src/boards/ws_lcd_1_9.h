// Pin map and panel parameters: Waveshare ESP32-S3-LCD-1.9 (non-touch)
// (1.9" 170x320 ST7789T3 over SPI, QMI8658 IMU, 2x WS2812, no RTC, no
// touch).  From the Waveshare ESP32-S3-LCD-1.9-Demo (08_LVGL_Test/lcd_config.h,
// 04_WS2812_Test).  Mounted in LANDSCAPE: 320 wide x 170 high.
// See docs/08-hardware-lcd-1-9.md.
#pragma once

#define BOARD_NAME           "Waveshare ESP32-S3-LCD-1.9"
#define BOARD_ROUND          0
#define BOARD_HAS_TOUCH      0     // set to 1 for the ESP32-S3-Touch-LCD-1.9 (CST816D, untested)
#define BOARD_HAS_RTC        0
#define BOARD_HAS_RGB_LED    1

// ---------------------------------------------------------------------------
// I2C bus (QMI8658 IMU; CST816D touch on the touch variant)
// ---------------------------------------------------------------------------
#define PIN_I2C_SDA          47
#define PIN_I2C_SCL          48
#define I2C_FREQ_HZ          400000

// ---------------------------------------------------------------------------
// ST7789 display, 4-wire SPI.  Panel is natively 170x320 (portrait); we run
// it rotated (MADCTL MV|MX) so LVGL sees 320x170.  The 170-px axis is the
// controller's 240-px column space with a 35-px offset on each side.
// ---------------------------------------------------------------------------
#define LCD_H_RES            320
#define LCD_V_RES            170
#define LCD_X_GAP            0
#define LCD_Y_GAP            35
#define LCD_SPI_HOST         SPI3_HOST
#define LCD_SPI_HZ           (40 * 1000 * 1000)
#define PIN_LCD_RST          9
#define PIN_LCD_CLK          10
#define PIN_LCD_DC           11
#define PIN_LCD_CS           12
#define PIN_LCD_MOSI         13
#define PIN_LCD_BL           14

// Partial LVGL draw buffers (DMA-capable internal RAM), in lines.
#define LCD_DRAW_BUF_LINES   40

// 1 = flip 180 degrees (mount USB on the other side); done in the panel
// via MADCTL, free.
#define LCD_ROTATE_180       0

// Backlight PWM
#define BL_PWM_FREQ_HZ       20000
#define BL_PWM_RES_BITS      10

// Touch (touch variant only): CST816D, register-compatible with CST820
#define CST820_ADDR          0x15
#define PIN_TP_INT           -1

// BOOT button (active low): page navigation on this touch-less board
#define PIN_BUTTON           0

// WS2812 x2 on the back (non-touch variant) - used as a link-status LED
#define PIN_STATUS_LED          15

// Misc onboard
#define PIN_BAT_ADC          -1
