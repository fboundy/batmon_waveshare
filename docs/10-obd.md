# OBD-II adapter (branch `feature/obd-ble`)

Connect a Bluetooth LE OBD-II dongle — design target the **Veepeak OBDCheck
BLE** — and show live engine data next to the battery data. The display is
a second BLE central alongside the BatMon link (NimBLE
`MAX_CONNECTIONS=3`: BatMon, OBD, and the pairing-service peripheral).

> **Status:** written blind — no adapter has been connected yet. The first
> job is diagnosis: which GATT characteristics the dongle uses, what the
> ELM327 firmware answers, and which PIDs the vehicle supports. Everything
> below is built so that this can be done from the serial console without
> reflashing.

## How these adapters work

Every cheap BLE OBD dongle is an **ELM327** (or clone) serial chip behind a
BLE "serial port": one characteristic you write ASCII commands to
(`ATZ\r`, `010C\r`), one that notifies the reply as text, ending in the
`>` prompt. Which UUIDs is adapter-specific. Layouts the client knows:

| Adapter family | Service | Write | Notify |
|---|---|---|---|
| Veepeak OBDCheck BLE, most clones | `FFF0` | `FFF2` | `FFF1` |
| HM-10 style | `FFE0` | `FFE1` | `FFE1` |
| Microchip RN4870 (Veepeak BLE+) | `49535343-FE7D-…` | `49535343-8841-…` | `49535343-1E4D-…` |
| Nordic UART | `6E400001-…` | `6E400002-…` | `6E400003-…` |
| OBDBLE | `E7810A71-…` | `BEF8D6C9-…` | `BEF8D6C9-…` |

On connect the client logs the **whole GATT table** (`I obd: service … /
chr … r= w= wnr= n=`), picks the first layout that matches, and otherwise
falls back to the first writable + first notifying characteristic it saw.
If the fallback picks wrong, add the right pair to `kLayouts` in
`src/obd/obd_client.cpp`.

## ELM327 session

Init after connect: `ATZ` (reset, 3 s), `ATE0` `ATL0` `ATS0` `ATH0` (echo,
linefeeds, spaces, headers off), `ATSP0` (auto protocol), `ATI` (version →
*Adapter* row), then `0100` / `0120` / `0140` / `0160` to learn the
supported-PID bitmaps, and `ATDP` for the protocol found. If `0100` gets no
data (`NO DATA`, `UNABLE TO CONNECT`, `SEARCHING...` timeout) the ECU is
asleep: ignition off. The poll loop keeps going and re-runs the init after
10 rounds without data, so turning the key later just works.

Every second, each supported PID from this list is read:

| PID | Value | Formula |
|---|---|---|
| 04 | engine load % | A·100/255 |
| 05 | coolant °C | A−40 |
| 0C | RPM | (256A+B)/4 |
| 0D | speed km/h | A |
| 0F | intake air °C | A−40 |
| 10 | MAF g/s | (256A+B)/100 |
| 11 | throttle % | A·100/255 |
| 2F | fuel level % | A·100/255 |
| 42 | control module voltage | (256A+B)/1000 |
| 46 | ambient °C | A−40 |
| 5C | oil °C | A−40 |

plus `ATRV`, the adapter's own reading of the OBD port voltage — the one
value that works with the engine off, i.e. the starter battery voltage.

Replies are parsed by scanning each line for `41 <pid>` and taking the
bytes after it, so multi-ECU answers (several lines) and adapters that
ignore `ATS0` are both fine.

## Using it

Round board: **OBD-II** page (page 5, before Setup).

1. Switch **OBD adapter** on. The continuous scan now also collects
   anything that looks like an OBD dongle (name contains *OBD*, *Veepeak*,
   *VLink*, *ELM*, … or advertises one of the services above).
2. Tap the adapter in the list. Its address is saved and the display
   connects (and reconnects after every drop, 5 s backoff).
3. The page turns into a value grid: adapter version, ECU/protocol, battery
   voltage, module voltage, RPM, speed, temperatures, fuel, load, throttle.
   Values grey out after 10 s without an update.
4. **Forget adapter** returns to the picker.

1.9 / any board: `obd on`, `obd list`, `obd connect <n>`.

Standby (phone presence) applies: with the gate closed the OBD link is
dropped like the BatMon link and comes back when a phone is present.

## The diagnostics log (no PC needed)

Everything worth knowing about a new adapter is written to a small text
log (`src/diag.*`, 16 KB rolling, saved to LittleFS `/diag.txt` so it
survives reboots): the advertised service UUIDs of the adapter, the full
GATT table with properties, the layout chosen, MTU, every command and
reply of the ELM327 init sequence, the supported-PID list, every `obd
send` result, disconnect reasons. Three ways to get at it:

1. **On screen** — OBD-II page → **Log**. Scrollable; the overlay refreshes
   as lines arrive. **Clear** empties it.
2. **Over BLE from a phone** — in the Log overlay press **Share BLE** (or
   Phones → Pair phone, or serial `phone pair`): the display advertises as
   *BatMon Display* for 2 minutes. In nRF Connect / LightBlue connect to it
   and open characteristic `b47a0003-9f21-4d9e-a1c3-5ab9d2f0c001`:
   * **enable notifications** — the whole log is streamed in ~180-byte
     chunks, ending with `--end--`; nRF Connect's log view can be exported
     or copied, or
   * **read** it repeatedly — each read returns the next 500-byte page,
     an empty read means the end (and rewinds).
   No pairing is needed for this characteristic. A phone that is already
   bonded may re-encrypt the link on connect; the display now keeps such a
   link up instead of dropping it 1.5 s after authentication.
3. **Serial** — `obd diag` prints it, `obd diag clear` empties it.

## Diagnosis from the serial console

| Command | Effect |
|---|---|
| `obd status` | link state, adapter, ELM version, protocol, all values, last raw reply |
| `obd list` | adapters seen by the scan |
| `obd connect <n>` | use candidate n (saved) |
| `obd disconnect` / `obd forget` | drop the link / drop the saved adapter |
| `obd on` / `obd off` | enable / disable |
| `obd send <cmd>` | raw ELM327 command; reply printed (`obd send ATI`, `obd send 0100`, `obd send 03` for DTCs) |
| `obd pids` | decoded supported-PID list |
| `obd log 1` | echo every command and every notification chunk |
| `obd diag [clear]` | print / clear the saved diagnostics log |

Suggested first session with a new adapter, engine running:

```
obd on
obd list                 -> 0: OBDBLE  aa:bb:...  -60 dBm
obd connect 0
                          (watch the GATT table in the log)
obd status               -> elm 'ELM327 v2.1'  ecu responding  protocol 'ISO 15765-4 (CAN 11/500)'
obd pids                 -> 0C 0D 05 0F 10 11 1C 21 2F 42 46 ...
obd send 010C            -> 410C0B54   (RPM = 0x0B54/4 = 725)
obd log 1                -> watch the poll loop
```

Things that can go wrong, and where to look:

* **No candidates:** the dongle only advertises when plugged into a live
  OBD port (some only with ignition on). Check the name it advertises with
  a phone app (nRF Connect) and add it to `looksLikeObd()` if it is exotic.
* **Connects, then "no usable write/notify characteristics":** paste the
  GATT table from the log; add its layout to `kLayouts`.
* **Timeouts after every command:** the notify characteristic is wrong
  (nothing ever arrives) or the adapter needs write-with-response — the
  client already uses write-without-response only when the characteristic
  lacks WRITE.
* **`ATI` answers but `0100` is `NO DATA`:** ignition off, or the protocol
  search failed. Try `obd send ATSP6` (CAN 11 bit / 500 k, most cars since
  2008) then `obd send 0100`.
* **BatMon link suffers:** two connections share one radio. Lower the OBD
  poll rate (`OBD_POLL_MS`) first.

## Radio arbitration

The controller cannot start a connection while scanning, and both centrals
live in their own task. `batmon::Client::radioAcquire()` takes a recursive
mutex and stops the scan; `radioRelease()` restarts it if presence or OBD
discovery still need it and gives the mutex back. Both `connectTo()`s wrap
their connect in it; `startScan()` only *tries* the mutex so a poll loop
never blocks behind the other side's connect attempt.

## Files

| File | Role |
|---|---|
| `src/obd/obd_client.{h,cpp}` | task, candidate list, layout table, ELM327 transactions, PID decoding |
| `src/diag.{h,cpp}` | rolling diagnostics log in LittleFS; BLE characteristic `b47a0003` (read pages / notify stream) |
| `src/batmon/batmon_client.*` | `radioAcquire/Release`, `scanWanted()`, feeds `obd::onAdvert()` |
| `src/ui/ui.cpp`, `ui_layout_*.cpp` | OBD page |
| `src/console.cpp` | `obd …` commands |
| `src/settings.*` | `obdEnabled`, `obdAddr`, `obdAddrType` |
| `src/config.h` | `OBD_POLL_MS`, `OBD_REPLY_TIMEOUT_MS`, `OBD_CONNECT_TIMEOUT_MS`, `OBD_RECONNECT_BACKOFF_MS` |

## Roadmap

- Show something from the OBD on the Halo page (starter battery voltage is
  the obvious one for a van: aux from the BatMon, starter from the OBD).
- Log OBD values into the history buffers / chart.
- DTC read (`03`) and clear (`04`) from the page.
- Vehicle-specific PIDs (mode 22) once the generic ones are proven.
