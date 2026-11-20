# Notices

## RECHO: what this project is built on

- **RECHO** (https://recho.atzkey.workers.dev/) is an independent custom firmware for the Echo Mini
  (build `2d11bdcd`, v0.10.1), the work of its author. It is not a FiiO or Snowsky product, and its
  author is not involved in this project and has not endorsed it.
- This project patches RECHO. It needs your own copy of RECHO 0.10.1, and almost everything in a
  patched image is RECHO's: 99.3 % of the Echo Mini image and of the Echo image are RECHO's bytes
  unchanged. The mod's own code (26.8 KB for the Mini, 27.2 KB for the Echo) runs inside RECHO and
  calls 71 / 72 of RECHO's routines to do its work; the routines and addresses it relies on are
  listed in `docs/02-recho-map.md`.
- **What is distributed, and what is not:**
  - The source code, tools and documentation in this repository contain no RECHO code or data. The
    patcher reads your own copy of RECHO and writes patch bytes plus the compiled hook code from
    `patches/`. (Until v47 the build also extracted 32 glyphs of RECHO's large font from your copy
    for the boot screen; the boot screen is now a black frame and nothing is extracted.)
  - The GitHub releases contain the mod's own files only (compiled hook code, patcher, verifier,
    icons) and the checksums of the images they produce. They contain no RECHO image.
  - **`docs/img/echo mini.zip` and `docs/img/echo.zip` are complete patched firmware images.** They
    contain RECHO's firmware (about 99.3 % of each image is RECHO's bytes unchanged). They were
    added to the repository by its owner. No permission from RECHO's author to redistribute RECHO is
    recorded here; if you are RECHO's author and want them removed, open an issue and they will be.
- **Correction:** commit `062c438` added those glyphs to the repository as `patches/boot_font.h`
  (32 characters of RECHO's large font, about 1 KB). Commit `3a7a4b6` removed the file and moved the
  extraction into the build; the earlier commit is still in the history.
- The pictures in `docs/img/` use a stand-in typeface. (`docs/img/boot.png`, a picture of the old
  boot screen drawn with those glyphs, was removed with it; it is still in the history.)

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
