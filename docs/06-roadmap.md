# Roadmap and open questions

## Needs a real BatMon to confirm (please report!)

1. ~~Does it connect and poll?~~ **Yes** — verified 2026-09-11 with a BatMon 30A.
2. **Enclosing service UUID** of the `0303` / `0105` characteristics — the
   firmware logs it. Once known it can be used to shortcut discovery.
3. **Epoch byte order** in `MIN` / `MAX` replies (LE assumed).
4. **Manufacturer data payload** in the advertisement — capture the raw
   bytes from a scanner (nRF Connect). If it carries live readings the
   display could work connection-less, which would also solve the
   one-central-at-a-time limitation.
5. **Multiple centrals** — does a BatMon accept a phone *and* this display
   at the same time?

## Planned features

### Display
- [ ] Auto-dim / night mode (time-of-day via RTC, or ambient via a schedule)
- [ ] Screen-off after inactivity with tap-to-wake (touch INT on GPIO16)
- [ ] Smoothed current / power (EMA) for a calmer runtime estimate
- [x] History chart (V / aux V / SoC / I; hour–month)
- [x] Persist history (LittleFS, every 5 min; RTC measures the reboot gap)
- [ ] Real timestamps on the chart axis — needs a time source (NTP over
      Wi-Fi, or set the RTC from the phone / a BatMon epoch) and an RTC
      backup battery on the header so power loss doesn't reset it
- [ ] Hardware 180° rotation via ST7701 MADCTL instead of the software flip
- [x] Charge-mismatch alert (aux charging, main not) on the main page
- [ ] Configurable low-SoC / low-voltage alarm using the onboard buzzer;
      optionally beep on the charge-mismatch alert too
- [ ] Multiple BatMon support: swipe between banks (e.g. leisure + starter)

### Connectivity
- [ ] Wi-Fi + MQTT publisher (Home Assistant discovery) so the display can
      replace the HA custom component's BLE polling entirely
- [ ] Wi-Fi OTA (ArduinoOTA / ElegantOTA)
- [ ] Web page for settings instead of on-screen buttons
- [ ] BLE advertisement decoding if it turns out to carry readings (see above)

### BatMon protocol
- [ ] Reset min/max (`Mode::ResetMinMax = 23`) button — untested, may need
      the device API rather than the sensor characteristic
- [ ] Explore other device-API function ids (calibration, thresholds) —
      only with a BatMon you don't mind mis-configuring

### Housekeeping
- [x] CI: GitHub Actions running `pio run` and `pio test -e native`
- [ ] Battery (LiPo) percentage on the Setup page using GPIO4
- [ ] Unit tests for `Client::computeDerived` by splitting the maths out of
      the client
