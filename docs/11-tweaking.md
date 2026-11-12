# 11 · Tweaking guide

How to add your own change without bricking anything.

## 1. Find the code

Disassemble fw1 (IMG `0xD68`, base `0x03050000`) and the module you care about (base
`0x0307A000`, header included):

```bash
python - <<'EOF'
d = open("RECHO-0.10.1.IMG", "rb").read()
open("fw1.bin", "wb").write(d[0xD68:0x273D4])
open("m45.bin", "wb").write(d[0x2A494:0x2A494 + int.from_bytes(d[0x2A494+12:0x2A494+16], "little")])
EOF
arm-none-eabi-objdump -D -b binary -marm -Mforce-thumb --adjust-vma=0x03050000 fw1.bin > fw1.od
arm-none-eabi-objdump -D -b binary -marm -Mforce-thumb --adjust-vma=0x0307A000 m45.bin > m45.od
```

Strings make good entry points: RECHO logs a lot (`"rtc (%s): ..."`, `"ui: volume %d dB ..."`).
Find the string, find the literal that points at it, read the function around it.
[02-recho-map.md](02-recho-map.md) lists what is already known.

## 2. Write the hook in C

Add a file in `patches/` (and to `tools/build_hooks.sh`). The pattern:

```c
static inline u32 lit(u32 v) { __asm__("" : "+r"(v)); return v; }
#define FN(addr, type) ((type)lit((addr) | 1u))      /* Thumb: bit 0 set */
#define draw_text FN(0x03058D84, int (*)(int, int, int, const char *, int, u32, int))

#define STATE __attribute__((section(".text.my_state,\"ax\",%progbits@"), used))
STATE volatile u32 my_counter = 0;                     /* no .data/.bss allowed */

void my_hook(void) { /* ... */ }
```

Export the symbol in `ENTRIES` and `-u` in `build_hooks.sh` if nothing in C calls it.

## 3. Wire it in

In `tools/patch_recho.py`, always with the original bytes:

```python
put(d, SITE - RAM_OF_IMG, thumb_bl(SITE, ORIGINAL_TARGET), thumb_bl(SITE, e["my_hook"]), "what this is")
```

- Function pointers in tables: `struct.pack("<I", addr | 1)`.
- 4-byte prologue replacement: `thumb_bw` plus a naked trampoline that replays the original
  instructions (see `gpio_set_orig`).
- Module edits: patch at `MODULE_IMG + (addr - 0x0307A000)` and call `reseal_module()`.

Good hook points:
- the POWER tick wrapper (main loop, every pass)
- a screen descriptor's draw / key / soft-key pointers
- a single `bl` you want to redirect

## 4. Teach the verifier

Add every new call target, variable and register to the allow-lists with a comment, add the patch
site to the allowed regions in section [2], and write a check that looks for exactly your bytes.
Run it against the original as well; it must fail there.

## 5. Test without risk first

- `tests/host_render.c` shows how to compile drawing code for the PC and look at the result.
- Keep changes display-only until they're proven; don't write storage from a hook.
- Flash, check `ECHOLOG.TXT` (Settings → Log to file), and keep the recovery steps at hand.
