# batmon_waveshare

An open-source **BatMon Halo**-style display for the
[BatMon](https://monitor-things.com/products/batmon-30a) BLE battery monitor,
running on the Waveshare
[ESP32-S3-Touch-LCD-2.1](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-2.1)
(2.1" round 480 × 480 touchscreen).

It connects to a BatMon over Bluetooth LE, stays connected, and shows:

* State of charge (270° ring + %), voltage, current with charge/discharge
  direction, power, battery temperature, time to empty / full
* A details page with every raw reading and **relay / switch control**
* A setup page for battery capacity, brightness, °C/°F, and a **Pause BLE**
  button that lets the phone app in for five minutes

The BLE protocol was reverse-engineered from the official
[Home Assistant integration](https://github.com/ringonotts/batmon_ha) — see
[docs/02-ble-protocol.md](docs/02-ble-protocol.md).

> **Status:** compiles and runs the UI; the BLE client implements the protocol
> exactly as the HA integration does but has **not yet been verified against a
> physical BatMon**. See [docs/06-roadmap.md](docs/06-roadmap.md) for what to
> check first.

## Quick start

```sh
pip install platformio
pio run -t upload          # flash over USB-C
pio device monitor         # 115200 baud
```

Swipe right on the display for details, again for setup. Enter your battery
bank capacity in Ah to enable the SoC reading. The first BatMon found is
remembered; use *Forget device* to change it.

## Documentation

Everything is in [`docs/`](docs/README.md):
hardware, protocol, architecture, build/flash, Halo feature parity, roadmap.

## Layout

```
platformio.ini      pioarduino platform, Arduino core 3.x, LVGL 8.3, NimBLE-Arduino 2.x
include/lv_conf.h   LVGL config
src/board/          Waveshare board support (TCA9554, ST7701 RGB panel, CST820, LVGL port)
src/batmon/         BatMon protocol codec + NimBLE client
src/ui/             LVGL pages
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
