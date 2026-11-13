#!/usr/bin/env python3
"""Slice a RECHO image into fw1 and its SFMD modules and disassemble each one.

    py -3 -I tools/re/re_dump.py RECHO-0.10.1.IMG [outdir]      (default: build/re)

Writes, at the run addresses the code really executes at:
    fw1.bin / fw1.od            fw1 (image offset + 0x0304F298), Thumb
    m44.bin / m44.od ...        every SFMD module, loaded at 0x0307A000 (header included)
    modules.txt                 module id, image offset, size

Everything written here is RECHO's code: keep it local (build/ is git-ignored). Needs
arm-none-eabi-objdump; set OBJDUMP if it is not in the default Windows location.
"""
import os
import struct
import subprocess
import sys

RAM_OF_IMG = 0x0304F298
MOD_BASE = 0x0307A000
FW1_END = 0x0306D92C          # end of fw1 code (the code cave starts after it)
OBJDUMP = os.environ.get("OBJDUMP", "C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/"
                                    "12.2 mpacbti-rel1/bin/arm-none-eabi-objdump.exe")


def modules(d):
    """SFMD modules: 'SFMD', u32 id, ?, u32 size (header included)."""
    out, i = [], 0
    while True:
        i = d.find(b"SFMD", i)
        if i < 0 or i > 0x44F000:
            return out
        mid, size = struct.unpack_from("<I", d, i + 4)[0], struct.unpack_from("<I", d, i + 12)[0]
        if mid < 200 and 0 < size < 0x20000:
            out.append((mid, i, size))
        i += 4


def disasm(binpath, vma, odpath):
    r = subprocess.run([OBJDUMP, "-D", "-b", "binary", "-marm", "-Mforce-thumb",
                        f"--adjust-vma={vma:#x}", binpath], capture_output=True, text=True)
    open(odpath, "w", encoding="utf-8").write(r.stdout)


def main():
    d = open(sys.argv[1], "rb").read()
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join("build", "re")
    os.makedirs(out, exist_ok=True)
    fw1 = d[0x03050000 - RAM_OF_IMG:FW1_END - RAM_OF_IMG]
    open(os.path.join(out, "fw1.bin"), "wb").write(fw1)
    disasm(os.path.join(out, "fw1.bin"), 0x03050000, os.path.join(out, "fw1.od"))
    lines = []
    for mid, off, size in modules(d):
        name = f"m{mid}"
        open(os.path.join(out, name + ".bin"), "wb").write(d[off:off + size])
        disasm(os.path.join(out, name + ".bin"), MOD_BASE, os.path.join(out, name + ".od"))
        lines.append(f"{name}\timage {off:#x}\tsize {size}")
    open(os.path.join(out, "modules.txt"), "w").write("\n".join(lines) + "\n")
    print(f"{out}: fw1 + {len(lines)} modules")


if __name__ == "__main__":
    main()
