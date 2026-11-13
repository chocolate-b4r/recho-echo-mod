# 08 · Memory and the code cave

## RAM (core A)

| Range | Size | Use |
|---|---|---|
| `0x03004000`… | | `.data` (from the load image), then `.bss` |
| `0x03004214` | ~6 KB | equaliser state |
| `0x03008A00`–`0x030231FF` | 108,800 B | frame buffer 320 × 170 RGB565 |
| `0x03028AE4` | 16 KB | log ring |
| `0x03034B6C` | 4 KB | boot dump |
| `0x03043002`–`0x0304A240` | ~29 KB | **free**: nothing references it |
| `0x0304A000` | | ceiling ReChord's brick log warns about |
| `0x03050000`–`0x0306D92B` | | fw1 code |
| `0x0306D92C`–`0x03074B6F` | ~29 KB | **code cave** (zero in the image) |
| `0x03074B70` | | `.data` load image |
| `0x0307A000` | | module overlay (one SFMD module at a time) |

"Nothing references it" is proven, not assumed. `verify_image.py` decodes every PC-relative
`ldr`/`ldr.w` in fw1 and in every module, plus every word of `.data`, and fails if any value
points into the free range.

## The mod's use of free RAM

| Address | Size | Echo | Echo Mini |
|---|---|---|---|
| `0x03044000` | 16,640 B | strip: a saved frame-buffer corner (≤ 8,320 px) while the port draws | Up Next name cache, 9 × 64 B |
| `0x03048200` | 276 B | file name buffer for Up Next | same |
| `0x03048400` | 7,680 B | lyrics (`.lrc` text) | unused |

## Cave rules

The hooks are linked at `0x0306E000` (`-Ttext=0x0306E000`) and copied into the image. The current
sizes are about 18 KB for the Mini and 26 KB for the Echo, of 27.4 KB available (`0x0306E000` up to
`0x03074B70`, where RECHO's `.data` load image starts). The Echo has about 1 KB left: check the end
of the blob before adding a feature there.

- **No `.data`, no `.bss`.** Those would need load rows the image does not have.
  `tools/build_hooks.sh` refuses a build that produces any. State variables live in the cave itself
  (plain SRAM, no MPU) via `__attribute__((section(".text.name,\"ax\",%progbits@")))`, with an
  initial value.
- **No libc.** `-ffreestanding -fno-builtin -nostdlib`; Cortex-M3 has hardware divide, so 32-bit
  `/` and `%` are fine; avoid 64-bit division.
- **Every firmware address goes through `lit()`**: an empty `asm` that stops the compiler from
  turning addresses into base + offset arithmetic. Each one then appears as a literal the verifier
  can audit against its allow-lists.
- **Call RECHO only where RECHO itself would**: from the main loop (the POWER tick), never from an
  interrupt. Before panel access, call `0x306B534`.
- **Static arrays inside functions become `.bss`**; give them the state section instead.
