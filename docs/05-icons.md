# 05 · Icons

RECHO draws its icons as text: private-use code points `U+E000`–`U+E02B` resolved from the
`EchoIcon` pack (part 2, IMG `0x44F2F4`, 1302 bytes). In C you write them as UTF-8,
e.g. `"\xee\x80\x83"` for `U+E003`.

| Code | Glyph | Used by |
|---|---|---|
| E000 / E001 / E002 | play / pause / stop | status bar, Now Playing |
| E003–E006 | play order normal / shuffle / repeat one / repeat all | Now Playing, Echo Up Next header |
| E02B | infinite shuffle | same |
| E007–E00B | battery 0–4 cells (19 × 7 in this mod) | status bar, charging animation |
| E00C | charging (end cells + bolt) | cable chooser |
| E00E / E00F / E010 | other jack / speaker / muted | status bar |
| E011–E017 | cassette, folder, USB, wave, gear, terminal, power | main menu |
| E018 / E019 | two circles / circle (stereo / mono) | Now Playing |
| E01A | chevron `>` | list rows |
| E021 | dot separator | Now Playing format line |
| E022–E028 | FLAC / WAV / MP3 / EQ / RG / A / T badges | Now Playing |
| E02A | info | main menu: About |

## Changing icons

```bash
python tools/icons.py export RECHO-0.10.1.IMG icons/original   # one PNG per glyph + sheet.png
# edit copies (white on black, 1 bit) in icons/mine/
python tools/patch_recho.py RECHO-0.10.1.IMG out/HIFIEC90.IMG --icons icons/mine
python tools/icon_layout.py RECHO-0.10.1.IMG icons/layout     # labelled sheet + CSV of meanings
```

- The rebuilt pack must fit the original 1302 bytes, so the outer header never changes. To make
  room, shrink a glyph nobody uses to 1 × 1 (this mod does that to the Supporters heart `E01C`
  and the unused skip glyphs).
- `from_image` keeps glyphs vertically centred by adjusting their y offset.
- The firmware's icon loader (`0x3058BC8`) has no checksum. A malformed pack makes it fall back to
  built-in glyphs; it does not brick. The verifier applies the loader's own rules anyway.
- `tools/make_battery_icons.py` generates the 19 × 7 battery set.
