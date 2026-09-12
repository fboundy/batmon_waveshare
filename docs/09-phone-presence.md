# Phone presence (branch `feature/phone-presence`)

Make the display follow its owner: pair one or more phones once, and from
then on the screen and the BatMon link are only active while one of them is
nearby. Optionally the BatMon relay follows the phone too.

> **Status:** verified on the 2.1 with an iPhone: pairing via LightBlue,
> presence from advertisements, standby/wake, delete, rename.

## How it works

1. **Pairing.** The display advertises as `BatMon Display` with a tiny GATT
   service (`b47a0001-…`) whose one characteristic requires an encrypted
   link. Reading it from a generic BLE utility app (nRF Connect, LightBlue)
   makes iOS/Android show their standard *Pair* prompt. Just-works pairing
   with bonding and LE Secure Connections. On completion NimBLE stores the
   bond; the firmware reads the phone's **Identity Resolving Key (IRK)** from
   the bond store, gives the phone a name (`Phone n`, renamable on the
   serial console) and drops the link.
2. **Presence.** Phones advertise continuously but with a *resolvable
   private address* that changes every ~15 min. The display's scan runs all
   the time now (it also feeds BatMon discovery, so the client no longer
   pauses polling to scan) and every advertisement is checked against each
   paired IRK: `hash = AES-128(IRK, prand)` — the same `ah()` function and
   byte order as NimBLE's `ble_hs_resolv_rpa()`. A match refreshes that
   phone's `lastSeen`; a phone is *present* for `PRESENCE_TIMEOUT_MS` (90 s)
   after its last advert. If the controller has already resolved the
   address (identity address type), it is matched directly.
3. **Gate.** `presence::gateOpen()` is true when: no phones are paired, or a
   phone is present, or for `BOOT_GRACE_MS` (60 s) after boot, or for
   `LEAVE_HOLD_MS` (5 s) after the last phone left. When the gate closes the
   BLE client releases the BatMon (`LinkState::Standby`) and the main loop
   turns the backlight off. Any touch or BOOT press wakes the screen for
   `WAKE_MS` (30 s) without opening the gate.
4. **Debounce.** A phone that has been away must be seen twice within
   `PRESENCE_CONFIRM_MS` (30 s) before it counts as back; idle iPhones send
   the odd advert even with Bluetooth toggled off in Control Centre, and one
   packet must not wake the display. (A proper Bluetooth-off is Settings ->
   Bluetooth, or Airplane mode.) The "away after" timeout is set on the
   Phones page / `phone timeout <s>`.
5. **Relay lock.** While phones are paired and none is present (screen
   woken by touch/BOOT in standby) the relay toggles are greyed out and
   ignored, so nobody without a paired phone can switch the relay.
6. **Relay follows phone** (Phones page switch / `relayphone 1`). On the first
   phone arriving the relay-on command is queued and sent as soon as the
   BatMon is connected; on the last phone leaving, relay-off is sent
   immediately, inside the 5 s hold before the link is dropped.

## Pairing a phone

Round board: **Phones** page → *Pair new phone* (2 min window, countdown on
screen). 1.9 / any board: serial `phone pair`. Then on the phone:

1. Install **nRF Connect** (Nordic) or **LightBlue**.
2. Scan, connect to **BatMon Display**.
3. Open the unknown service `b47a0001-9f21-…` and **read** its characteristic.
4. Accept the phone's *Bluetooth Pairing Request*. The display logs
   `bonded with <address>` and the Phones page lists it as present.
5. Disconnect / close the app. Presence now comes from advertisements.

The phone keeps advertising while locked and in a pocket. If you *Forget*
the display in the phone's Bluetooth settings, the phone gets a new IRK and
must be paired again (`phone forget <n>` first).

Up to 8 phones (`CONFIG_BT_NIMBLE_MAX_BONDS`). Names are kept in NVS
namespace `phones`, keyed by identity address; the bonds themselves are
NimBLE's.

## Serial console additions

| Command | Effect |
|---|---|
| `phone list` | paired phones, present/away, last seen, RSSI |
| `phone pair` / `phone stop` | open / close the pairing window |
| `phone forget <n\|all>` | delete the bond(s) |
| `phone name <n> <name>` | rename |
| `relayphone 0\|1` | relay follows phone |

`status` also prints the gate state.

## Files

| File | Role |
|---|---|
| `src/presence.{h,cpp}` | pairing service, bond list, RPA resolution, gate, relay-follow |
| `src/batmon/batmon_client.*` | continuous callback scan (`onResult`) feeding both presence and BatMon discovery; `Standby` state |
| `src/main.cpp` | `presence::tick()`, backlight standby / wake |
| `src/ui/*` | Phones page (both layouts), *Relay w/ phone* switch on Setup |
| `platformio.ini` | peripheral role enabled, `MAX_CONNECTIONS=2`, `MAX_BONDS=8` |

## Risks / things to verify on hardware

1. **Does iOS pairing complete and does the bond carry an IRK?** Look for
   `bonded with …` and `irk=1` in `phone list`. If `irk=0` the phone did not
   distribute its IRK (it should with bonding + SC).
2. **Does the phone show as present after disconnecting?** Within a few
   seconds `phone list` should say PRESENT with an RSSI. If it never does,
   the RPA byte order in `rpaMatches()` is the first suspect — compare with
   `ble_hs_resolv_rpa()` in NimBLE's `ble_hs_resolv.c`.
3. **Scan alongside the BatMon connection.** The scan now stays running
   while connected (when phones are paired). Watch `pollErrors` on the
   Details page; if the link suffers, lower `setInterval/setWindow` duty in
   `Client::begin()`.
4. **BatMon discovery via the continuous scan** replaced the blocking
   `getResults()` — verify the initial connect still happens within ~10 s.
5. Standby recovery: with a phone paired and absent, the screen is off and
   the BatMon released; touch wakes the screen for 30 s (Phones page and
   the serial console still work), and the first 60 s after boot are
   always open.

## Roadmap

- Show presence on the Halo page (small phone glyph per phone).
- RSSI threshold per phone (only "present" when close).
- Auto-pause the BatMon link when a phone that runs the BatMon app is here.
