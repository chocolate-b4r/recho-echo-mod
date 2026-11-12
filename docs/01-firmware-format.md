# 01 · Firmware format (RKnanoFW, RECHO 0.10.1)

The Echo Mini and the full-size Echo use Rockchip's RKnano family SoC: two Cortex-M3 cores, core A
for the UI and core B for decoding. A RECHO image is a 5 MiB (`0x500004`-byte) file in Rockchip's
**RKnanoFW** container. All offsets below were checked byte by byte against
`RECHO 0.10.1 (2d11bdcd)`; `tools/rkimg.py info IMAGE` prints them for any image.

## Map

| IMG offset | Contents |
|---|---|
| `0x000`–`0x1F7` | outer header: date, version (`0x004`), brand `RECHO` (`0x010`), build tag `2d11bdcd` (`0x030`), part chain (`0x070`–`0x08C`), fw size `0x450000` (`0x1F4`) |
| `0x1F8` | `RKnanoFW` magic, base `0x03050000` |
| `0x208` | 91 load rows: (source, destination, length) — the boot ROM copies code and data with them |
| `0xD68`–`0x273D3` | **fw1 code**, runs at `0x03050000`. Conversion: `RAM = 0x0304F298 + IMG` |
| `0x258D8` | `.data` load image as seen from fw1 (RAM `0x03074B70`), copied to `0x03004000` at boot |
| `0x273D4`–`0x28ECF` | second copy of the `.data` image |
| `0x28ED4`… | **SFMD modules** (screens), see below |
| `0x3B8E8` | part header; parts: font A, font B, `EchoIcon` icon pack (`0x44F2F4`, 1302 B) |
| `0x450000` | copy of the first 512 header bytes (must match the header) |
| `0x500000` | CRC-32 trailer over `[0, 0x500000)` |

### CRC

Polynomial `0x04C10DB7`, MSB first, initial value 0, no final XOR (Rockchip's). The same routine
signs modules. `tools/rkimg.py seal IMAGE` rewrites the header self-copy and the trailer; the patcher
calls it last. FiiO's own updater ignores the trailer, but RECHO's updater and the verifier check it.

### Code cave

fw1 code ends at `0x0306D92C`; the `.data` load image starts at `0x03074B70`. The 29 KB between them
is all zero and nothing references it. The patches link their code at **`0x0306E000`**. It is loaded
by the existing first load row, so the boot ROM copies it like any other code. See
[08-memory.md](08-memory.md).

## SFMD modules

Screens are overlays. Each module is loaded **whole, header included, at `0x0307A000`** when its
screen opens, so only one module is in memory at a time.

| IMG | Module | Screen |
|---|---|---|
| `0x28ED4` | m44 | Files |
| `0x2A494` | m45 | Now Playing |
| `0x2B224` | m47 | Main menu |
| `0x2BDD8` | m48 | Settings |
| `0x2CD58` | m49 | Spectrum (unreachable after the menu patch) |
| `0x2E43C` | m50 | Debug (unreachable after the menu patch) |
| … | m78 | core-B decoder firmware |

Header (32 bytes): magic `SFMD`, id, key `0x1E9D3409`, length including the header, load end,
bss end, **CRC at +24** (same CRC-32, computed with this field zeroed), descriptor pointer, 0.
`reseal_module()` in `tools/patch_recho.py` recomputes it after a module edit. The module loader
rejects a module whose CRC does not match.

Code inside a module is at `0x0307A000 + (IMG − module offset)`. The patches reach the cave from
modules with ordinary `bl`/`b.w` (±16 MB range).

## Icon pack (`EchoIcon`)

Part 2 of the part chain: 1302 bytes of 1-bit glyphs drawn as text through private-use code points
`U+E000`–`U+E02B`. The patcher rebuilds it in place and keeps the exact size, so the outer header
never changes. See [05-icons.md](05-icons.md).

## What must never change

ReChord's brick log and our own checks agree on these:

- the outer header, the `RKnanoFW` header and all 91 load rows (wrong rows = no boot)
- code crossing the loader base `0x03088000`, or RAM use above `0x0304A000`
- `.data`/`.bss` sizes (the hooks may not add any; see [08-memory.md](08-memory.md))
- module headers other than the CRC

`tools/verify_image.py` checks all of them. See [09-verifier.md](09-verifier.md).
