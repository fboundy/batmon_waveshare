# Firmware architecture

```
src/
├── main.cpp                 setup(): bring up board, UI, BLE; loop(): LVGL + UI refresh
├── config.h                 every pin, timing and tunable in one place
├── settings.{h,cpp}         NVS-backed user settings (Preferences, namespace "batmon")
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
    └── ui.*                 the three LVGL pages
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
* **Connected** — fast poll every `pollMs` (default 1 s), slow poll every
  30 s, derived values recomputed after each fast poll. Commands are drained
  between polls.
* **Paused** — connection dropped on purpose for N minutes so the phone app
  can connect. `Resume` or the timer returns to Scanning.

## Derived values

Computed in `Client::computeDerived()`:

* `watts = volts × current` (signed, like HA)
* `soc` = see [02-ble-protocol.md §5](02-ble-protocol.md#5-state-of-charge); `-1` if capacity is 0
* `hoursRemaining` = remaining Ah ÷ |current| when discharging, missing Ah ÷ current when charging, `-1` when |I| < 50 mA.
  This is the instantaneous estimate; smoothing is on the roadmap.

## UI

Three tiles in an `lv_tileview`, swiped horizontally. All widgets are kept
inside the visible circle of the round panel (roughly a 440 px diameter).

| Tile | Contents |
|---|---|
| Halo | 270° SoC arc coloured green/amber/red, big SoC %, voltage, current with charge/discharge arrow (blue = charging, amber = discharging), power, external temperature, time-to-empty/full, link status dot |
| Details | every raw reading, RSSI, poll counters, MAC; **Relay** and **Switch** toggles |
| Setup | capacity ±1/±10 Ah, brightness slider, °C/°F, **Pause BLE 5 min / Resume**, **Forget device**, firmware version |

Readings older than 10 s are drawn grey so a frozen link is obvious.

## Rendering

LVGL is given the two PSRAM frame buffers that belong to the RGB driver and
runs in `full_refresh` mode. A flush therefore calls
`esp_lcd_panel_draw_bitmap()` with a pointer the driver already owns, which
it recognises and turns into a buffer swap at the next VSYNC — no memcpy.
This is the same scheme Waveshare's demo uses and it is tear-free at 16 MHz.

Cost: every LVGL redraw touches the whole 480 × 480 × 2 B buffer in PSRAM
(~460 KB). At a 250 ms UI refresh this is negligible.
