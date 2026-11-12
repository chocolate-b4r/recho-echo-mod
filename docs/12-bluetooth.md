# 12 · Bluetooth (in progress)

RECHO has no Bluetooth code. This mod adds it in steps, each one tested on a real player before
the next.

| Step | What | Status |
|---|---|---|
| 1 | Diagnostic: power the module, open its serial line, log what it says | **works on both**: Mini `CV13`, Echo `CV15`, both answer `CQIOK` |
| 2 | Diagnostic: scan, connect to the first device found, disconnect | **scan works on both**: the module answers `CKKK`, ends its search with `CSEN` after ~26 s; no headphones were in pairing mode |
| 2b | Settings → Bluetooth: on/off, search, connect, disconnect (`patches/bt.c`) | built (Mini v14, Echo stage 13); replaces the diagnostic |
| 3 | Route audio to the module and back to the jacks | **Echo: built (stage 21), first device test pending**; Mini: pins unknown |

## Main menu → Bluetooth (step 2b, `patches/bt.c`)

Since Mini v18 / Echo stage 18 the Bluetooth screen opens from the main menu (the Supporters row's
slot: label → `str_bluetooth`, its select case at m47 `0x307A1E4` → `bl bt_request_open`), and Back
returns to the main menu. The Settings row described below was removed.

### Earlier: Settings → Bluetooth

Settings' hidden **Debug strip** row became **Bluetooth** (second row, value On / Off). Selecting it
opens the Bluetooth screen:

| Row | Play selects |
|---|---|
| Bluetooth On / Off | powers the module (GPIO2.7) and opens UART1; the header shows starting / on / no reply |
| Search for headphones | `CS00`, 30 s at most (the module ends with `CSEN`); becomes **Stop searching** (`CP00`) |
| one row per device found | `CL06` + address, `CH06` 4 s later if unanswered; 30 s limit, then `CT00` and "failed" |
| Disconnect | shown in row 2 while connected (`CT00`) |

Vol keys move, ↩ or ≡ go back to Settings, hold ≡ goes to the main menu. The module is off at
every start-up and is switched off before the charging screen. With Log to file on, power-on,
devices found, connects and every non-status frame are logged as `bt: ...`.

How it hooks in (m48 and fw1 addresses):

| Site | Change |
|---|---|
| m48 label table, new row 1 | `0x0307AE94` "Debug strip" → `str_bluetooth` |
| m48 `0x307A804` (Debug strip select) | `bl bt_request_open; b.n 0x307A75A` (byte 4 no longer toggled) |
| m48 `0x307A3FE` (Debug strip value) | `bl bt_value_r1` (r1 = module on, other registers kept) |
| POWER tick | `bt_tick`: opens the screen like the cable chooser (`0x305BA54` close module, `0x305C3E4` switch screen), polls the UART, retries and time-outs |

The screen is a RECHO screen descriptor in the cave (`bt_screen`), laid out like USBSEL's: +24
enter, +28 key (`0x3059FE8` moves the cursor), +32 tick (redraw), +36 draw. Back asks for
module 48 with `0x305B234`.

## Results so far

- 2026-11-12, step 1: both modules start and answer every status query (`CQIOK` = idle, not connected).
- 2026-11-12, step 2 (30 s): both modules end the search themselves with `CSEN` after ~26 s, 0 devices.
- 2026-11-12, step 2: both answer `CS00` with `CKKK`, the same acknowledgement Podium's tests expect
  during a scan, and `CP00` with `CKKK` + `CP`. No `CS nn` device frame arrived in 15 s. The scan now
  lasts 30 s, as long as Podium's, and the headphones must be in pairing mode (not just switched on).

## Step 1: the diagnostic (`patches/bt_diag.c`)

**To run it:**

1. Turn Settings → Log to file on.
2. Restart the player and leave it on for 40 s.
3. Copy `ECHOLOG.TXT` from the internal drive.

The diagnostic only runs while logging is on. It starts 10 s after boot, keeps the module powered
for 25 s, then switches it off again. It doesn't touch audio and writes nothing except log lines.

**Lines to look for:**

```
bt diag: power on GPIO2.7, UART1 115200 on GPIO2.16/2.17, listening 25 s
bt diag: rx "CV15" [43 56 31 35 ]          <- the module is alive and says its version
bt diag: rx "CQ.." [...]                    <- answers to the status queries
bt diag: done, N bytes from the module, M status queries, version char 53 (0 = none)
```

- If `N` is 0 on a player, the module there either isn't powered from GPIO2.7 or doesn't talk
  on UART1 group A.
- The pins are Podium's, verified only on the full-size Echo. The Echo Mini may use different
  ones; its log decides how step 2 is wired.

## Hardware facts used

| | |
|---|---|
| Module power | GPIO2.7, high = on (RECHO's `gpio_set` `0x3051590`) |
| Serial | UART1 `0x400B0000`, function 1 on GPIO2.16 / GPIO2.17 (`0x3051570`), group A via GRF `+0xBC` = `0x02000000` |
| Clock | CRU `+0x38`: `0x00C00040` (crystal) then `0x003F0000` (÷1); divisor 13 → 115,200 8N1 |
| Reset / gates | CRU `+0xC0` bit 8 (reset), `+0x84` bit 13 (UART1 clock), `+0xA0` bit 9 (bus clock); write-masked |
| Frames | ASCII `C` + letter + 2 characters, NUL-terminated. Module: `CV1x` when ready. Player: `CQ00` status query |
| Log on/off | settings block `0x0303EDD0` byte 5 |

## Step 3: audio to the headphones (full-size Echo, `bt_route` / `follow_player` in `patches/bt.c`)

Podium's rules, unchanged: stream while connected, playing and no jack in use (jack readings are
ignored while routed and for 500 ms after a change, because switching the pins fakes them).

- On connect: `CY0` 0x00 once.
- Stream on: `CY0` 0x1E (after a `C` 0x86 play notice if the headphones last reported paused).
  Stream off: `CY0` 0x00 (after a `C` 0x87 pause notice if they last reported playing).
- Routing on: DAC mute GPIO2.1 high, wired output GPIO2.14 low; the first time also stock's
  start-up settings (GRF `+0xC4` = `0x00010000`, CRU `+0x84` = `0x00920092`, `+0xA0` = `0x00020002`,
  `+0xC0` = `0x00680068`, jack pulls GRF `+0x14`/`+0x54`), logged with the old values. RECHO's own
  writes to the two pins (through `gpio_set_echo`) are remembered and held back while routed, and
  restored afterwards (also before the charging screen).
- Headphone buttons: `CA`/`CB` skip back / forward (`0x305AB80`), `CP` pauses RECHO when we are
  streaming, `CC` resumes it after such a pause (`0x3057C94`), ignoring echoes for 3 s.
- Volume: RECHO sets the volume in the DAC chip, after the point the module listens on, so the
  module plays at its own fixed level (0x1E).

### Device results so far (Echo)

- Stage 21/22 (2026-11-13): connect, `CY0 1E` (answered `CY`), routing and the earbud buttons work;
  no sound. Before routing RECHO already had every stock setting Podium lists (GRF `+0xC4` bit 0
  clear, CRU `+0x84` `0xCD92`, `+0xA0` `0x7CF2`, `+0xC0` `0x026F`), GPIO2.9 low, GPIO2.0 high,
  I2S1 `TXCR` `0x401F` and `CKR` `0x00033F3F` (identical to Podium at 44.1 kHz), I2S1 pins on group A.
  GPIO2.1 reads **high while the jacks play**, so it is not an active-high mute here.
- Silent: GPIO2.1 high with GPIO2.14 low or untouched, each with GPIO2.9 low or not.
- Stage 23 tries GPIO2.1 low (with GPIO2.14 low / untouched) and GPIO2.2 (DAC B enable) low.
