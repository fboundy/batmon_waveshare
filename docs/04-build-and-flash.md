# Build, flash, debug

The project is a [PlatformIO](https://platformio.org/) project using the
[pioarduino](https://github.com/pioarduino/platform-espressif32) platform
(Arduino-ESP32 core 3.x / ESP-IDF 5.x). Everything is pinned in
`platformio.ini`.

## Prerequisites

* Python 3.9+ and `pip install platformio` (or the PlatformIO VS Code extension)
* USB-C cable to the **USB** port (native USB, not the UART port) — either
  works for flashing, the native one gives the serial log via CDC.
* Windows only: PlatformIO unpacks the Arduino core into paths longer than
  260 characters. Either enable long paths
  (`HKLM\SYSTEM\CurrentControlSet\Control\FileSystem\LongPathsEnabled = 1`)
  or point PlatformIO at a short directory:

  ```powershell
  $env:PLATFORMIO_CORE_DIR = "C:\pio"
  ```

* Windows only: run `pio` from **PowerShell or cmd**, not Git Bash / MSYS.
  The pioarduino platform installs the Xtensa toolchain via Espressif's
  `idf_tools.py`, which refuses to run under MSYS and the platform then
  reports only `idf_tools.py installation failed` followed by
  `'xtensa-esp32s3-elf-gcc' is not recognized`.

## Build

Two hardware environments, one per board:

| env | board | notes |
|---|---|---|
| `waveshare_s3_lcd_2_1` (default) | ESP32-S3-Touch-LCD-2.1, round 480×480, touch | verified on hardware |
| `waveshare_s3_lcd_1_9` | ESP32-S3-LCD-1.9, landscape 320×170, no touch | builds; not yet run — see [08-hardware-lcd-1-9.md](08-hardware-lcd-1-9.md) |

```sh
pio run                                   # builds the default env (2.1)
pio run -e waveshare_s3_lcd_1_9           # the 1.9
pio run -e waveshare_s3_lcd_1_9 -t upload # build + flash (auto-detects the port)
pio device monitor                        # 115200 baud, exception decoder enabled
```

First build downloads the toolchain and Arduino core (~1 GB) and takes
several minutes; subsequent builds are incremental.

If the board does not enter download mode automatically: hold **BOOT**, press
and release **RESET**, release **BOOT**, then upload.

## Host unit tests

The protocol codec has no hardware dependencies:

```sh
pio test -e native
```

This needs a native C/C++ compiler on the PATH (GCC/Clang/MinGW). Without one
you can still validate the byte layout with the HA Python classes — the same
vectors are exercised in `docs/02-ble-protocol.md`.

## Fonts

LVGL's built-in Montserrat stops at 48 px. Larger glyphs are generated from
the TTF that ships inside the LVGL library with `tools/gen_font.py`
(needs only Pillow — `lv_font_conv` needs node, which this project avoids):

```sh
python tools/gen_font.py   --ttf .pio/libdeps/waveshare_s3_lcd_2_1/lvgl/scripts/built_in_font/Montserrat-Medium.ttf   --size 72 --chars "0123456789-" --name lv_font_montserrat_72_digits   -o src/ui/font_montserrat_72_digits.c
```

It emits the `lv_font_fmt_txt` format at 4 bpp (bit stream continuous
across rows, byte-padded per glyph; `ofs_y` measured from the baseline) with
`line_height = 1.125 x size` and `base_line = 0.21 x size` to match the
built-in Montserrat metrics. Only the listed characters exist in the font,
so use it for labels that only ever show those.

## Configuration

All tunables are in `src/config.h`:

| Define | Default | Meaning |
|---|---|---|
| `BLE_SCAN_MS` | 6000 | length of one scan pass |
| `BLE_POLL_FAST_MS` | 1000 | V/I/T/Ah/aux V/switch poll period (overridden by the `pollMs` setting) |
| `BLE_POLL_SLOW_MS` | 30000 | Ah max/min, CPU temp, relay |
| `CHART_REFRESH_MS` | 5000 | how often the chart page redraws while visible |
| `HISTORY_SAVE_MS` | 300000 | how often the history buffers are written to LittleFS |
| `LCD_ROTATE_180` | 1 | rotate UI and touch by 180° (board mounted USB-up) |
| `PRESENCE_TIMEOUT_MS` | 90000 | a phone counts as present this long after its last advert |
| `BOOT_GRACE_MS` | 60000 | gate open after boot regardless of phones |
| `LEAVE_HOLD_MS` | 5000 | BatMon link kept this long after the last phone leaves (relay-off goes out first) |
| `WAKE_MS` | 30000 | touch/button wakes a standby screen for this long |
| `OBD_POLL_MS` | 1000 | OBD-II poll round period |
| `OBD_REPLY_TIMEOUT_MS` | 2500 | wait for the ELM327 `>` prompt |
| `PAIRING_WINDOW_MS` | 120000 | how long "Pair new phone" advertises |
| `CHG_AUX_CHARGING_V` | 13.2 | aux voltage above which a charger is considered running |
| `CHG_MAIN_CURRENT_A` | 0.05 | main current above which the main is charging |
| `CHG_FULL_SOC` / `CHG_FULL_MAIN_V` | 95 % / 14.4 V | main considered full (charge icon yellow instead of red) |
| `BLE_PAUSE_DEFAULT_MS` | 300000 | "Pause BLE" duration |
| `DATA_STALE_MS` | 10000 | readings older than this are greyed out |
| `BL_DEFAULT_PERCENT` | 80 | backlight until the saved setting is loaded |

Chart history lives in `/history.bin` on the LittleFS partition and is also
kept across re-flashing (`pio run -t erase` wipes it).

User settings (capacity, preferred device, brightness, units, poll period,
chart series/range)
live in NVS and survive re-flashing unless the NVS partition is erased
(`pio run -t erase`).

## Serial console

The firmware reads commands on the same USB serial port (115200, newline
terminated). This is the only way to change settings on the touch-less
1.9; it works on the 2.1 too.

| Command | Effect |
|---|---|
| `status` | link state, latest readings, settings, history status |
| `cap <Ah>` | battery capacity for SoC (0 = unknown) |
| `bright <0-100>` | backlight |
| `degf <0\|1>` | temperature units |
| `poll <ms>` | fast poll period, 250–10000 |
| `chart <series> <range>` | e.g. `chart main,soc day` — series from main/aux/soc/amps, range hour/day/week/month |
| `switch on\|off`, `relay on\|off` | BatMon outputs |
| `pause [minutes]`, `resume` | release the BatMon for the phone app |
| `forget` | forget the preferred BatMon and rescan |
| `save` | flush history to flash now |
| `page` | next page |
| `phone list` / `phone pair` / `phone stop` / `phone forget <n\|all>` / `phone name <n> <name>` | phone presence, see [09-phone-presence.md](09-phone-presence.md) |
| `relayphone 0\|1` | relay follows phone presence |
| `beacon scan` / `beacon list` / `beacon add <n\|uuid> [major] [minor] [name]` | iBeacon tags as presence sources |
| `obd status\|list\|connect <n>\|disconnect\|forget\|on\|off\|send <cmd>\|pids\|log <0\|1>` | OBD-II adapter, see [10-obd.md](10-obd.md) |
| `help` | list |

`pio device monitor` sends what you type when you press Enter.

## Serial log

`CORE_DEBUG_LEVEL=3` (INFO). On connect the firmware prints the BatMon's full
GATT table; that output is worth pasting into an issue if something does not
work:

```
I batmon: connecting to aa:bb:cc:dd:ee:ff 'BK-Leisure'
I batmon: service 0000xxxx-8e22-4541-9d4c-21edae82ed19
I batmon:    chr 00000303-8e22-4541-9d4c-21edae82ed19 r=1 w=1 n=0
I batmon:    chr 00000105-8e22-4541-9d4c-21edae82ed19 r=0 w=1 n=0
```

## Arduino IDE instead of PlatformIO

Not directly supported, but the sources are plain Arduino-compatible C++:
copy `src/` into a sketch folder, rename `main.cpp` to `<sketch>.ino`, put
`include/lv_conf.h` next to the LVGL library, install **lvgl 8.3.x** and
**NimBLE-Arduino 2.x**, and use board *ESP32S3 Dev Module* with
OPI PSRAM, 16 MB flash, USB CDC on boot enabled.

## Custom ESP-IDF configuration (branch `feature/idf-hybrid`)

The 2.1 env sets `custom_sdkconfig` in `platformio.ini`. pioarduino then
rebuilds the Arduino core libraries from ESP-IDF source with those options
(first build ~10 min, needs ~2 GB for `framework-espidf`; later builds are
normal) and the sketch compiles exactly as before. Options and why:

| Option | Why |
|---|---|
| `CONFIG_SPIRAM_MODE_OCT` | the rebuild ignores the board's memory type; this board has octal PSRAM |
| `CONFIG_SPIRAM_BOOT_INIT` | required with XIP: PSRAM must be up before the Arduino core's lazy init |
| `CONFIG_SPIRAM_XIP_FROM_PSRAM` (+ `FETCH_INSTRUCTIONS`, `RODATA`) | flash writes (history save, NVS) no longer stall PSRAM, so the RGB panel keeps streaming |
| `CONFIG_LCD_RGB_ISR_IRAM_SAFE`, `CONFIG_GDMA_ISR_IRAM_SAFE`, `CONFIG_GDMA_CTRL_FUNC_IN_IRAM` | the panel's bounce-buffer refill ISR keeps running during flash writes |
| `CONFIG_LCD_RGB_RESTART_IN_VSYNC` | already on in the stock core; kept explicit |
| `CONFIG_COMPILER_OPTIMIZATION_PERF` | speed over size |

`custom_component_remove` drops the RainMaker/Insights/ESP-SR/Zigbee/etc.
managed components the lib builder would otherwise pull in (one of them has
an embedded-file path bug on Windows). `default_16MB.csv` lives in the
project root because the lib builder wants it there.

Quirks: when running via `python -m platformio` the automatic re-run after
the core rebuild fails with `"None" is not recognized` - just run `pio run`
again. The rebuilt libraries replace the stock ones inside
`framework-arduinoespressif32-libs`; delete that package to go back.

**Linker script:** the core rebuild also generates a `sections.ld` matching
the new sdkconfig (it places the IRAM-safe LCD/GDMA functions) and leaves it
in `.pio/build/<env>/`. The stock `framework-arduinoespressif32-libs/esp32s3/ld/sections.ld`
is *not* updated, so a clean build of another env fails with
`dangerous relocation: l32r: literal placed after use`. Fix: copy the generated
file over the stock one (`ld/sections.ld`) once. Both envs share one
rebuilt core, so `custom_sdkconfig` lives in the common section.
