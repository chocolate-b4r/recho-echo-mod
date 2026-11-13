# Full-size Echo: research notes for a RECHO port (2026-11-12)

## Sources looked at
- Podium 4.6.0 source (podium-echo.online) — EchoPod fork for the full-size Echo, GPL-3.0.
- `intelarc/echo-mini-fw` — intelarc/echo-mini-fw: RE toolkit for FiiO's full-size Echo V1.9.0 (despite the name).
- Not downloaded: BlackVllad/ECHO-Firmware-Tool (Windows binaries in a personal repo, unverified),
  unitreign/ocean-flame (web image editor for stock firmware, not needed for a port),
  TheModSmith/snowsky-echo-firmware-notes (404). Reddit threads: blocked for automated reading.

## Hardware facts that matter (Mini -> Echo)
| | Mini | Echo | source |
|---|---|---|---|
| Panel | ST7789-class 320x170, MADCTL 0x60, rows +35 | ST7796S 480x222 landscape, MADCTL 0x28, rows +49 | Podium `logic/panel.c` |
| Panel init | RECHO `0x306B000` (2 variants by ID) | Podium `ST7796S_STEPS` | |
| Power key / hold / charger / backlight | GPIO2.5 / 2.6 / 2.3 / 2.4 | same | Podium `board/power.c` |
| Power pair | — | GPIO0.14 + GPIO2.0 high | Podium `logic/board_pins.c` |
| DAC mute / reset | GPIO2.0 / GPIO2.1 | GPIO2.1 / none | same |
| 4.4 mm detect (GPIO2.8) | low = plugged | high = plugged | Podium `docs/hardware.md` |
| ADC ch2 | key ladder | headset-remote mic | same |
| DAC | 2x CS43131 | 2x CS43198, driven with the CS43131 sequence (works) | intelarc README, Podium `dac_sequence.c` |
| Maskrom | 2207:262D, keys | same, proven with ECHOV200 | Podium `docs/device-recovery.md` |

## FiiO Echo updater/boot (intelarc)
- Scans `\ECHOECHO.IMG`, then `\ECHOV100..498.IMG`; downgrades allowed.
- Checks `RKnanoFW` and header copy; writes backup slot (8 KB chunks, read-back), header last; then primary; deletes file.
- Boot: compares the two header copies, boots the newer slot by header date. No signature; trailing CRC not read.

## RECHO display path (for the port)
- Panel init `0x306B000`, command sender `0x306AFDC(cmd, data*, len)`.
- Window `0x306B1F0(x0, y0, x1, y1)`: columns as given, rows +35 (`adds r5/r4,#35` at `0x306B20C/0x306B20E`).
- Full frame buffer 320x170 RGB565 at `0x03008A00` (108,800 B; screenshot loops 170 rows at `0x305C26E`).

## RECHO RAM map (core 0 data RAM 0x03004000-0x0304A240 usable)
| Start | Size | Owner (from code that uses it) |
|---|---|---|
| 0x03004214 | ~6.2 KB | equalizer state |
| 0x030061A0 | ~10 KB | LCD push / DMA |
| 0x03008A00 | 108,800 B | frame buffer 320x170 |
| 0x03024588 + 0x03026588 | 2 x 8 KB | storage / update sector buffers |
| 0x03028AE4 | 16 KB | log ring buffer |
| 0x0302F108 | ~8.4 KB | file browser lists (m44) |
| 0x030312C4 | 4 KB | icon pack copy / scan buffer |
| 0x03032748 | ~9 KB | unidentified |
| 0x03034B6C | 4 KB | boot-dump / system-backup protocol |
| 0x03035D98 | ~14 KB | player / UI state |
| 0x030399E8..0x0303F06C | ~18 KB | queue, playlist, path buffers |
| 0x030409A0 | ~9.8 KB | no-init area (boot markers, survives reset) |
| **0x03043000-0x0304A240** | **~29 KB** | **no references: free at run time** |
