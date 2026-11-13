#!/usr/bin/env python3
"""Who calls a RECHO routine, and where is it stored as a pointer?

    py -3 -I tools/re/re_callers.py RECHO-0.10.1.IMG 305BC6C 3050260 ...

For each target (hex RAM address) it lists every Thumb-2 `bl` / `b.w` in fw1 that lands on it,
and every literal word holding the address with the Thumb bit set (screen descriptors, service
tables, callbacks). This is how the USB-disk loop's key callback (0x30505EC), the after-USB
routine (0x305BC6C) and the USB screen descriptor (0x305F35C) were found.
"""
import struct
import sys

RAM_OF_IMG = 0x0304F298


def bl_target(h1, h2, pc):
    s = (h1 >> 10) & 1
    j1, j2 = (h2 >> 13) & 1, (h2 >> 11) & 1
    i1, i2 = 1 - (j1 ^ s), 1 - (j2 ^ s)
    off = (s << 24) | (i1 << 23) | (i2 << 22) | ((h1 & 0x3FF) << 12) | ((h2 & 0x7FF) << 1)
    if s:
        off -= 1 << 25
    return pc + 4 + off


def main():
    d = open(sys.argv[1], "rb").read()
    targets = {int(x, 16) & ~1 for x in sys.argv[2:]}
    lo, hi = 0x03050000 - RAM_OF_IMG, 0x0306D92C - RAM_OF_IMG
    for o in range(lo, hi, 2):
        h1, h2 = struct.unpack_from("<HH", d, o)
        if (h1 & 0xF800) == 0xF000 and (h2 & 0xD000) in (0xD000, 0x9000):
            t = bl_target(h1, h2, RAM_OF_IMG + o)
            if t in targets:
                kind = "bl " if h2 & 0x4000 else "b.w"
                print(f"{kind} at {RAM_OF_IMG + o:#x} -> {t:#x}")
    for t in sorted(targets):
        p = struct.pack("<I", t | 1)
        j = d.find(p)
        while j >= 0:
            print(f"pointer {t | 1:#x} stored at {RAM_OF_IMG + j:#x} (image {j:#x})")
            j = d.find(p, j + 1)


if __name__ == "__main__":
    main()
