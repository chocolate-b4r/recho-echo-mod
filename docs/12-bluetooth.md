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
- Stage 23 (round 2): GPIO2.1 low with GPIO2.14 low / untouched, GPIO2.2 (DAC B enable) low, and all
  three low: all silent. Pin routing is ruled out; next step is FiiO's stock Echo firmware itself.

### FiiO's stock Echo 2.0.0 (`ECHOV200.IMG`), decompiled

Same RKnano container. The load row at `0x200` gives the main code: loaded from `0x030795CC`
(file offset = address - `0x0304FE00` = `0x297CC`), run at `0x03050000`, `0x2B8F0` bytes.

- `0x30672AE(letter, arg)` builds `C` letter `0` arg (`CY0` + level); `0x30672CA(notice)` the
  `C 86/87 00` notices; `0x3067304` is the Bluetooth loop (`CL` + address to connect, `CQ` status);
  `0x30512A0` sends the buffer (`0x300555C`, length at `0x3004100`) through the UART1 driver.
  The only commands are `CL`, `CQ`, `CY0` and the notices, as in Podium and here.
- On connect (`0x3067726`): GPIO2.1 **high**, GPIO2.14 **low** (`0x30527D4` mux, `0x3056388`
  direction, `0x30563C2` level, all bank 2). On disconnect (`0x306778A`): GPIO2.1 low, GPIO2.14 high.
  Then `CY0 1E` when playing / `CY0 00` when not (flags `0x1D4` / `0x1D3`).
- The codec clock set-up (`0x3051646`, ACODEC `+0x200`..`+0x214`) is the same as RECHO's (`0x3055A0C`).

So FiiO's Bluetooth steps are what stage 21 already does. Stage 25 logs the module's raw reply to
every `CY0` (the parser used to keep only `CY`), to see whether it answers with an error.

### Stage 26 findings (2026-11-13)

- The module answers every `CY0` with `CKKK` `CYOK`: the stream command is accepted.
- No sound at any volume. (An earlier version of this page concluded from that that the module
  does not take the DAC's analogue output. That was wrong: EchoPod's research notes say it does,
  and samples and encodes that signal itself; see "What Podium and EchoPod do" below.)
- The headphones (Nothing Ear (2)) streamed fine with FiiO's own firmware, so the hardware path works.
- GRF / CRU / GPIO snapshot while routed: `logs/echo-bt-dump.txt` (I2S1 pins on function 1, UART1 on
  GPIO2.16/17, audio clock `CRU+0x20` = `0x8004` as Podium, GPIO2 data `0x80C7` / dir `0x3C2D7`).
- FiiO's module reset (overlay at `0x307E8C0`, file `0x...`): power off, 200 ms, UART set-up, 100 ms,
  then `0x307E986`: GPIO2.7 high and wait up to 5 s for `CV1`. Nothing beyond what we do.

### Other projects checked (2026-11-13)

- ReChord (`docs/re/decomp/apps/bluetooth/`): FiiO's Echo Mini Bluetooth screens (device list
  drawing, status), nothing on the audio path.
- EchoPod (Echo Mini, Podium's predecessor): routes with GPIO2.0 (mute) / GPIO2.14 and lists "no
  Bluetooth audio" as a known issue.
- Podium (Echo): the only one with Bluetooth sound; every register and pin we can compare matches.
- FiiO 2.0.0: `GRF+0xBC` bits 3:2 (`0x30528EE`) select UART pins, not audio.
- FiiO's audio start (player overlay `0x308D8xx`, file entry `0x628`): GPIO2.14 and GPIO2.1 high,
  GPIO1.4 low, GPIO2.2 high, I2S1 opened at 48 kHz, 24-bit (`0x30560A8(1, 2, 1, 48000, ..., 23)`), then
  per track at the track's rate. Same fields as RECHO's `TXCR` `0x401F`.
- FiiO's player overlay (entry `0x568`) reads the Bluetooth-connected flag (`+0x344`) on pause / play
  (sends the `C 87` / `C 86` notices there) and keeps a **separate Bluetooth volume** (`+21`, wired
  `+19`). Not yet traced: what that volume drives (the `CY0` level, or a digital gain). Next lead.
- FiiO's Bluetooth volume (`0x0302234C` + 21) only feeds the volume bar drawing (`0x30831EC`), nothing
  in the audio path.
- Stage 29 (last attempt): routes for the whole connection from the moment it connects, as FiiO's
  connect step does, and only streams (`CY0 1E` / `00`) with play / pause.
- Stage 29 on the player: still silent with the route held for the whole connection.

## What Podium and EchoPod do (checked 2026-11-13)

Sources: Podium 4.6.0 and 4.6.1 (`board/bluetooth.c`, identical in both), EchoPod 0.12.0
(github.com/BasilSkyWalk/echopod: `board/bluetooth.c`, `docs/hardware.md`, `docs/research.md`,
`docs/architecture.md`). Both stream to OnePlus Buds 3 since 2026-10-29.

| Step | Podium (Echo) / EchoPod (Mini) | This mod |
|---|---|---|
| Module power, UART1 group A 115,200 8N1 | GPIO2.7, pins 2.16 / 2.17 | same |
| Commands | `CY0 00` on connect, `CY0 1E` / `CY0 00` to start / stop the stream, `C 86` / `C 87` | same |
| Routing | DAC mute high (Echo GPIO2.1, Mini GPIO2.0), GPIO2.14 low; back 1 s after the stream stops | same pins; back at once |
| Pins held while routed | every DAC pin write goes through `board_bluetooth_pin`, so the DAC code can't undo them | `gpio_set_echo` holds them (Echo) |
| Stock start-up writes | GRF `+0xC4`, CRU `+0x84` / `+0xA0` / `+0xC0`, GRF `+0x14` / `+0x54`, **once**, and nothing else in their firmware touches those registers | the same six writes, **once**; RECHO keeps running its own audio code afterwards |
| Audio | the module takes the **DAC's analogue output**; a separate DAC volume table for Bluetooth (6.5 dB below the 3.5 mm one, or equal on "Louder") | RECHO's volume table |

The difference that matters: in Podium nothing undoes the start-up writes, in RECHO something
might. The dump above shows GRF `+0xC4` reading `f040` on the first route and `3040` on later
ones, so RECHO does rewrite at least that register between routes. Their notes also say it is not
known which of the six writes is needed.

**Test build (Echo stage 45):** the six writes are applied on every route (not once), checked
once a second while routed, and applied again whenever one has changed. The log (Settings → Log
to file) shows:

```
bt: stock before route: GRF+C4 … CRU+84 … +A0 … +C0 … GRF+14 … +54 … wrong 00 fixes 0
bt: stock after 1 s: …
bt: stock changed back: … wrong 01 fixes 0      <- bit i = which of the six RECHO undid
bt: stock after 5 s: …
```

`wrong` is a bit per register in the order GRF `+0xC4`, CRU `+0x84`, `+0xA0`, `+0xC0`, GRF
`+0x14`, `+0x54`. If it plays now, narrow down which register matters; if it stays silent with
`wrong 00` throughout, the settings are not the cause.

### Result (2026-11-13): the settings are not the cause

Echo stage 45, earbuds "Ear (2)" connected, music playing with nothing in the jacks: still silent.

```
12.483 bt: stock before route: GRF+C4 00003040 CRU+84 0000cd92 +A0 00007cf2 +C0 0000026f GRF+14 00000028 +54 00000001 wrong 00 fixes 0
13.483 bt: stock after 1 s:   (same)                                                                                   wrong 00 fixes 0
17.483 bt: stock after 5 s:   (same)                                                                                   wrong 00 fixes 0
```

All six registers already hold stock's bits **before** the mod writes them, and RECHO never changes
them while routed. (The `f040` → `3040` change seen earlier is in GRF `+0xC4` bits 14–15, which none of
the six writes cover.) The routing pins read as Podium sets them: GPIO2.1 = 1, GPIO2.14 = 0.

Open doubt: Podium's Bluetooth notes (`docs/hardware.md`, known issues, release notes) are copied
word for word from EchoPod, which runs on the **Echo Mini** and names Mini pins (GPIO2.0 / 2.14).
Podium maps them to the Echo by analogy (mute GPIO2.1), and nothing in its notes shows Bluetooth audio
tested on a full-size Echo. A decisive test: install Podium 4.6.1 on the Echo and try the same
earbuds. If it plays, the hardware path works and its register state can be compared with RECHO's;
if it is silent too, the Echo's routing is not what either project assumes.

The Echo Mini build has no routing at all yet (streaming is hard-wired off), so it cannot play over
Bluetooth whatever the settings.

## Podium plays on this Echo: register comparison (2026-11-13)

Podium 4.6.1 streams to the same earbuds on the same Echo, so the hardware path works. A Podium
build with one addition (`dac_path_write_dump`, 5 s into the first stream; decoded by
`tools/re/re_btdump.py`) recorded its state: [research/bt-dump-podium-streaming.txt](research/bt-dump-podium-streaming.txt).
RECHO's state while routed (silent) is in [research/bt-dump-recho-routed.txt](research/bt-dump-recho-routed.txt).

Same in both: I2S1 (`TXCR 401f`, `CKR 33f3f`), the six stock settings, GPIO2.1 / GPIO2.14 routing,
GRF `+0xC4`. Different (Podium / RECHO):

| Group | Registers | Podium | RECHO |
|---|---|---|---|
| clock gates | CRU `+0x80`–`+0xA0` | only 0x11, 0x14, 0x17, 0x81 gated | ~30 more gated |
| soft resets | CRU `+0xC0`–`+0xCC` | `0068 0 0 0` | `026f 3230 00f0 0010` |
| pins | GPIO0.12 / .15 / .24 / .25, GPIO2.9 | inputs | outputs (RECHO's Mini pin map) |
| pin functions | GRF `+0x20 +0x24 +0x44 +0x80 +0x88 +0x8C +0xB4 +0xBC` | | differ |
| clocks | CRU `+0x004` (PLL), `+0x014`, `+0x030` | | differ (not touched: changing the PLL can stop the player) |

The DAC chips' registers were read on Podium only (both chips: `080000` = `10`, PCM volume
`090001/2` = `1c`, power `020000` = `a6`); RECHO's side still needs its I2C routine found.

**Step test (Echo stage 46):** while streaming, every 15 s the next group is set to Podium's value and
logged (`bt: step N applied`): 1 clock gates, 2 soft resets, 3 the five pins to inputs, 4 GRF pin
functions. The end of the stream restores RECHO's values (`bt: step 0 undone`). Pressing pause on
the earbuds the moment sound starts puts a timestamp in the log (`rx P.`) next to the step that did it.
