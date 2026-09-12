# Firmware architecture

```
src/
├── main.cpp                 setup(): board, history, UI, BLE, console; loop(): LVGL, button, console, UI refresh
├── config.h                 app tunables; includes the board header selected by -D BOARD_*
├── boards/
│   ├── ws_lcd_2_1.h         pins / panel parameters: ESP32-S3-Touch-LCD-2.1 (round 480x480)
│   └── ws_lcd_1_9.h         pins / panel parameters: ESP32-S3-LCD-1.9 (landscape 320x170)
├── settings.{h,cpp}         NVS-backed user settings (Preferences, namespace "batmon")
├── history.{h,cpp}          two-tier PSRAM ring buffers of V / aux V / SoC / I, persisted to LittleFS
├── console.{h,cpp}          USB serial command console (settings, switch/relay, pause, page)
├── board/
│   ├── board.h              the board interface the app uses (init, lvgl, backlight, clock, LED, button)
│   ├── board_ws_2_1.cpp     implementation for the 2.1 (compiled only in that env)
│   ├── board_ws_1_9.cpp     implementation for the 1.9
│   ├── lvgl_port.*          generic LVGL 8.3 glue: direct frame buffers or partial async buffers, optional touch
│   ├── button.*             debounced BOOT button, short / long press
│   ├── display_st7701.*     2.1: ST7701 init over 3-wire SPI + esp_lcd RGB panel + backlight PWM
│   ├── display_st7789.*     1.9: ST7789 over SPI via esp_lcd, DMA strips, backlight PWM
│   ├── tca9554.*            2.1: IO expander
│   ├── touch_cst820.*       2.1: CST820 (also CST816 on the 1.9 touch variant)
│   └── rtc.*                2.1: PCF85063, used as a monotonic clock to measure reboot gaps
├── batmon/
│   ├── batmon_protocol.*    pure encode/decode of the BatMon wire format (no BLE deps)
│   ├── batmon_state.h       snapshot struct shared between tasks
│   └── batmon_client.*      NimBLE central task: scan / connect / poll / relay control
└── ui/
    ├── ui.*                 all page behaviour: update(), chart building, callbacks, navigation
    ├── ui_internal.h        Widgets struct + helpers shared with the layouts
    ├── ui_layout_round.cpp  page construction for the 480x480 round panel (arc gauge, touch)
    ├── ui_layout_wide.cpp   page construction for 320x170 landscape (bar gauge, no touch)
    └── font_montserrat_72_digits.c   generated 72 px digits for the round SoC (tools/gen_font.py)
include/lv_conf.h            LVGL configuration (16-bit colour, Montserrat 12–48; byte swap per env)
tools/gen_font.py            Pillow-based LVGL font generator (no node/lv_font_conv needed)
test/test_protocol/          Unity tests for batmon_protocol (host, `pio test -e native`)
```

## Board abstraction

`platformio.ini` has one env per board; each defines `BOARD_WS_LCD_*`,
which makes `config.h` pull in the matching `src/boards/*.h`, and uses
`build_src_filter` to compile only that board's `board_*.cpp`, display
driver and UI layout. The application talks to `board::` only:

| `board::` | 2.1 | 1.9 |
|---|---|---|
| `init()` | I2C, TCA9554, ST7701 RGB panel, CST820, RTC, button | ST7789 SPI, button, WS2812 |
| `lvgl()` | direct mode: LVGL draws into the two PSRAM frame buffers, software 180° flip | partial mode: two 320×40 DMA strips, async flush, rotation in MADCTL |
| `clock()` / `clockValid()` | PCF85063 | none — history gap assumed zero |
| `setStatusLed()` | no-op | WS2812 green / blue / red |
| `pollButton()` | BOOT (GPIO0) | BOOT (GPIO0) |

The UI is split the same way: `ui.cpp` owns every behaviour and only touches
widgets that the active layout created (all handles in `ui::Widgets` are
null by default), so a layout can leave out anything that does not fit.
`ui_layout_wide.cpp` has no toggles or sliders at all — the 1.9 has no touch
— and shows relay/switch state as text instead; the round layout keeps the
interactive widgets.

## Tasks and threading

| Task | Core | Priority | Does |
|---|---|---|---|
| `loopTask` (Arduino `loop()`) | 1 | 1 | `lv_timer_handler()`, touch polling, BOOT button, serial console, calls `ui::update()` every 250 ms with a fresh snapshot |
| `batmon_ble` | 0 | 2 | everything NimBLE: scanning, connecting, sequential polling, executing queued commands |
| NimBLE host task | 0 | — | created by NimBLE-Arduino |
| `hist_save` | 0 | 1 | copies the history buffers under the lock, then writes `/history.bin` to LittleFS |
| `lvgl_tick` esp_timer | — | — | `lv_tick_inc(2)` every 2 ms |

Data flows one way: the BLE task writes into `Client::state_` under a mutex;
the UI task calls `Client::snapshot()` to copy it. UI → BLE requests
(relay/switch, pause, resume, forget, reconnect) go through a FreeRTOS queue
and are executed between polls, so no NimBLE call ever happens on the UI
task and no LVGL call ever happens on the BLE task.

LVGL itself is guarded by a recursive mutex in `LvglPort` (currently only
the loop task touches LVGL, but the lock is there for future tasks such as
a Wi-Fi / MQTT bridge that wants to show status).

## BLE state machine (`Client::task`)

```
        ┌──────────┐  found   ┌────────────┐ ok  ┌───────────┐
  ─────►│ Scanning │─────────►│ Connecting │────►│ Connected │──┐ link lost /
        └──────────┘          └────────────┘     └───────────┘  │ 5 poll errors
             ▲   ▲ none / fail       │ fail             ▲        │
             │   └───────────────────┘                  │        ▼
             │              ┌──────────────┐            │  ┌──────────────┐
             └──────────────│    Paused    │◄───────────┴──│ Reconnecting │
               timer / resume└──────────────┘  pause cmd    └──────────────┘
                                                              2 s backoff
```

* **Scanning** — 6 s active scan. A device qualifies if it advertises the
  BatMon service UUID, manufacturer ID 4077, or a `BK-` name. If a preferred
  address is stored it must match; otherwise the strongest RSSI wins and is
  stored as the preferred device.
* **Connecting** — 10 s timeout, connection interval 15–30 ms. On connect the
  full GATT table is logged and the two characteristics located by UUID.
* **Connected** — fast poll every `pollMs` (default 1 s: main V, I, ext T,
  Ah, aux V, switch), slow poll every 30 s (Ah max/min, CPU T, relay), derived
  values recomputed and a history sample pushed after each fast poll.
  Commands are drained between polls.
* **Paused** — connection dropped on purpose for N minutes so the phone app
  can connect. `Resume` or the timer returns to Scanning.

## Derived values

Computed in `Client::computeDerived()`:

* `watts = volts × current` (signed, like HA)
* `soc` = see [02-ble-protocol.md §5](02-ble-protocol.md#5-state-of-charge); `-1` if capacity is 0
* `hoursRemaining` = remaining Ah ÷ |current| when discharging, missing Ah ÷ current when charging, `-1` when |I| < 50 mA.
  This is the instantaneous estimate; smoothing is on the roadmap.

## History store (`src/history.*`)

Feeds the chart page. Two ring buffers in PSRAM, both NaN-aware means of
the 1 s polls:

| Tier | Interval | Depth | Size | Serves |
|---|---|---|---|---|
| 0 | 15 s | 24 h (5760 samples) | 92 KB | Hour view (scroll back up to 24 h) |
| 1 | 6 min | 30 days (7200 samples) | 115 KB | Day (1:1), Week (7:1), Month (30:1) views |

`history::push()` is called by the BLE task after every fast poll;
`history::tick()` (also from the BLE task) commits an interval when it has
elapsed, writing NaN for intervals with no data so gaps show as breaks in
the line. Timing is `millis()`-based, so windows are "relative to now".

### Persistence

Every `HISTORY_SAVE_MS` (5 min) `tick()` signals the `hist_save` task, which
snapshots both buffers plus the partial tier-1 accumulator into PSRAM
staging copies and writes them to `/history.bin` (~207 KB) on LittleFS
(the 3.4 MB `spiffs` partition of `default_16MB.csv`; NVS is only 20 KB).
The write goes to `/history.tmp` and is renamed, so a power cut mid-write
leaves the previous file intact. Wear: ~60 MB/day over a 3.4 MB
wear-levelled region is ~20 erase cycles/day, decades of endurance.

At boot `history::begin()` restores the file, then uses the PCF85063 RTC to
work out how long the display was off and pushes that many NaN samples so
the restored data lands at the right place on the time axis. The RTC has no
backup battery, so it only survives resets/reflashes, not power loss: if its
oscillator-stop flag is set the gap is unknown and assumed to be zero (the
old data is simply continued — a limitation, logged as a warning).
`Rtc::begin()` restarts the count at 2000-01-01 in that case.

`history::window(range, offset, out[240])` returns one chart's worth of
points, averaged over the stride, with `offset` windows back from now.

## UI

Four tiles in an `lv_tileview`, swiped horizontally, in the order Halo,
Chart, Details, Setup. All widgets are kept inside the visible circle of the
round panel (roughly a 440 px diameter).

| Tile | Contents |
|---|---|
| Halo (round) | 270° SoC arc coloured green/amber/red; 72 px SoC digits with a 32 px unit on a shared baseline; **Main** and **Aux** voltages (40 px, 28 px units) side by side; current with charge/discharge arrow (blue = charging, amber = discharging); power; external temperature; time-to-empty/full; large **Switch** output toggle; red **charge-mismatch alert** (aux > 13.0 V while main current < 0.2 A); Bluetooth glyph in the arc's gap, green when connected with fresh data, red otherwise |
| Halo (wide) | horizontal SoC **bar** coloured the same way, 28 px SoC %, Main/Aux voltages, current/power/temperature, runtime, "Switch ON/OFF" text, alert line, Bluetooth glyph |
| Chart | line chart of Main V / Aux V / SoC / Amps (toggle buttons), **Hour / Day / Week / Month** range buttons and ◀ ▶ to scroll one range at a time. Left axis is volts (auto-ranged); right axis is SoC % when SoC is shown, otherwise amps (auto-ranged). When both SoC and amps are on, amps are scaled onto the SoC axis and the scale is printed under the chart |
| Details | every raw reading, RSSI, poll counters, MAC, time since the last history save; **Relay** and **Switch** toggles |
| Setup | capacity ±1/±10 Ah, brightness slider, °C/°F, **Pause BLE 5 min / Resume**, **Forget device**, firmware version |

Readings older than 10 s are drawn grey so a frozen link is obvious.
Baseline alignment between different font sizes in one row is done by
translating the smaller label up by the difference of the fonts'
`base_line` values (see `buildHalo()`).
Switch/relay widgets are not re-synced from device state for 3 s after a
user tap so they don't snap back before the BLE command completes. Chart
series and range selections persist in NVS.

## Rendering

LVGL is given the two PSRAM frame buffers that belong to the RGB driver and
runs in `full_refresh` mode. A flush therefore calls
`esp_lcd_panel_draw_bitmap()` with a pointer the driver already owns, which
it recognises and turns into a buffer swap at the next VSYNC — no memcpy.
This is the same scheme Waveshare's demo uses and it is tear-free at 16 MHz.

Cost: every LVGL redraw touches the whole 480 × 480 × 2 B buffer in PSRAM
(~460 KB). At a 250 ms UI refresh this is negligible.

### Orientation

On the 2.1, `LCD_ROTATE_180` (on by default — the board is mounted USB-up) turns the UI
upside down. LVGL's own `sw_rotate` refuses to work with `full_refresh`, so
`LvglPort::flushCb()` reverses the frame buffer in place (two RGB565 pixels
per 32-bit word: reverse the words, swap the halves — a few ms in PSRAM)
before handing it to the RGB driver, and `touchCb()` mirrors both touch
axes. Doing it in the ST7701 (MADCTL / `0xC7` source-direction registers)
would be free but is untested on this panel. On the 1.9 the same flag is
implemented in the ST7789's MADCTL (mirror x/y), which costs nothing.

### Input without touch

`ui::nextPage()` and `ui::cycleChartRange()` are driven by the BOOT button
(short / long press) on every board; the serial console (`src/console.cpp`)
covers settings, relay/switch, pause/resume and forget, and calls
`ui::settingsChanged()` so the widgets follow.
