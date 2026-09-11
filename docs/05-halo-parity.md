# BatMon Halo feature parity

The [BatMon Halo](https://monitor-things.com/products/batmon-halo) is the
official companion display: a 1.75" 466 × 466 AMOLED touchscreen, USB-C
powered, that connects to a BatMon over BLE and shows live readings.
This project reproduces it on the 2.1" 480 × 480 Waveshare panel.

## What Halo does (from the product page)

| Halo feature | Status here | Notes |
|---|---|---|
| Battery state of charge | ✅ | Same formula as the HA integration; needs bank capacity entered on the Setup page |
| Battery voltage | ✅ | |
| Charging / discharging current | ✅ | Arrow + colour indicates direction |
| Power (W) | ✅ | |
| Battery temperature | ✅ | External thermistor reading; °C / °F selectable |
| Bright touchscreen, day/night readable | ✅ / ➖ | Brightness slider; automatic dimming is on the roadmap |
| Wireless BLE connection | ✅ | |
| Automatic reconnection | ✅ | 2 s backoff, indefinite retries |
| Simple USB-powered installation | ✅ | Any 5 V USB-C supply; the board also takes a LiPo |
| Works with every BatMon | ✅ | Verified with a BatMon 30A; other variants share the protocol |
| Multiple Halos on one BatMon | ❓ | Unknown whether BatMon accepts several centrals; see [02-ble-protocol.md §7](02-ble-protocol.md#7-concurrency-caveat) |
| OTA firmware update from the BatMon app | ❌ | Not applicable; flash over USB. Wi-Fi OTA is on the roadmap |
| Easy retrofit to existing BatMon systems | ✅ | Nothing on the BatMon side changes |

## Extras beyond Halo

Already implemented:

* **Second battery voltage** (BatMon's external voltage input) on the main
  page, labelled Main / Aux.
* **Switch output toggle** on the main page; relay on the Details page.
* **Charge-mismatch alert** on the main page when the aux battery is being
  charged (> 13.0 V) but the main battery is not (current < 0.2 A) — e.g.
  a split-charge relay or DC-DC charger not engaging.
* **History chart page** — Main V, Aux V, SoC and current over the last
  Hour / Day / Week / Month, scrollable back through 24 h of 15 s samples and
  30 days of 6 min samples (RAM only; lost on reboot).
* **Details page** — raw Ah counter, full/min reference, external voltage,
  BatMon CPU temperature, RSSI, poll statistics, MAC address.
* **Relay and switch control** from the display (the HA switch entities).
* **Time to empty / time to full** from the present current.
* **Pause BLE** button so the phone app can connect without power-cycling
  the display.
* **Forget device** to re-pair with a different BatMon.
* Stale-data greying when the link stalls.

See [06-roadmap.md](06-roadmap.md) for what is planned.
