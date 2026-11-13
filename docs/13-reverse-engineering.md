# 13 · Reverse engineering: method and findings

How RECHO 0.10.1 was read to build this mod, so the work can be repeated and checked. RECHO's
source is not public; everything here comes from its binary (for interoperability), the players'
own log files, and the published sources listed in [NOTICE.md](../NOTICE.md). The addresses
themselves are collected in [02-recho-map.md](02-recho-map.md).

> [!NOTE]
> The scripts below write RECHO's code and glyphs into `build/re/`. That folder is git-ignored on
> purpose: run them on your own copy of RECHO, don't commit what they produce.

## Toolkit

| Script | What it does |
|---|---|
| `tools/re/re_dump.py IMAGE` | slices fw1 and every SFMD module out of the image and disassembles each at the address it runs at (`fw1.od`, `m44.od`, …) |
| `tools/re/re_callers.py IMAGE ADDR…` | every `bl` / `b.w` to a routine, and every stored pointer to it (screen descriptors, callbacks) |
| `tools/re/re_glyphs.py IMAGE` | where each icon glyph `U+E000`–`U+E02B` is used |
| `tools/re/re_fonts.py IMAGE` | renders text in RECHO's two bitmap fonts |
| `tools/icons.py export IMAGE DIR` | the icon pack as PNGs |
| `tools/verify_image.py` | decodes every literal load in fw1 and the modules (free RAM, code cave, allow-lists) |
| `ECHOLOG.TXT` on the player | RECHO's own log (Settings → Log to file): keys, module loads, timings, USB, updates |

Disassembly is GNU `objdump -D -b binary -marm -Mforce-thumb --adjust-vma=…`. fw1 runs at
image offset + `0x0304F298`; every module runs at `0x0307A000`, header included, one at a time.

## The loop that worked

1. **Ask the log first.** RECHO logs a lot (`ui: key play ev 3`, `mod: m44 loaded`,
   `usb: exit key pressed`, `lcd: … name drawn and pushed`). The order of lines answered more
   questions than the disassembly did.
2. **Find the string or constant**, then the code that loads it (literal pools hold addresses:
   search for the 4 bytes), then its callers with `re_callers.py`.
3. **Read just enough** of the routine to know its arguments and side effects. Prefer replacing
   one pointer or one `bl` over editing a routine's body.
4. **Patch with `put(old → new)`**, so the patcher refuses an image whose bytes differ, and add a
   verifier check for the exact bytes.
5. **Confirm on the device** with the log (the line that should appear, or stop appearing).

## Findings, with evidence

### Boot screen and fonts

| Question | Evidence | Result |
|---|---|---|
| Why did the boot text always look bold? | Log: `0.174 lcd: … name drawn and pushed`, then `0.201 font: bitmap fonts from selected slot` | RECHO draws the splash **before** it loads its fonts. `draw_text` (`0x3058D84`) checks font+12 and falls back to the 6 × 11 boot font (`0x306C290`) |
| Which font does *Power off* use? | power-off routine `0x305A618`: `movs r0,#76; ldr r1,="Power off"; bl 0x30599EC`; that helper calls `text_width(1, …)` and `draw_text(1, …)` | the large font (font 1), colour `0xF79E` |
| Font format | image parts 0 and 1: 2,104,326 and 2,169,862 bytes = 65,536 cells | small: 32 B per glyph (12 rows × 16 bit, byte 24 = `0x80` \| advance); large: 33 B (16 rows, byte 32). Both 1-pixel strokes |
| Fix | | the hooks carry the 32 needed glyphs, extracted at build time (`tools/make_boot_font.py`) |

### USB disk

| Question | Evidence | Result |
|---|---|---|
| How is USB mode left? | string `eject on the computer, or press power` → USB screen draw `0x3050260`; descriptor at `0x305F35C` has no key handler | the mass-storage loop `0x3054FB8` polls a callback, `0x30505EC`, which arms on power release and leaves on `power_key_down()` at `0x305062C` |
| Exit reasons | table at `0x3051384`: ejected, cable removed, host suspended, charger, error, key pressed | logged as `usb: exit <reason>` |
| Why did the screen turn off after leaving? | the loop leaves on the press; the same press then reaches the UI's power handling | `usb_key_exit` waits for the release before returning |
| Reading ↩ inside the loop | RECHO's key poll (`0x30583A8`) does not run there; it samples ADC channel 1 on 3 of 4 passes; its ↩ band is `450 ≤ v ≤ 549` (`0x305850E`) | the hook samples channel 1 itself, conversion started one call and read the next |
| Where does RECHO go afterwards? | log: `usb: exit …`, then `mod: m44 loaded`, `ui: listed dir 0`; `re_callers` → `0x305BC6C` is called only from the USB path (`0x305123E`) and stores `movs r2,#44` to `0x030399CC` | one byte: 44 (Files) → 47 (main menu) |

### Main menu and Files

| Question | Evidence | Result |
|---|---|---|
| m47's descriptor | `"MENU"` at `0x307AA88`: +28 key `0x307A109`, +32 tick, +36 draw `0x307A2B9` | draw and key pointers replaced (`menu_draw`, `menu_key`) |
| How does Files "play all"? | m44 key handler: hold ▶‖ at the top level → `0x305A02C(drive)`, then `0x307A278(0, "Music"/"SD card")`; the routine ends with `set_module(45)` | the menu opens Files, waits until m44 is loaded, checks `0x307A278`'s first 16 bytes, then calls the same pair |
| Can the menu call m44 directly? | `set_module` (`0x305B234`) only stores the request at `0x030399CC`; modules load later in the main loop, all at `0x0307A000` | no: a tick waits for `mod: m44 loaded` (module id at `0x307A004`) |
| Drives | m44 strings at `0x307B4DE` `"SD card"`, `0x307B4E6` `"Music"`; drive 1 = SD | the menu plays drive 0 (internal) |

### Display fallback (Echo)

| Question | Evidence | Result |
|---|---|---|
| Why did Up Next and the strip vanish after quick power-offs? | `0x306C018` writes `"VOP1"` to `0x0309FFB0` and clears it after 5 s on VOP; a start that finds `VOP1` writes `"VOPF"` and stays on the slow GPIO path while RAM is powered | `about_tick` clears `VOPF` after 5 s; the top bar says *Safe display: restart* while on the fallback |

### Queue, play order, names

| Question | Evidence | Result |
|---|---|---|
| Next song in shuffle | `0x3065FE4(q, cur, mode, 0)` is pure computation (lowbias32 + 6-round Feistel, cycle walking) | walking it gives RECHO's real play order: Up Next and `9/10` |
| Song names without tags | `0x3064B64(store, entry, buf, &vol, 0)`: one 36-byte index read, 276 B copied | cached one per main-loop pass (Echo sidebar, Mini Up Next) |
| Safe to read storage? | before USB mode `0x305BC30` clears queue-active and closes the store | the readers check queue-active first |

### Icons

`re_glyphs.py` on the patched image, then each hit read in context: a glyph whose only uses are
in screens the mod replaces or removes is shrunk to 1 × 1 (table in [05-icons.md](05-icons.md)).
Example: `U+E011` (cassette) is still used by the cable chooser (`0x3059CE4` loads its string), so
it stays.

### Echo hardware

The full-size Echo's panel, pins and recovery were learnt from Podium's source and the
`intelarc/echo-mini-fw` toolkit; the notes are in [research/echo-port-notes.md](research/echo-port-notes.md)
and the result in [04-echo-port.md](04-echo-port.md).

### Bluetooth (unfinished)

What was tried, logged and ruled out is in [12-bluetooth.md](12-bluetooth.md): pairing works, the
module answers (`CV13` / `CV15`, `CKKK`, `CYOK`), routing on connect matches Podium and FiiO, and
there is still no audio.

## Replicating a build

```bash
RECHO_IMG="RECHO-0.10.1.IMG" tools/build_hooks.sh          # boot glyphs from your image, Mini hooks
RECHO_IMG="RECHO-0.10.1.IMG" tools/build_hooks.sh echo     # Echo hooks
python tools/patch_recho.py RECHO-0.10.1.IMG out/HIFIEC90.IMG --icons icons/mod
python tools/patch_recho.py RECHO-0.10.1.IMG out/RECHOECH.IMG --icons icons/mod --echo
python tools/verify_image.py RECHO-0.10.1.IMG out/HIFIEC90.IMG
python tools/verify_image.py RECHO-0.10.1.IMG out/RECHOECH.IMG --echo
```

`icons/mod/` holds the 15 glyphs this mod redraws (battery, play / pause and others); every other
glyph stays RECHO's. With the same toolchain (Arm GNU Toolchain 12.2 mpacbti-rel1) the output is
byte-identical to the builds listed in the [CHANGELOG](CHANGELOG.md).
