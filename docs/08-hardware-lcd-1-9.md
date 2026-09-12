# Hardware: Waveshare ESP32-S3-LCD-1.9 (second target)

Source: [Waveshare wiki](https://www.waveshare.com/wiki/ESP32-S3-LCD-1.9)
and `ESP32-S3-LCD-1.9-Demo.zip` (`Arduino/examples/08_LVGL_Test/lcd_config.h`,
`lcd_bsp.c`, `04_WS2812_Test`), from which the pin map and the ST7789
tuning registers were taken. The wiki's interface tables are images, so the
demo is the authoritative source.

> **Status:** builds (`pio run -e waveshare_s3_lcd_1_9`), **not yet run on
> hardware** — the board was not available when this port was written.
> First-run checklist is at the bottom.

This is the **non-touch** variant. The `ESP32-S3-Touch-LCD-1.9` adds a
CST816D touch controller (register-compatible with the CST820 driver in this
repo) and an IPEX antenna pad; set `BOARD_HAS_TOUCH 1` in
`src/boards/ws_lcd_1_9.h` to try it (untested).

## Summary

| Item | Detail |
|---|---|
| MCU | ESP32-S3R8 — same as the 2.1: 8 MB octal PSRAM, 16 MB flash |
| Display | 1.9" IPS, **170 × 320** native, ST7789T3 controller, **4-wire SPI**; used in **landscape 320 × 170** |
| Touch | none (CST816D on the *Touch* variant, I2C `0x15`) |
| IMU | QMI8658 on I2C — unused |
| RTC | **none** — history restore cannot measure the reboot gap (assumed zero) |
| LEDs | 2 × WS2812 on the back (non-touch variant) — used as a link-status LED |
| Buttons | RESET, **BOOT (GPIO0)** — used for page navigation |
| Storage | TF-card slot — unused |
| Power | USB-C 5 V; MX1.25 LiPo header with charger |
| Headers | Pico-compatible 2 × 20 pins |

## Pin map

| Signal | GPIO | Notes |
|---|---|---|
| LCD RST | 9 | |
| LCD SCLK | 10 | SPI3_HOST |
| LCD DC | 11 | |
| LCD CS | 12 | |
| LCD MOSI | 13 | no MISO |
| LCD backlight | 14 | PWM (LEDC), 20 kHz |
| WS2812 data | 15 | 2 LEDs; only the first is driven |
| I2C SDA / SCL | 47 / 48 | IMU (and touch on the touch variant) |
| BOOT button | 0 | active low, internal pull-up |

## Panel driving

The ESP-IDF `esp_lcd` ST7789 driver does the sleep-out / MADCTL / COLMOD
sequence; the firmware then sends the panel-tuning registers from the
Waveshare demo (`0xB2` porch, `0xB7`, `0xBB` VCOM, `0xC0`–`0xC6`, `0xD0`,
`0xD6`, `0xE0`/`0xE1` gamma), inverts colour (`INVON` — this panel needs
it) and sets:

| Setting | Value | Why |
|---|---|---|
| `swap_xy` | true | landscape (MADCTL MV) |
| `mirror` | x = true, y = false (or y = true for `LCD_ROTATE_180`) | MX for USB-on-the-right; MY for the other way |
| gap | x 0, y **35** | the 170-px axis sits in the middle of the controller's 240 columns |
| SPI clock | 40 MHz | |
| colour order | RGB, 16 bpp | ST7789 wants big-endian RGB565, so this env builds LVGL with `LV_COLOR_16_SWAP=1` |

Pixels are streamed from two 320 × 40 px DMA buffers in internal RAM
(51 KB); `on_color_trans_done` fires `LvglPort::flushDone()` so LVGL renders
the next strip while the previous one is still on the wire. A full frame is
~108 KB → about 25 ms at 40 MHz.

## Input without touch

| Action | Effect |
|---|---|
| BOOT short press | next page (Halo → Chart → Details → Setup → Halo) |
| BOOT long press (≥ 0.8 s) | cycle chart range Hour → Day → Week → Month |
| USB serial console | everything else — see [04-build-and-flash.md](04-build-and-flash.md#serial-console) |

The WS2812 shows link state: green connected (fresh data), blue paused, red
otherwise, at 1/8 brightness.

## First-run checklist

1. `pio run -e waveshare_s3_lcd_1_9 -t upload`, then `pio device monitor`.
   Expect `st7789: panel up, 320x170`, then BLE scanning.
2. Picture on screen? If it is mirrored or offset by 35 px, adjust
   `esp_lcd_panel_mirror()` / `LCD_Y_GAP` in `src/board/display_st7789.cpp`
   and `src/boards/ws_lcd_1_9.h`. If colours are swapped (blue ↔ red), the
   `rgb_ele_order` needs to be BGR; if they look inverted, remove the
   `esp_lcd_panel_invert_color()` call.
3. If the image is garbage/striped the byte order is wrong: drop
   `-D LV_COLOR_16_SWAP=1` from the env.
4. BOOT press should change page; hold should change the chart range.
5. `status` on the serial console, then `cap <Ah>` to enable SoC.
