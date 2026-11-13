# Notices

## RECHO: what this project is built on

- **RECHO** (https://recho.atzkey.workers.dev/) is an independent custom firmware for the Echo Mini
  (build `2d11bdcd`, v0.10.1), the work of its author. It is not a FiiO or Snowsky product, and its
  author is not involved in this project and has not endorsed it.
- This project patches RECHO. It needs your own copy of RECHO 0.10.1, and almost everything in a
  patched image is RECHO's: 99.4 % of the Echo Mini image and 99.3 % of the Echo image are RECHO's
  bytes unchanged. The mod's own code (16 KB for the Mini, 21.5 KB for the Echo) runs inside RECHO
  and calls 49 / 53 of RECHO's routines to do its work; the routines and addresses it relies on are
  listed in `docs/02-recho-map.md`.
- No RECHO image, code or data is distributed here. The patcher reads your copy and writes patch
  bytes plus the compiled hook code from `patches/`. The boot screen's 32 glyphs are extracted from
  your copy at build time (`build/boot_font.h`, not committed).
- **Correction:** commit `062c438` added those glyphs to the repository as `patches/boot_font.h`
  (32 characters of RECHO's large font, about 1 KB). Commit `3a7a4b6` removed the file and moved the
  extraction into the build; the earlier commit is still in the history.
- `docs/img/boot.png` is a picture of the patched boot screen and shows those glyphs, like a
  screenshot. The other pictures in `docs/img/` use a stand-in typeface.

## FiiO / Snowsky

- **FiiO / Snowsky** make the Echo Mini and Echo hardware and the stock firmware (Echo Mini `HIFIEC40`,
  Echo `ECHOV190` / `ECHOV200`), © FiiO. It is mentioned only as the recovery image; get it from FiiO.

## Learnt from other projects

- **Podium** (GPL-3.0), firmware for the full-size Echo: the ST7796S init sequence, the Echo pin map
  (power pair, DAC mute, 4.4 mm polarity, backlight), the maskrom key combination and the
  boot-loader reboot sequence. Values are hardware facts; the code here is written independently.
  `tools/verify_image.py` can compare the panel table with Podium's source if you point
  `PODIUM_PANEL` at `platform/device/logic/panel.c`.
- **rkdeveloptool** (Rockchip, GPL-2.0) with the Echo patch (5000 ms timeout, 32-sector transfers,
  maskrom id `2207:262D`) is the recovery tool. It is not included.
- **ReChord / livetrack.club** recovery guides for the Echo Mini: brick causes the verifier checks for.

## This repository

MIT licence, see [LICENSE](LICENSE). Addresses, byte patterns and descriptions of RECHO internals
in `docs/` are the result of reading the binary for interoperability.
