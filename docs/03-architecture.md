# Firmware architecture

```
src/
├── main.cpp                 setup(): bring up board, UI, BLE; loop(): LVGL + UI refresh
├── config.h                 every pin, timing and tunable in one place
├── settings.{h,cpp}         NVS-backed user settings (Preferences, namespace "batmon")
├── history.{h,cpp}          two-tier PSRAM ring buffers of V / aux V / SoC / I for the chart page
├── board/                   Waveshare board support
│   ├── tca9554.*            IO expander
│   ├── display.*            ST7701 init over 3-wire SPI + esp_lcd RGB panel + backlight PWM
│   ├── touch.*              CST820
│   └── lvgl_port.*          LVGL 8.3 display/indev glue, tick timer, LVGL mutex
├── batmon/
│   ├── batmon_protocol.*    pure encode/decode of the BatMon wire format (no BLE deps)
│   ├── batmon_state.h       snapshot struct shared between tasks
│   └── batmon_client.*      NimBLE central task: scan / connect / poll / relay control
└── ui/
    └── ui.*                 the four LVGL pages
include/lv_conf.h            LVGL configuration (16-bit colour, Montserrat 14–48)
test/test_protocol/          Unity tests for batmon_protocol (host, `pio test -e native`)
```

## Tasks and threading

| Task | Core | Priority | Does |
|---|---|---|---|
| `loopTask` (Arduino `loop()`) | 1 | 1 | `lv_timer_handler()`, touch polling, calls `ui::update()` every 250 ms with a fresh snapshot |
| `batmon_ble` | 0 | 2 | everything NimBLE: scanning, connecting, sequential polling, executing queued commands |
| NimBLE host task | 0 | — | created by NimBLE-Arduino |
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
the line. Timing is `millis()`-based — windows are "relative to now" — and
everything is lost on reboot; SD-card persistence is on the roadmap.

`history::window(range, offset, out[240])` returns one chart's worth of
points, averaged over the stride, with `offset` windows back from now.

## UI

Four tiles in an `lv_tileview`, swiped horizontally. All widgets are kept
inside the visible circle of the round panel (roughly a 440 px diameter).

| Tile | Contents |
|---|---|
| Halo | 270° SoC arc coloured green/amber/red; SoC % with the unit on a shared baseline; **Main** and **Aux** voltages side by side; current with charge/discharge arrow (blue = charging, amber = discharging); power; external temperature; time-to-empty/full; **Switch** output toggle; red **charge-mismatch alert** (aux > 13.0 V while main current < 0.2 A); link status dot |
| Details | every raw reading, RSSI, poll counters, MAC; **Relay** and **Switch** toggles |
| Chart | line chart of Main V / Aux V / SoC / Amps (toggle buttons), **Hour / Day / Week / Month** range buttons and ◀ ▶ to scroll one range at a time. Left axis is volts (auto-ranged); right axis is SoC % when SoC is shown, otherwise amps (auto-ranged). When both SoC and amps are on, amps are scaled onto the SoC axis and the scale is printed under the chart |
| Setup | capacity ±1/±10 Ah, brightness slider, °C/°F, **Pause BLE 5 min / Resume**, **Forget device**, firmware version |

Readings older than 10 s are drawn grey so a frozen link is obvious.
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
