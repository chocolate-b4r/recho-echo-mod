#!/usr/bin/env python3
"""Render text in RECHO's two bitmap fonts, straight from an image.

    py -3 -I tools/re/re_fonts.py IMAGE [outdir] ["text" ...]     (default: build/re)

Image parts 0 and 1 (the part chain at 0x070) are the fonts, one fixed-size cell per BMP code
point:
    part 0, small font: 32 B per glyph = 12 rows x 16 bits (MSB = left), byte 24 = 0x80 | advance
    part 1, large font: 33 B per glyph = 16 rows x 16 bits, byte 32 = 0x80 | advance
Both are 1-pixel strokes. Comparing them is how the "bold boot text" turned out not to be a font
choice: RECHO draws the boot screen before it loads either font (docs/13-reverse-engineering.md).
The pictures show RECHO's glyphs: keep them local.
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from rkimg import parts  # noqa: E402
from PIL import Image  # noqa: E402

FONTS = {0: (32, 12, 24), 1: (33, 16, 32)}    # bytes per glyph, rows, metadata byte


def render(d, part, text):
    off, _ = parts(d)[1][part]
    gs, rows, meta = FONTS[part]
    im = Image.new("L", (8 + 12 * len(text), rows + 4), 0)
    x = 2
    for ch in text:
        g = d[off + ord(ch) * gs: off + (ord(ch) + 1) * gs]
        for r in range(rows):
            v = (g[2 * r] << 8) | g[2 * r + 1]
            for c in range(16):
                if v & (0x8000 >> c) and x + c < im.width:
                    im.putpixel((x + c, 2 + r), 255)
        x += (g[meta] & 0x7F) or 4
    return im.crop((0, 0, x + 2, im.height))


def main():
    d = open(sys.argv[1], "rb").read()
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join("build", "re")
    texts = sys.argv[3:] or ["Power off", "> loading kernel...", "0123456789 %./:[]"]
    os.makedirs(out, exist_ok=True)
    for part in (0, 1):
        lines = [render(d, part, t) for t in texts]
        sheet = Image.new("L", (max(i.width for i in lines), sum(i.height for i in lines)), 0)
        y = 0
        for i in lines:
            sheet.paste(i, (0, y))
            y += i.height
        path = os.path.join(out, f"font{part}.png")
        sheet.resize((sheet.width * 4, sheet.height * 4), Image.NEAREST).save(path)
        print(path)


if __name__ == "__main__":
    main()
