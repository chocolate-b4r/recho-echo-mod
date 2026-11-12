# Notices

## Not included, not redistributed

- **RECHO** (https://recho.atzkey.workers.dev/) is an independent custom firmware for the Echo Mini
  (build `2d11bdcd`, v0.10.1), the work of its author. It is not a FiiO or Snowsky product.
  This repository contains no part of it. The patcher reads your own copy and writes patch bytes
  plus the compiled hook code from `patches/`.
- **FiiO / Snowsky** make the Echo Mini and Echo hardware and the stock firmware (Echo Mini `HIFIEC40`,
  Echo `ECHOV190` / `ECHOV200`), © FiiO.
  It is mentioned only as the recovery image; get it from FiiO.

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
