# Documentation

| Doc | What's in it |
|---|---|
| [01-hardware.md](01-hardware.md) | Waveshare ESP32-S3-Touch-LCD-2.1: chips, pin map, display init, touch, expander |
| [02-ble-protocol.md](02-ble-protocol.md) | BatMon BLE protocol reverse-engineered from the Home Assistant integration: discovery, characteristics, byte layouts, SoC formula |
| [03-architecture.md](03-architecture.md) | Firmware structure, tasks, BLE state machine, UI pages, rendering |
| [04-build-and-flash.md](04-build-and-flash.md) | PlatformIO build, flashing, host tests, serial log |
| [05-halo-parity.md](05-halo-parity.md) | Feature-by-feature comparison with the BatMon Halo and what's extra |
| [06-roadmap.md](06-roadmap.md) | Open questions that need a real BatMon, planned features |
| [07-session-log.md](07-session-log.md) | What has been done, decided and verified so far; next steps (read this first when picking the project up) |

## Sources

* BatMon HA integration — https://github.com/ringonotts/batmon_ha (protocol)
* Waveshare wiki — https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-2.1 (board, demo code)
* BatMon 30A — https://monitor-things.com/products/batmon-30a
* BatMon Halo — https://monitor-things.com/products/batmon-halo
