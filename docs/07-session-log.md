# Session log / handoff

Chronological record of what has been done, decided and verified, so work can
continue on another machine. Newest entries at the bottom.

## 2026-09-11 — initial build (Windows PC, Claude Code)

### Done
- Reverse-engineered the BatMon BLE protocol from `ringonotts/batmon_ha`
  (`batmon.py`, `const.py`, `manifest.json`). Written up in
  [02-ble-protocol.md](02-ble-protocol.md). Byte-level test vectors were run
  through the HA Python classes (`CPopByteArray`, `CPPushByteArray`,
  `BatmonSensorCommand`) and match the C++ codec.
- Board support written from the Waveshare wiki + `ESP32-S3-Touch-LCD-2.1-Demo`
  (`LVGL_Arduino` example): pin map, ST7701 init table, RGB timings, CST820,
  TCA9554. See [01-hardware.md](01-hardware.md).
- Firmware: NimBLE central task, LVGL 8.3 three-page UI, NVS settings.
  See [03-architecture.md](03-architecture.md).
- Repo published: https://github.com/fboundy/batmon_waveshare (public, MIT).
  GitHub Actions runs the native codec tests and the firmware build and
  uploads `firmware.bin` / `bootloader.bin` / `partitions.bin` as artifacts.

### Verified on hardware
- Flashed over the native USB port (Windows enumerates it as
  `USB Serial Device`, VID:PID `303A:1001`, board serial `CC:BA:97:05:5A:54`).
- Boots, display and touch initialise, BLE scan runs every ~8 s and lists
  ~12 nearby advertisers. Firmware build metrics: RAM 40.5 %, flash 17 %.
- **Not yet verified:** any actual BatMon traffic. The BatMon was out of
  range during this session, so the connect / GATT discovery / polling path
  has only been exercised against the protocol as read from the HA code.

### Decisions and why
| Decision | Reason |
|---|---|
| PlatformIO + pioarduino platform (Arduino core 3.2.1 / IDF 5.4) | Waveshare demo needs IDF 5 `esp_lcd` RGB APIs and core-3 `ledcAttach`; official PlatformIO espressif32 is stuck on core 2.x |
| NimBLE-Arduino 2.x, peripheral/broadcaster roles compiled out | Much smaller than Bluedroid; we only ever act as central |
| LVGL renders straight into the RGB driver's two PSRAM frame buffers, `full_refresh=1` | Same as Waveshare demo; flush becomes a VSYNC buffer swap, tear-free |
| Stay connected and poll every 1 s (HA connects/polls/disconnects every 60 s) | A display wants live data; cost is that the phone app can't connect while we hold the link, hence the Pause-BLE button |
| SoC formula copied from HA, capacity entered on-device | BatMon does not report SoC; HA's `100 + (Ah − Ah_max)/capacity × 100` is the only known method |
| Preferred device = first BatMon seen, stored in NVS; Forget button to change | Simple pairing without a picker UI; multi-BatMon is on the roadmap |
| Epoch in MIN/MAX replies decoded little-endian | HA reads it big-endian but never uses it; every other field is LE. Unverified |

### Environment quirks discovered
- Windows path length (>260) breaks the Arduino core unpack → `PLATFORMIO_CORE_DIR=C:\pio`.
- pioarduino's `idf_tools.py` refuses to run under Git Bash/MSYS → use PowerShell.
- No native compiler on the Windows PC → `pio test -e native` runs only in CI
  (it passes there, 8/8).
- `click` 8.2 breaks the bundled esptool → CI pins `click<8.2`.

### Next steps (in order)
1. Take the display within range of the BatMon with the phone app **fully closed**.
   Watch `pio device monitor`: expect `BatMon candidate …`, `connecting to …`, the
   GATT table (`service …` / `chr …` lines), then `pollOk` climbing on the
   Details page. Paste the GATT table into [02-ble-protocol.md](02-ble-protocol.md) §2.
2. If it never becomes a candidate, rebuild with `-D CORE_DEBUG_LEVEL=4` in
   `platformio.ini`; every advertiser is then logged as
   `adv <mac> rssi name mfg=[hex] svc=[uuids]` — see how the BatMon actually
   advertises and adjust `isBatMon()` in `src/batmon/batmon_client.cpp`.
3. Check the sign convention of current (expected +charge / −discharge) and
   that the SoC reading agrees with the phone app once capacity is entered.
4. Then the roadmap in [06-roadmap.md](06-roadmap.md) (screen sleep, smoothing,
   Wi-Fi/MQTT, OTA).

## 2026-09-11 (later) - first contact with the BatMon, v0.2.0

### Verified
- Display taken to the vehicle: connected to `BK-Battery1`, showed 100 %,
  13.19 V, discharging 0.9 A / 12 W, 17.2 C, "31h 56m to empty",
  Connected. **The protocol as implemented from the HA integration is
  confirmed working.** Current sign convention (negative = discharge) is
  as expected.
- Serial log with the GATT table was not captured on that run (board was
  on vehicle USB) - still wanted for docs/02 section 2.

### Bug found from the photo
- The SoC `%` sat low and to the right of the digits: it had been placed
  with `lv_obj_align_to()` once, relative to the placeholder `--`, and never
  moved when the digits got wider. Fixed by putting digits + unit in a flex
  row with bottom alignment and a baseline correction.

### Added (v0.2.0)
- Halo page: SoC moved up, `%` baseline-aligned; **Main** and **Aux**
  voltage side by side with captions; **Switch** toggle; red alert when aux
  > 13.0 V while main current < 0.2 A (`ALERT_*` in `config.h`).
- Aux voltage and switch state moved into the 1 s poll group.
- `src/history.*`: 15 s x 24 h and 6 min x 30 d ring buffers in PSRAM.
- New **Chart** page (3rd tile): Main V / Aux V / SoC / Amps toggles,
  Hour / Day / Week / Month, scroll arrows. Selections persist in NVS.
- Switch widgets hold the user's tap for 3 s before re-syncing from the
  device.

### Flashed
- v0.2.0 first flash boot-looped: `ui::create()` builds the chart page,
  which calls `history::window()`, before `history::begin()` had created
  its mutex (`assert xQueueSemaphoreTake`). Fixed by initialising history
  before the UI in `main.cpp`. Reflashed; boots clean, scans, no BatMon in
  range at the desk. **Not yet seen against the BatMon** - check the new
  Halo layout, the Aux voltage, the switch and the chart on the vehicle.
- Lesson: anything the UI reads at build time must be initialised before
  `ui::create()`.
- Note: if the board is boot-looping, the first `pio run -t upload` can
  lose the COM port mid-reset; just run it again.

### Halo page polish (v0.2.1)
- SoC digits 72 px (custom generated font, digits + hyphen only) with the
  32 px `%` on the baseline; voltages 40 px with 28 px `V` units; big
  100x48 switch; link state is now a Bluetooth glyph (green connected /
  red otherwise) in the arc's bottom gap instead of a dot + text.
- `tools/gen_font.py` written because there is no node for lv_font_conv.
  Pillow's `getbbox()` is not a tight ink box - measure the rendered glyph.
- Flashed and booting; not yet seen against the BatMon.

## 2026-09-12 - v0.3.0: rotation, page order, persistent history

- **180° rotation** (`LCD_ROTATE_180`): LVGL's `sw_rotate` refuses
  `full_refresh`, so the flush callback reverses the frame buffer in place
  and touch coordinates are mirrored. Hardware rotation via ST7701 MADCTL
  untested - roadmap.
- **Page order** now Halo, Chart, Details, Setup.
- **History persisted** to LittleFS (`/history.bin`, ~207 KB, every 5 min,
  temp-file + rename). NVS was ruled out (20 KB partition). A `hist_save`
  task does the flash writes so BLE polling never blocks. The PCF85063 RTC
  (new `src/board/rtc.*`) timestamps saves; on boot the elapsed time is
  turned into NaN gap samples. No RTC battery -> after a power loss the gap
  is unknown and assumed zero.
- Details page shows time since the last save.
- **Verified on the board:** first save `saved 20+0 samples in 3329 ms`;
  after a reflash `restored 20+0 samples, gap 105 s` (RTC measured the
  downtime) and the next save reported `47+1` = 20 restored + 7 gap + 20
  new, with the first tier-1 sample. A ~200 KB LittleFS write takes ~3.3 s
  on the background task; polling is unaffected.
- Rotation and page order not visually checked yet (no photo).

## 2026-09-12 - v0.4.0: second board (ESP32-S3-LCD-1.9), board/UI split

- User asked for a version for the Waveshare ESP32-S3-LCD-1.9 (non-touch
  variant, mounted landscape) with a **bar** SoC gauge, reusing as much as
  possible. The board is not available yet, so this is build-verified only.
- Refactor: `board::` interface with per-env implementations, generic
  `LvglPort` (direct vs partial/async), `ui.cpp` logic shared with two
  layout files, `src/boards/*.h` pin maps. The 2.1 code path is a straight
  move; both envs build clean (2.1: RAM 41.8 % flash 17.5 %; 1.9: RAM
  41.8 % flash 15.6 %).
- 1.9 specifics from the Waveshare demo: ST7789 SPI on GPIO 9-14, y gap 35,
  tuning registers B2..E1 + INVON, WS2812 on GPIO15, no RTC, BOOT = GPIO0.
  Driven through IDF's esp_lcd ST7789 driver with `LV_COLOR_16_SWAP=1`.
- New inputs for touch-less use, on both boards: BOOT short = next page,
  long = cycle chart range; USB serial console (`help`).
- **Not verified:** 2.1 regression flash - the board dropped off USB again
  before the upload; do `pio run -t upload` when it is back. 1.9 first-run
  checklist is in docs/08.

### Not yet done
- Real time on the chart axis needs a time source; see roadmap.
