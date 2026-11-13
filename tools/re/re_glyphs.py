#!/usr/bin/env python3
"""Where is each icon glyph (U+E000..U+E02B) referenced?

    py -3 -I tools/re/re_glyphs.py IMAGE

RECHO draws icons as text: a glyph is used wherever its UTF-8 bytes (EE 80 xx) appear in a
string. This lists every occurrence outside the icon pack, as fw1 or module run addresses.
Hits far into the image (fonts, resources) are byte patterns, not strings: check the context
before calling a glyph "used". This is how the unused glyphs in docs/05-icons.md were found.
"""
import struct
import sys

RAM_OF_IMG = 0x0304F298
PACK = 0x44F2F4               # EchoIcon pack, 1302 bytes


def modules(d):
    out, i = [], 0
    while True:
        i = d.find(b"SFMD", i)
        if i < 0 or i > 0x44F000:
            return out
        mid, size = struct.unpack_from("<I", d, i + 4)[0], struct.unpack_from("<I", d, i + 12)[0]
        if mid < 200 and 0 < size < 0x20000:
            out.append((i, i + size, mid))
        i += 4


def main():
    d = open(sys.argv[1], "rb").read()
    mods = modules(d)

    def where(off):
        for a, b, m in mods:
            if a <= off < b:
                return f"m{m}@{0x0307A000 + off - a:#x}"
        return f"fw1@{RAM_OF_IMG + off:#x}"

    for cp in range(0xE000, 0xE02C):
        pat, hits, o = chr(cp).encode(), [], 0
        while True:
            o = d.find(pat, o)
            if o < 0:
                break
            if not PACK <= o < PACK + 1302:
                hits.append(where(o))
            o += 1
        print(f"U+{cp:04X} {len(hits):2d}  {' '.join(hits[:8])}")


if __name__ == "__main__":
    main()
