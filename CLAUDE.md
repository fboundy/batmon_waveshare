# CLAUDE.md — working notes for this repo

Read `docs/07-session-log.md` first for where the project stands; `docs/` has
everything else (hardware, protocol, architecture, build, roadmap).

## What this is
Firmware for the Waveshare ESP32-S3-Touch-LCD-2.1 that mimics the BatMon Halo
display: connects to a BatMon battery monitor over BLE and shows SoC / V / A /
W / temperature, plus relay control and settings. Protocol was taken from the
BatMon Home Assistant integration (https://github.com/ringonotts/batmon_ha) —
that repo is the source of truth for comms; don't go hunting vendor PDFs for it.

## Build / flash / monitor
```
pio run                                   # build
pio run -t upload                         # flash (auto-detects port)
pio device monitor --baud 115200          # serial log
pio test -e native                        # host tests for the protocol codec (needs gcc)
```
Windows gotchas (both cost real time the first session):
- run from **PowerShell / cmd, not Git Bash** — pioarduino's idf_tools.py refuses MSYS
  and the only symptom is "idf_tools.py installation failed" / missing xtensa gcc.
- set `PLATFORMIO_CORE_DIR` to a short path (e.g. `C:\pio`) unless Windows long paths
  are enabled, or the Arduino core unpack fails on a >260-char path.
- CI pins `click<8.2` because esptool 5.0-dev breaks with click 8.2.

## Layout
- `src/batmon/batmon_protocol.*` — pure codec, no BLE deps, unit-tested. Change with care;
  test vectors were cross-checked against the HA Python classes.
- `src/batmon/batmon_client.*` — NimBLE 2.x central task (core 0). All BLE calls happen here.
- `src/board/*` — TCA9554, ST7701 RGB panel (init table copied from Waveshare demo), CST820, LVGL 8.3 port.
- `src/ui/ui.cpp` — four tileview pages (Halo, Details, Chart, Setup). Only the loop task touches LVGL.
- `src/history.*` — PSRAM ring buffers behind the chart page; fed from the BLE task.
- `src/config.h` — every pin and tunable.
- `include/lv_conf.h` — LVGL config (fonts 14–48 enabled, demos off).

## Conventions
- All wire fields little-endian; see docs/02 for the one HA quirk (epoch read BE, unused).
- UI ↔ BLE only via `Client::snapshot()` and the command queue; never call NimBLE from the UI task.
- Keep docs in `docs/` updated when behaviour or protocol knowledge changes.
- Commit messages end with the Co-Authored-By line when Claude writes them.
