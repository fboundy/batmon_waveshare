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

### Unflashed change
Commit `ef11e63` (advertiser dump at debug level) was pushed but the board
still runs the previous build; behaviour is identical at the default log
level, so reflash whenever convenient.
