// Pin map and panel parameters: Waveshare ESP32-S3-Touch-LCD-2.1
// (2.1" round 480x480 ST7701 RGB panel, CST820 touch, TCA9554 expander,
// PCF85063 RTC).  From the Waveshare wiki "Internal Hardware Connection"
// tables and the LVGL_Arduino demo.  See docs/01-hardware.md.
#pragma once

#define BOARD_NAME           "Waveshare ESP32-S3-Touch-LCD-2.1"
#define BOARD_ROUND          1
#define BOARD_HAS_TOUCH      1
#define BOARD_HAS_RTC        1

// ---------------------------------------------------------------------------
// I2C bus (shared: TCA9554 IO expander, CST820 touch, QMI8658 IMU, PCF85063 RTC)
// ---------------------------------------------------------------------------
#define PIN_I2C_SDA          15
#define PIN_I2C_SCL          7
#define I2C_FREQ_HZ          400000

// TCA9554PWR IO expander (address 0x20).  Pin numbers are 1-based "EXIOn".
#define TCA9554_ADDR         0x20
#define EXIO_LCD_RST         1
#define EXIO_TP_RST          2
#define EXIO_LCD_CS          3
#define EXIO_SD_CS           4
#define EXIO_IMU_INT2        5
#define EXIO_IMU_INT1        6
#define EXIO_RTC_INT         7
#define EXIO_BUZZER          8

// ---------------------------------------------------------------------------
// ST7701 display: 3-wire SPI (9-bit, "SDA/SCL") for register init, then
// 16-bit parallel RGB565 for pixel data.
// ---------------------------------------------------------------------------
#define LCD_H_RES            480
#define LCD_V_RES            480
#define PIN_LCD_SPI_SCL      2
#define PIN_LCD_SPI_SDA      1
#define PIN_LCD_BL           6

#define PIN_LCD_HSYNC        38
#define PIN_LCD_VSYNC        39
#define PIN_LCD_DE           40
#define PIN_LCD_PCLK         41
// RGB565 data bus, D0..D15 = B1..B5, G0..G5, R1..R5  (B0/R0 not connected)
#define PIN_LCD_D0           5    // B1
#define PIN_LCD_D1           45   // B2
#define PIN_LCD_D2           48   // B3
#define PIN_LCD_D3           47   // B4
#define PIN_LCD_D4           21   // B5
#define PIN_LCD_D5           14   // G0
#define PIN_LCD_D6           13   // G1
#define PIN_LCD_D7           12   // G2
#define PIN_LCD_D8           11   // G3
#define PIN_LCD_D9           10   // G4
#define PIN_LCD_D10          9    // G5
#define PIN_LCD_D11          46   // R1
#define PIN_LCD_D12          3    // R2
#define PIN_LCD_D13          8    // R3
#define PIN_LCD_D14          18   // R4
#define PIN_LCD_D15          17   // R5

#define LCD_PCLK_HZ          (16 * 1000 * 1000)
#define LCD_HSYNC_PULSE      8
#define LCD_HSYNC_BACK       10
#define LCD_HSYNC_FRONT      50
#define LCD_VSYNC_PULSE      3
#define LCD_VSYNC_BACK       8
#define LCD_VSYNC_FRONT      8
// Bounce buffer (in pixels) avoids screen drift when PSRAM bandwidth is contended.
#define LCD_BOUNCE_PX        (LCD_H_RES * 10)

// Mount orientation: 1 = rotate the whole UI (and touch) by 180 degrees.
// Done in software on the frame buffer; see docs/03-architecture.md.
#define LCD_ROTATE_180       1

// Backlight PWM
#define BL_PWM_FREQ_HZ       20000
#define BL_PWM_RES_BITS      10

// CST820 capacitive touch (I2C 0x15)
#define CST820_ADDR          0x15
#define PIN_TP_INT           16

// BOOT button (active low) - optional page navigation
#define PIN_BUTTON           0

// Misc onboard
#define PIN_BAT_ADC          4     // 3:1 divider onto the LiPo header
