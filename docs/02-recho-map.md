# 02 · RECHO 0.10.1 map

Addresses are RAM addresses for build `2d11bdcd` (Thumb functions are called with bit 0 set). Each
entry was read from the disassembly and, where marked, confirmed on a device. Only what the patches
use or depend on is listed.

## Display

| Address | What |
|---|---|
| `0x03008A00` | frame buffer, 320 × 170 RGB565 (108,800 B). Every screen draws here |
| `0x03004114` | clip rectangle x0, y0, x1, y1 used by the drawing routines |
| `0x030041D8` | content rectangle of the current screen (x0, y0, x1, y1). y1 is 143 with a button bar, 169 without |
| `0x0303E730` | 1 while the current screen has a button bar ("HINTS") |
| `0x0306BF2C` | `fill_rect(x0, y0, x1, y1, colour)`, clipped |
| `0x03058D84` | `draw_text(font, x, y, utf8, maxw, fg, bg)`. bg −1 = transparent; font 0 small, font 1 large |
| `0x03058D20` | `text_width(font, utf8)` |
| `0x030599EC` | centred text helper: `(y, text)` in font 1, colour `0xF79E` (the *Power off* message) |
| `0x03034B48` | font table, 16 B per font: +8 height, +12 loaded flag |
| `0x03058CF4` | 1 when both fonts are loaded (font 0 +12 and font 1 +12) |
| `0x0306C290` | boot text `(x, y, s, colour, bg, scale)`: 6 × 11 built-in font; `draw_text` falls back to it while no font is loaded |
| `0x0306C3E4` | boot splash ("RECHO 0.10.1"), called at `0x3050D7C`, ~0.17 s after reset, before the fonts load (~0.20 s) |
| image parts 0 / 1 | small / large font: one cell per BMP code point, 32 B (12 rows) / 33 B (16 rows), 16-bit rows MSB left, then `0x80` \| advance |
| `0x0306C018` | VOP setup; marks `0x0309FFB0` `"VOP1"` and clears it after 5 s; a start that finds it writes `"VOPF"` (slow GPIO path) |
| `0x0306BC70` | `lcd_flush()` (sync push of dirty rows) |
| `0x0306BAE0` | sync push of rows y0..y1 |
| `0x0306B908` | DMA band push (≤ 16 rows) through the bounce buffer `0x030061A0` |
| `0x0306B534` | wait until no DMA band is in flight. **Call before touching the panel yourself** |
| `0x0306B1F0` | set panel window: adds the row offset (Mini 35) — the Echo patch changes it to 75 |
| `0x0306AF98` / `0x0306AF6C` | panel command send / bus mode (`0x800` command, 2 pixel) |
| `0x60070024` / `0x6007002C` | LCD controller status (bit 0 busy) / data |
| `0x030089EC` | 1 while the panel is on the fast VOP bus (0 early at boot) |
| `0x0306B000` | Mini panel init, called after sleep-out from `0x306B9F4` (boot) and `0x306C206` (wake) |
| `0x0306BDD4` | `backlight(level)`; `0x03006174` holds the PWM value, 0 = off |
| `0x030041D4` | brightness setting |

## Screens, services, keys

| Address | What |
|---|---|
| `0x0303E73C` | current screen descriptor. Descriptor +24 enter, +28 key handler, +32 tick, +36 draw, +40 leave, +44 soft-key labels |
| `0x0305C41C` | returns the current screen descriptor |
| `0x0305C51C` | `request_redraw(screen)` |
| `0x0305C3E4` | switch to a screen descriptor |
| `0x030399C8` / `0x030399CC` | id of the loaded module / module requested next |
| `0x0305B234` | `set_module(id)`: 44 Files, 45 Now Playing, 47 menu, 48 Settings |
| `0x0305BA3C` | go to the main menu (no-op if already there) |
| `0x0305BE7A`… | global key filter: power, volume, play, holds. `0x305BF18` is hold ≡ → main menu |
| `0x0305C3AC` | register a service; services are ticked from the main loop via `0x0305C7A4` |
| `0x0306AC48` | POWER service descriptor; its tick pointer is at `0x0306AC68` (original `0x0306A7C5`) |
| `0x03060B98` | HINTS service: rect 0,144..319,169, draw pointer at `0x03060BBC` (`0x0305AEED`) |
| `0x0305AEEC` | HINTS draw: fills rows 144..169, a rule, five label cells or a message |
| `0x0305AE28` | message to show in the bar instead of labels (or 0) |
| `0x0305A874` | show a bar message `(text)` for 2 s (text at `0x0303AB4C`, expiry at `0x0303AB48`) |
| `0x0307AA88` | m47 (main menu) descriptor `"MENU"`: key `0x307A109`, tick `0x307A211`, draw `0x307A2B9` |
| `0x0303BCC0` | main menu state: [0] row count (written by the draw), [1] cursor, [6] About open |
| m44 `0x307A278` | Files: queue a folder tree `(dir, name)`, `dir` 0 = the whole drive; starts playback, then `set_module(45)` |
| `0x0305A02C` | select drive `(n)`: 0 internal ("Music"), 1 SD card; 0 = failed |
| `0x0303AB2C` | m44: 1 while the selected drive is the SD card |

Key event = two bytes: key id and event type.

| Key id | Key | | Event | Meaning |
|---|---|---|---|---|
| 1 | Power | | 1 | press |
| 2 / 3 | Vol + / Vol − | | 2 | tap (release before hold) |
| 4 | ▶‖ | | 3 | hold |
| 5 | ≡ | | 4 | repeat |
| 6 | ↩ | | | |

Raw key ladder (ADC channel 1, value at `0x030041A8`): vol+ ≈ 124, vol− ≈ 1, ▶‖ ≈ 210–310,
≡ ≈ 337–393, ↩ ≈ 480–545. The same values were seen on both players. RECHO's poll (`0x30583A8`,
every 10 ms) samples channel 1 on three passes out of four and channel 0 / 2 / 3 from the table at
`0x3060180` on the fourth; it decodes ↩ as 450–549 (`0x305850E`).

## Player and queue

| Address | What |
|---|---|
| `0x03057BD0` | player state: 1 playing, 2 paused |
| `0x03057BE4` | elapsed ms |
| `0x03057BDC` | format record: [0] sample rate Hz, [2] bits, [3] kbps (lossy), [4..5] length in samples |
| `0x03057DB4` / `0x03057DC0` | ReplayGain mode (1–3 on) / gain in centi-dB |
| `0x030041D0` / `0x0303AB8C` | volume in dB (−128 = mute) / ms of the last volume change |
| `0x0305A194` | queue loaded |
| `0x0305A1B0` | current queue index (0-based) |
| `0x0305A1BC` | queue info: [1] count |
| `0x0305A8F8(index)` | tag record or 0 (starts an async read): +0 title, +160 artist, +256 album, +352 track no. |
| `0x0305A1CC` / `0x0305A1C4` | file name / folder name of the current track |
| `0x03059AE8(buf, ms)` | formats `m:ss` |
| `0x0303AB90` | queue object: [4] count, [8] shuffle seed, [24] shuffle pass, [28] skip predicate (NULL) |
| `0x03065FE4(q, cur, mode, 0)` | next entry; pure computation (lowbias32 + 6-round Feistel, cycle-walking) |
| `0x0303BD74` / `0x0303BD78` | queue active / current entry |
| `0x0303BD80` | queue store (`RQUEUE.M3U` + `RQUEUE.IDX`) |
| `0x03064B64(store, entry, buf, &vol, 0)` | file name of an entry: one 36-byte index read, copies 276 B, 0 = ok. On an I/O error it resets the store |
| `0x0303EDD0` | settings block; byte 7 = play mode (0 normal, 1 shuffle, 2 repeat one, 3 repeat all, 4 infinite shuffle) |
| `0x0305BC30` | before USB-disk mode: stops the player, clears queue-active, closes the store |
| `0x03054FB8` | USB-disk loop; exit reasons (table `0x3051384`): ejected, cable removed, host suspended, charger, error, key pressed |
| `0x030505EC` | the loop's callback: watchdog, arms on power release, leaves on `power_key_down()` at `0x305062C`, redraws every 500 ms |
| `0x0305BC6C` | after USB disk: restores state, `movs r2,#44` at `0x305BC8A` → module request (opens Files) |
| `0x0305F35C` | USB screen descriptor `"USB"`: draw `0x3050261`, no key handler |

## Power, battery, clock, storage

| Address | What |
|---|---|
| `0x0305A618` | power-off routine; ends in `bl 0x3051E28` (power_down) at `0x305A678` |
| `0x030401EC` | battery block: [0] mV, [1] per-mille, [2] level 0–4, [3] cable, [4] charger done, [5] low |
| `0x03059414` | status-bar draw (battery glyph level load at `0x305945E`) |
| `0x03051638` / `0x03051C48` | ms clock / delay (feeds the watchdog) |
| `0x03051610` / `0x03051590` | GPIO read / set |
| `0x400D0000` | SAR ADC: battery ch0, key ladder ch1 |
| `0x0305DA2C` / `0x0305DAC4` | read clock chip (16 regs, 0 = ok) / decode to y,m,d,h,min,s,weekday (returns 1 if unreliable) |
| `0x030399C0` | USB-disk request flag; the cable chooser state is at `0x030041B0` ([1] cursor) |
| `0x030399B8` / `0x030399BC` | chooser countdown active / start ms |
| `0x03054094` | update-file search: `ECHOMINI.IMG` (name at `0x305F3BF`) or `HIFIECnn.IMG` (prefix at `0x305F3CB`) |
| `0x030540C4` | slot writer: firmware area and slot 2 offset from the eMMC ID block (`0x3052640`) |
| `0x0305444C` | FW1-from-FW2 repair |

## Free RAM

`0x03043002`–`0x0304A240` (≈29 KB) is never referenced by fw1, the modules or `.data`; the verifier
decodes every PC-relative literal load to prove it. The Echo build uses 16,640 B at `0x03044000`
and 276 B at `0x03048200`. See [08-memory.md](08-memory.md).
