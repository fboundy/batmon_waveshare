# batmon_waveshare

An open-source **BatMon Halo**-style display for the
[BatMon](https://monitor-things.com/products/batmon-30a) BLE battery monitor,
running on Waveshare ESP32-S3 display boards:

| Board | Screen | Input | Status |
|---|---|---|---|
| [ESP32-S3-Touch-LCD-2.1](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-2.1) | 2.1" round 480 × 480, ring SoC gauge | touch | working |
| [ESP32-S3-LCD-1.9](https://www.waveshare.com/wiki/ESP32-S3-LCD-1.9) | 1.9" landscape 320 × 170, bar SoC gauge | BOOT button + serial console | builds, untested |

It connects to a BatMon over Bluetooth LE, stays connected, and shows:

* State of charge (270° ring + %), main and aux battery voltage, current with
  charge/discharge direction, power, battery temperature, time to empty /
  full, a **switch output** toggle, and an alert when the aux battery is
  charging but the main one is not
* A details page with every raw reading and **relay / switch control**
* A chart page: main V / aux V / SoC / current over the last hour, day, week
  or month, scrollable; history is saved to flash every five minutes
* A setup page for battery capacity, brightness, °C/°F, and a **Pause BLE**
  button that lets the phone app in for five minutes

The BLE protocol was reverse-engineered from the official
[Home Assistant integration](https://github.com/ringonotts/batmon_ha) — see
[docs/02-ble-protocol.md](docs/02-ble-protocol.md).

> **Status:** working against a real BatMon 30A (connects, polls, shows live
> readings). See [docs/07-session-log.md](docs/07-session-log.md) for the
> current state and [docs/06-roadmap.md](docs/06-roadmap.md) for what's next.

## Quick start

```sh
pip install platformio
pio run -t upload                              # 2.1 (default env), flash over USB-C
pio run -e waveshare_s3_lcd_1_9 -t upload      # 1.9
pio device monitor                             # 115200 baud; type 'help' for the console
```

Swipe left on the display for the chart, again for details, again for setup. Enter your battery
bank capacity in Ah to enable the SoC reading. The first BatMon found is
remembered; use *Forget device* to change it.

## Documentation

Everything is in [`docs/`](docs/README.md) — start with [docs/07-session-log.md](docs/07-session-log.md) for current status;
hardware, protocol, architecture, build/flash, Halo feature parity, roadmap.

## Layout

```
platformio.ini      pioarduino platform, Arduino core 3.x, LVGL 8.3, NimBLE-Arduino 2.x
include/lv_conf.h   LVGL config
src/boards/         per-board pin maps (2.1, 1.9)
src/board/          board interface + drivers (ST7701 RGB, ST7789 SPI, CST820, TCA9554, RTC, button, LVGL port)
src/batmon/         BatMon protocol codec + NimBLE client
src/ui/             LVGL page logic + one layout file per form factor
src/console.cpp     serial command console
test/               host unit tests for the codec
docs/               documentation
```

## Credits

* Protocol: [ringonotts/batmon_ha](https://github.com/ringonotts/batmon_ha)
* Display init sequence and pin map: Waveshare `ESP32-S3-Touch-LCD-2.1-Demo`
* BatMon and Halo are products of The Monitor of Things; this project is not
  affiliated with them.

## License

MIT — see [LICENSE](LICENSE).
