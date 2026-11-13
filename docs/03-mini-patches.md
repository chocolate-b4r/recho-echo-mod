# 03 · Patches (both players)

Every patch is applied by `tools/patch_recho.py` with `put(d, offset, old, new)`, which **refuses to
run if the original bytes differ**. The patcher only works on RECHO 0.10.1 `2d11bdcd`. Addresses
are RAM addresses; "m47 `0x307A19C`" means module m47's code at that run address.

## 1 · Main menu without Spectrum, Supporters, Debug (module m47)

Original rows: 0 Files, 1 Now playing, 2 Spectrum, 3 USB disk, 4 Settings, 5 Supporters, 6 Debug,
7 Power off, 8 About. `--drop-rows` (default `2,5,6`) chooses what goes.

| Site | Change |
|---|---|
| `0x307AA40`, `0x307AA64` | icon and label pointer tables rebuilt without the dropped rows |
| `0x307A1A4` | `tbb` dispatch table for the kept rows |
| `0x307A19C` | dispatch bound `cmp r3,#8` → rows − 1 |
| `0x307A67C` | row count `movs r3,#9` → rows |
| `0x307A04C`, `0x307A6DA` | USB-disk row checks (draw, key hint) follow the row |
| `0x307A0E6`, `0x307A6E4` | Power-off row checks follow the row |
| module CRC | resealed |

## 2 · Off-state charging screen (`patches/charge_screen.c`)

The power-off routine's last call, `bl 0x3051E28` (power_down) at **`0x305A678`**, becomes
`bl charge_screen`. With no cable it calls power_down at once. With a cable it shows a four-pill
gauge and a 7-segment percentage. It reads ADC channel 0 directly and draws rectangles only:
no text routine, no log, no storage, because v2 did and restarted the player. The backlight dims
after 8 s; a tap shows it again; a long press reboots; pulling the cable powers down. The Echo build
blanks the whole panel first and pushes the picture centred (`echo_flush_centred`).

## 3 · Charging animation (`patches/charge_anim.c`)

| Site | Change |
|---|---|
| `0x305945E` (6 B) | status bar's level load `ldr r2,[r3,#8]; ldr.w r8,[r3,#20]` → `bl status_level_shim; nop`. Returns `(ms/500) % 5` while the cable is in and the charger is not done |
| `0x306AC68` | POWER service tick pointer `0x306A7C5` → `power_tick_wrap`, which calls the original tick, then asks the current screen to redraw when the animation frame changes. On the Echo it also runs the Echo ticks |
| `0x305945A` | `bne.w` to the bolt glyph on cable → two NOPs (the level pills animate instead) |

## 4 · Icons

19 × 7 battery glyphs `U+E007`–`U+E00B` (0–4 cells) and `U+E00C` (charging). Unused glyphs are
shrunk to 1 × 1 so the pack keeps its exact 1302 bytes. See [05-icons.md](05-icons.md).

## 5 · Cable chooser: Charge only / USB disk

| Site | Change |
|---|---|
| `0x3060AE4` / `0x3060AF0` | icon / label tables → Charge only, USB disk (DAC row removed) |
| `.data` `0x25A88`, `0x27584` | row count 3 → 2 (both copies of the `.data` image) |
| `0x305BA70` | opener ignores the on-cable setting: always cursor 0 with the 5 s countdown (`ldrb` → `movs r3,#1`) |
| `0x305B948` | select: row 0 = leave (charge only), row 1 = USB disk (`cbnz` → `cbz`) |
| `0x3059D96`, `0x3059FC2` | "not yet" note and hint for the old DAC row disabled |
| `0x305B9B0` | countdown expiry `bl 0x305B934` → `bl 0x305A618` (power off → charging screen) |

## 6 · Settings (module m48)

Rows hidden: Show tags (now always on), On USB connect, Swap up/down. Debug strip became the
Bluetooth row (see [12-bluetooth.md](12-bluetooth.md)). The label table at `0x307AF08`, the value `tbh` at `0x307A1EE`, the select `tbb` at
`0x307A73E` and every row-specific check (`0x307A262` Clock, `0x307A456` Equalizer, hints at
`0x307A0B6`/`0x307A0BA`/`0x307A0C2`, bounds `0x307A1E4`/`0x307A736`, count `0x307AC1E`) are
remapped, so each visible row still selects and shows its original setting. Module resealed.

## 7 · Buttons (`patches/keys.c`)

| Site | Change |
|---|---|
| m45 `0x307A12C` (6 B) | ↩ tap on Now Playing: play-mode cycle → `bl 0x305BA3C` (main menu) + `b.n 0x307A170` |
| m45 literal `0x307A1E8` | "MODE" label → `str_back` ("BACK") |
| m45 literal `0x307A1D8` | "menu" hold label → `str_settings` |
| `0x3060B80` | SHELL key filter pointer `0x305BD41` → `shell_key`: ≡ tap opens Files, hold = favourite (Now Playing / screen off) |

| m47 `0x307AAA4` | main menu key handler `0x307A109` → `menu_key`: hold ▶‖ on Files plays the whole internal drive |
| `0x305062C` | USB-disk loop `bl power_key_down` → `bl usb_key_exit`: ↩ (or power) leaves, after the key is released |

↩ hold stays previous track. See [07-buttons.md](07-buttons.md).

## 8 · Screens drawn by the mod (`patches/ui_np.c`)

| Site | Change |
|---|---|
| m45 `0x307A1F0` | Now Playing draw → `b.w np_draw` |
| m47 `0x307AAAC` | main menu draw pointer → `menu_draw` |
| `0x3060AD4` | cable chooser draw pointer → `usb_draw` |
| `0x3050D7C` | boot splash `bl 0x306C3E4` → `bl boot_screen` (a black frame) |
| `0x30594D4`, `0x305A4B0` | RECHO's `%d dB` volume text → `vol_text` (shown from 0 dB up) |

## 9 · Removed

Modules m49 (Spectrum) and m50 (Debug) are zeroed in the image; nothing can open them. Glyphs that
nothing draws are shrunk to 1 × 1 ([05-icons.md](05-icons.md)).

## Full-size Echo only

See [04-echo-port.md](04-echo-port.md) for the port and [06-echo-ui.md](06-echo-ui.md) for the screens.
