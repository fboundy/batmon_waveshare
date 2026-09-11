# BatMon BLE protocol

Everything here was derived from the BatMon Home Assistant custom component
[ringonotts/batmon_ha](https://github.com/ringonotts/batmon_ha)
(`custom_components/batmon_bm/batmon.py`, `const.py`, `manifest.json`,
`config_flow.py`). No BatMon hardware was sniffed. Where the HA code is
ambiguous the ambiguity is called out.

The firmware implementation is `src/batmon/batmon_protocol.{h,cpp}`; the
byte-level test vectors in `test/test_protocol/test_protocol.cpp` were
cross-checked against the HA Python classes (`CPopByteArray`,
`CPPushByteArray`, `BatmonSensorCommand`) and match.

## 1. Discovery (advertising)

| Item | Value | Source |
|---|---|---|
| Advertised service UUID | `00000000-cc7a-482a-984a-7f2ed5b3e58f` | `manifest.json`, `config_flow.py` |
| Manufacturer ID | `4077` (`0x0FED`) | `manifest.json`, `const.py` `MFCT_ID` |
| Device name | `BK-<user name>` — HA strips the `BK-` prefix for display | `batmon.py` `BatMonDevice.__init__` |

The firmware treats a device as a BatMon if **any** of the above match.
The HA config flow only checks the service UUID; the manifest matcher uses
both. If your BatMon advertises with a different local name the service
UUID / manufacturer ID still catches it.

The contents of the manufacturer-specific data (after the 2-byte company ID)
are **not** decoded by the HA integration and are unknown. It is possible the
BatMon broadcasts live readings there (that would explain how "multiple Halo
displays can connect to a single BatMon"). If you can capture a BatMon
advertisement, please open an issue with the raw bytes — see
[06-roadmap.md](06-roadmap.md).

## 2. GATT characteristics

The HA integration addresses characteristics by UUID only, so the enclosing
**service** UUID is not documented. The firmware walks every service on
connect and logs the full table at `INFO` level (`batmon: service ... chr ...`).

| Characteristic | UUID | Use |
|---|---|---|
| Sensor command | `00000303-8e22-4541-9d4c-21edae82ed19` | Write a 3-byte request, then read the reply from the same characteristic |
| Device API | `00000105-8e22-4541-9d4c-21edae82ed19` | Write-only RPC used to drive the relay / switch output |

Both writes are done **with response** (`response=True` in bleak).
No notifications are used.

## 3. Sensor command (`0x0303`)

### Request — 3 bytes

```
offset  size  field
0       u8    sensor type   (see table)
1       u8    mode          (see table)
2       u8    len = 0
```

### Sensor types (`BmConst.Type`)

| Value | Name | Unit | Notes |
|---|---|---|---|
| 0 | `BAT_VOLTS` | V | main battery voltage |
| 1 | `EXT_VOLTS` | V | auxiliary voltage input |
| 2 | `INT_TEMP` | °C | BatMon PCB / CPU temperature (HA: "CPU Temperature") |
| 3 | `EXT_TEMP` | °C | external thermistor lead (HA: "External Sensor Temperature") |
| 4 | `BAT_CURRENT` | A | positive = charging, negative = discharging (HA multiplies by volts for watts, so sign is preserved) |
| 5 | `BAT_AMPHOURS` | Ah | coulomb counter. HA reports it as "Amp Hours" and also as "Watt Hours" (= Ah × V) |
| 6 | `RELAY_PIN` | 0/1 | relay output state |
| 7 | `SWITCH_PIN` | 0/1 | switch (IO) output state |

### Modes (`BmConst.Mode`)

| Value | Name | Used by HA | Reply payload |
|---|---|---|---|
| 0 | `VALUE` | yes | `f32` value |
| 1 | `MIN` | no (defined only) | `f32` min, `u32` epoch |
| 2 | `MAX` | yes (for `BAT_AMPHOURS`) | `f32` max, `u32` epoch |
| 20 | `LIN_EQU` | no | unknown — presumably a linear calibration |
| 21 | `TEMPCO` | no | unknown — temperature coefficient |
| 22 | `THRESHOLD` | no | unknown — alarm threshold |
| 23 | `RESET_MINMAX` | no | unknown — probably resets the min/max history |

Only `VALUE`, `MIN` and `MAX` are implemented in the firmware. The others are
listed for completeness; sending them is untested and could change device
configuration, so don't experiment blindly.

### Reply — read back from the same characteristic

```
offset  size  field
0       u8    sensor type   (echoes the request)
1       u8    mode          (echoes the request)
2       u8    len           (payload length)
3..     ...   payload
```

| Mode | Payload |
|---|---|
| `VALUE` | `f32` (4 bytes) |
| `MIN` / `MAX` | `f32` (4 bytes) + `u32` unix epoch (4 bytes) |

### Byte order

* **`f32` values are little-endian.** The HA decoder looks big-endian at first
  glance (`unpack('>f', pack('I', int(bits, 2)))`) but `pack('I', …)` uses
  *native* byte order, which on every host HA runs on is little-endian. The
  net effect is that the four wire bytes are interpreted as a little-endian
  float. Test vector: `CD CC 4C 41` → `12.8`.
* **The `u32` epoch is read big-endian by HA** (`int(popBin(32), 2)`), but
  HA never uses the value. Given every other multi-byte field (including the
  int32 arguments the HA code *writes*, which are explicitly LE) is
  little-endian, the firmware decodes the epoch as **little-endian**. This
  is unverified; the epoch is not displayed anywhere yet.

## 4. Device API (`0x0105`) — set relay / switch

The HA switch platform sends one RPC. Encoding (all little-endian):

```
offset  size  field
0       u16   api_ref = 606
2       u8    0x01          arg type tag: "int32 follows"
3       i32   io_type       2 = relay, 3 = switch
7       u8    0x01          arg type tag: "int32 follows"
8       i32   value         0 = off, 1 = on
12      u8    0x00          terminator
```

Examples:

| Action | Bytes |
|---|---|
| Relay on | `5E 02 01 02 00 00 00 01 01 00 00 00 00` |
| Switch off | `5E 02 01 03 00 00 00 01 00 00 00 00 00` |

After writing, HA reads `RELAY_PIN` / `SWITCH_PIN` (`VALUE`) to confirm the
new state. The firmware does the same.

The format looks like a generic RPC: `[u16 function id][tagged args…][0]`.
`606` is the only function id known. Other ids exist on the device (the app
does calibration / configuration through it) but none are documented here.

## 5. State of charge

The BatMon does not report SoC directly. HA computes it from the coulomb
counter:

```
max_ah    = read(BAT_AMPHOURS, MAX).value      # "full" reference
amp_hours = read(BAT_AMPHOURS, VALUE).value
tmp_ah    = max_ah if max_ah > 0 else 0
SoC (%)   = 100 + ((amp_hours - tmp_ah) / capacity_ah) * 100
```

`capacity_ah` is user-configured (HA config flow "battery_capacity"; the
firmware's Setup page). The firmware clamps the result to 0–100 and only shows
SoC once a capacity has been entered. If `MAX` has never been read the
reference is 0 (same as HA).

## 6. Polling pattern

HA connects, reads all ten mapped sensors sequentially (each a write + read),
then disconnects, every 60 s. The firmware instead stays connected and polls:

| Group | Sensors | Default period |
|---|---|---|
| fast | `BAT_VOLTS`, `BAT_CURRENT`, `EXT_TEMP`, `BAT_AMPHOURS` (VALUE), `EXT_VOLTS`, `SWITCH_PIN` | 1 s (`pollMs` setting) |
| slow | `BAT_AMPHOURS` (MAX, MIN), `INT_TEMP`, `RELAY_PIN` | 30 s |

Each read is a write-with-response followed by a read, i.e. two connection
events. With a 15–30 ms connection interval the fast group (12 round trips)
completes in well under 500 ms.

## 7. Concurrency caveat

The HA README states a BatMon only accepts one BLE central at a time and that
the phone app must be fully closed for HA to connect. The same applies to
this display: while it is connected the phone app cannot connect. The Setup
page has a **Pause BLE** button that disconnects for 5 minutes so the app can
be used, then reconnects automatically.

(The Halo marketing says multiple Halos can attach to one BatMon, which
suggests newer BatMon firmware allows several centrals *or* that Halo uses
advertisement data — unconfirmed.)

## 8. Verified on hardware (2026-09-11)

The connect / discover / poll path above has been run against a real BatMon
30A: it is discovered, connects, and returns sane values (13.19 V, −0.9 A,
17.2 °C, `BK-Battery1` name stripped to `Battery1`). The full GATT table has
not yet been captured from the serial log — see [06-roadmap.md](06-roadmap.md).
