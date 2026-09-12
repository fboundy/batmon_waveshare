# Hardware: Waveshare ESP32-S3-Touch-LCD-2.1

Source: [Waveshare wiki](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-2.1)
and the `ESP32-S3-Touch-LCD-2.1-Demo.zip` Arduino example (`LVGL_Arduino`),
from which the display init sequence and pin map were taken.

## Summary

| Item | Detail |
|---|---|
| MCU | ESP32-S3R8 — dual-core Xtensa LX7 @ 240 MHz, 512 KB SRAM, **8 MB octal PSRAM**, **16 MB QIO flash** |
| Radio | 2.4 GHz Wi-Fi b/g/n + **Bluetooth 5 LE**, onboard antenna (IPEX pad available by moving a resistor) |
| Display | 2.1" round IPS, **480 × 480**, ST7701 controller, 16-bit RGB parallel interface + 3-wire SPI for init; PWM backlight |
| Touch | CST820 capacitive, I2C `0x15`, INT on GPIO16, reset via IO expander |
| IO expander | TCA9554PWR, I2C `0x20` — LCD reset/CS, touch reset, SD CS, buzzer, IMU/RTC interrupts. **Not** brought out |
| IMU | QMI8658 6-axis, I2C (INT1/INT2 on expander) — unused by this firmware |
| RTC | PCF85063, I2C `0x51`, backup-battery header (unpopulated) — used as a monotonic clock to size the reboot gap in the history |
| Storage | TF-card slot (SPI, shares MOSI/SCK with the LCD SPI) — unused |
| Power | USB-C 5 V; MX1.25 LiPo header with charger; ME6217C33 3.3 V LDO (800 mA); battery sense on GPIO4 (÷3) |
| USB | Native USB (GPIO19/20) **and** a CH343P USB-UART on a second USB-C; auto-download circuit |
| Buttons | RESET, BOOT (GPIO0), battery power switch |
| Buzzer | on expander EXIO8 |
| Headers | 12-pin FPC-style (GND, 5 V, D−, D+, GND, 3V3, SCL, SDA, TXD, RXD, NC, IO0); separate 4-pin I2C and UART headers |
| Size | 75 mm diameter round PCB |

## Pin map

### I2C (shared bus)

| Signal | GPIO |
|---|---|
| SDA | 15 |
| SCL | 7 |

Devices: TCA9554 (`0x20`), CST820 (`0x15`), QMI8658 (`0x6B`), PCF85063 (`0x51`).

### TCA9554 expander (EXIO pins, 1-based)

| EXIO | Function | Direction |
|---|---|---|
| 1 | LCD reset | out |
| 2 | Touch reset | out |
| 3 | LCD SPI chip-select | out |
| 4 | SD card chip-select | out |
| 5 | IMU INT2 | in |
| 6 | IMU INT1 | in |
| 7 | RTC INT | in |
| 8 | Buzzer | out |

The firmware configures all eight as outputs (as the Waveshare demo does)
and drives the buzzer low at boot.

### ST7701 display

Init is done over a **9-bit 3-wire SPI** (1 D/C bit + 8 data bits, no MISO).
The ESP32 SPI peripheral is configured with `command_bits = 1`,
`address_bits = 8`, 40 MHz, mode 0, CS handled manually on EXIO3.

| Signal | GPIO |
|---|---|
| LCD_SCL (SPI clock) | 2 |
| LCD_SDA (SPI MOSI) | 1 |
| LCD_BL (backlight PWM) | 6 |
| LCD_RST | EXIO1 |
| LCD_CS | EXIO3 |

Pixel data uses the LCD_CAM peripheral in **RGB565** (`data_width = 16`):

| RGB signal | GPIO | | RGB signal | GPIO |
|---|---|---|---|---|
| PCLK | 41 | | G0 (D5) | 14 |
| DE | 40 | | G1 (D6) | 13 |
| VSYNC | 39 | | G2 (D7) | 12 |
| HSYNC | 38 | | G3 (D8) | 11 |
| B1 (D0) | 5 | | G4 (D9) | 10 |
| B2 (D1) | 45 | | G5 (D10) | 9 |
| B3 (D2) | 48 | | R1 (D11) | 46 |
| B4 (D3) | 47 | | R2 (D12) | 3 |
| B5 (D4) | 21 | | R3 (D13) | 8 |
| | | | R4 (D14) | 18 |
| | | | R5 (D15) | 17 |

B0 and R0 are not connected (the panel is wired for 16-bit colour).

Timing (from the demo, works):

| Parameter | Value |
|---|---|
| Pixel clock | 16 MHz |
| HSYNC pulse / back porch / front porch | 8 / 10 / 50 |
| VSYNC pulse / back porch / front porch | 3 / 8 / 8 |
| Frame buffers | 2, in PSRAM (`fb_in_psram`, `double_fb`) |
| Bounce buffer | 480 × 10 px |

The full register table is in `src/board/display.cpp` (`kInit[]`). It is a
verbatim transcription of `Display_ST7701.cpp` from the Waveshare demo:
Command2 BK0 (scan lines, porches, gamma), BK1 (VOP/VCOM/VGH/VGL and GIP
timing), BK3, then `MADCTL=0x00`, `COLMOD=0x66`, sleep-out, 480 ms wait,
inversion-off, 120 ms wait, display-on.

### CST820 touch

| Register | Purpose |
|---|---|
| `0x01` | gesture ID, then `[points, xH, xL, yH, yL]` |
| `0x15` | firmware version |
| `0xA7` | chip ID, project ID, FW version |
| `0xFE` | write `0xFF` to disable auto-sleep |

Gesture IDs: `0x01` up, `0x02` down, `0x03` left, `0x04` right,
`0x05` single click, `0x0B` double click, `0x0C` long press. The firmware
only uses the raw point; LVGL derives swipes itself.

Coordinates are 0–479 in both axes with no rotation needed.

### Other

| Signal | GPIO |
|---|---|
| Touch INT | 16 |
| Battery ADC | 4 (÷3 divider; demo uses `mV × 3 / 0.992857`) |
| SD MISO / MOSI / SCK / CS | 42 / 1 / 2 / EXIO4 |
| UART TXD / RXD | 43 / 44 |

## Build settings that matter

| Setting | Value | Why |
|---|---|---|
| Arduino core | ≥ 3.0 (ESP-IDF 5.x) | `esp_lcd_rgb_panel_get_frame_buffer`, bounce buffers, `ledcAttach()` |
| PSRAM | OPI, `-DBOARD_HAS_PSRAM` | both frame buffers (2 × 460 KB) live in PSRAM |
| Flash | 16 MB, QIO, `default_16MB.csv` | plenty of room for fonts |
| USB | `ARDUINO_USB_MODE=1`, `ARDUINO_USB_CDC_ON_BOOT=1` | serial log over the native USB-C port |

See [04-build-and-flash.md](04-build-and-flash.md).

## Things learnt the hard way

* Keep the RGB pixel clock at 16 MHz. Going faster with PSRAM frame buffers
  causes tearing / drift when Wi-Fi or BLE is active; the bounce buffer
  mitigates but does not eliminate it.
* The CST820 goes to sleep after a few seconds unless `0xFE = 0xFF` is
  written; a sleeping controller swallows the first touch.
* The TCA9554 output latch should be read back before writing so the LCD is
  not accidentally reset when re-initialising.
* The LCD SPI and the SD card share MOSI/SCK. Re-initialising the LCD requires
  re-initialising the SD card (per Waveshare).
