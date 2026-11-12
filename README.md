# recho-echo-mod

Binary patches for **[RECHO](https://recho.atzkey.workers.dev/) 0.10.1** (build `2d11bdcd`), an independent
custom firmware for the FiiO / Snowsky **Echo Mini**, plus a port of it to the full-size **Snowsky Echo**.
RECHO is not made by FiiO or Snowsky; they make the players and their stock firmware.

Everything here is a script, C source or document. **No firmware image is included.** You patch
your own copy of RECHO 0.10.1 and get a new image that a verifier checks byte by byte before you
flash anything.

| Player | What you get |
|---|---|
| Echo Mini | Off-state charging screen, charging animation, 19 × 7 battery glyphs, menu without Spectrum / Supporters / Debug, shorter Settings, cable chooser "Charge only / USB disk" (no DAC) that powers off into the charging screen after 5 s, button remap, no button-hint bar, own Now Playing screen (clock, queue position, battery %, red paused state), two-halves cable chooser |
| Echo (full size) | All of the above, plus ST7796S panel support, Echo pin map, Up Next column with battery %, centred layout and charging screen, RECHO's bar messages on their own line, battery glyph removed from RECHO's top bars, RECHO's own updater re-enabled under Echo-only file names |

> **Use at your own risk.** Flashing custom firmware can leave a player unable to start until it
> is recovered over USB. Read [docs/10-install-and-recovery.md](docs/10-install-and-recovery.md)
> before the first flash and keep the recovery tools ready.

## Quick start

Requirements: Python 3.10+, Pillow (only for icons), the Arm GNU Toolchain (`arm-none-eabi-gcc`
12.x) and bash (Git for Windows' bash works).

```bash
# 1. compile the hook code that goes into the firmware's code cave
tools/build_hooks.sh            # Echo Mini  -> build/hooks.*
tools/build_hooks.sh echo       # full Echo  -> build/hooks-echo.*

# 2. patch your copy of RECHO 0.10.1 (it refuses any other build)
python tools/patch_recho.py RECHO-0.10.1.IMG out/HIFIEC90.IMG              # Echo Mini
python tools/patch_recho.py RECHO-0.10.1.IMG out/RECHOECH.IMG --echo       # full Echo

# 3. check every changed byte against the original before flashing
python tools/verify_image.py RECHO-0.10.1.IMG out/HIFIEC90.IMG
python tools/verify_image.py RECHO-0.10.1.IMG out/RECHOECH.IMG --echo
```

Set `ARM=/path/to/arm-none-eabi/bin` and `OBJDUMP=/path/to/arm-none-eabi-objdump` if the toolchain is
not in the default Windows location. Steps 1 and 2 must use the same hook build: the verifier
compares the cave bytes in the image with `build/hooks*.bin`.

Optional: `--icons DIR` replaces glyphs in the icon pack (see [docs/05-icons.md](docs/05-icons.md)).

## Installing

| Player | First install | Later updates |
|---|---|---|
| Echo Mini | copy `HIFIEC90.IMG` to the top of the internal drive, eject, restart | same |
| Echo | from FiiO's firmware: copy the image as `ECHOVnnn.IMG` | copy it as `RECHOECH.IMG` (RECHO's own updater) |

Details, maskrom recovery and the exact commands: [docs/10-install-and-recovery.md](docs/10-install-and-recovery.md).

## Documentation

| Doc | Contents |
|---|---|
| [01 Firmware format](docs/01-firmware-format.md) | RKnanoFW container, load rows, CRC, SFMD modules, part chain, sealing |
| [02 RECHO map](docs/02-recho-map.md) | Addresses of every RECHO routine and variable these patches use, and what they do |
| [03 Echo Mini patches](docs/03-mini-patches.md) | Every patch site: original bytes, new bytes, reason |
| [04 Echo port](docs/04-echo-port.md) | Hardware differences, panel, pins, updater, flash layout evidence |
| [05 Screens and icons](docs/05-icons.md) | Icon pack, glyph codes, how to replace glyphs |
| [06 Echo user interface](docs/06-echo-ui.md) | Panel layout, Up Next, Now Playing, top bars, cable chooser, message line |
| [07 Buttons](docs/07-buttons.md) | Key ids, events, the remap and how it is wired |
| [08 Memory and code cave](docs/08-memory.md) | RAM map, free RAM, cave rules, why no `.data`/`.bss` |
| [09 Verifier](docs/09-verifier.md) | Every check `verify_image.py` runs and why |
| [10 Install and recovery](docs/10-install-and-recovery.md) | Flashing, maskrom, rkdeveloptool, Zadig |
| [11 Tweaking guide](docs/11-tweaking.md) | How to add your own patch safely |
| [12 Bluetooth](docs/12-bluetooth.md) | Bluetooth work in progress: the diagnostic build and the hardware facts |
| [CHANGELOG](docs/CHANGELOG.md) | Build history (Mini v1–v8, Echo stages 1–7) |

## Repository layout

```
patches/   C code linked into the firmware's free code cave (0x0306E000..)
tools/     patcher, verifier, image tools, icon tools, hook build script
tests/     host-side render test for the charging screen
docs/      everything above
```

## Credits and licences

See [NOTICE.md](NOTICE.md). RECHO is © its author ([recho.atzkey.workers.dev](https://recho.atzkey.workers.dev/));
the players and their stock firmware are © FiiO / Snowsky. Facts about the
full-size Echo (panel init table, pin map, maskrom procedure) were learnt from the Podium project
(GPL). This repository's own code and documents are MIT licensed ([LICENSE](LICENSE)).

## Design mock-ups

`tools/render_mockups.py` draws the main menu at each panel's real size from the patch sources
(`docs/design/`: 1x, 4x and a blank zone template per player). The typeface is a stand-in, and the
mock-ups show the v23 / stage 24 menu, before the owner's redesign.
