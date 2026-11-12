"""Export / import the RECHO "EchoIcon" icon pack (image part 2).

Pack layout (byte-verified on RECHO 0.10.1):
  0x00  "EchoIcon"
  0x08  u16 version (1)    0x0A u16 count (44)
  0x0C  u16 first codepoint (0xE000)   0x0E u16 line height (16)
  0x10  count x 12-byte entries: u8 w, u8 h, u8 a, u8 b, u32 flags, u32 data offset (from pack start)
  then 1-bpp bitmaps, rows MSB-first, ceil(w/8) bytes per row.

  py -3 -I icons.py export IMAGE OUTDIR    -> icon_XX_Uxxxx.png (1:1) + sheet.png (8x preview)
  py -3 -I icons.py import IMAGE INDIR OUT -> OUT = IMAGE with icons replaced from INDIR PNGs
                                              (black = off, anything brighter = on; new sizes allowed)
"""
import os
import struct
import sys

from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rkimg import parts, u32, seal  # noqa: E402

PART_INDEX = 2
LIMIT_SLACK = 0x200  # keep clear of the header self-copy


def locate(d):
    _, ps = parts(d)
    off, size = ps[PART_INDEX]
    if d[off:off + 8] != b"EchoIcon":
        raise SystemExit("part 2 is not an EchoIcon pack")
    return off, size


def parse(pack):
    _ver, count, first, height = struct.unpack_from("<HHHH", pack, 8)
    icons = []
    for i in range(count):
        w, h, a, b, flags, off = struct.unpack_from("<BBBBII", pack, 16 + 12 * i)
        stride = (w + 7) // 8
        rows = [pack[off + r * stride: off + (r + 1) * stride] for r in range(h)]
        icons.append(dict(w=w, h=h, a=a, b=b, flags=flags, rows=rows))
    return dict(version=_ver, first=first, height=height, icons=icons)


def build(meta):
    icons = meta["icons"]
    head = b"EchoIcon" + struct.pack("<HHHH", meta["version"], len(icons), meta["first"], meta["height"])
    data_off = len(head) + 12 * len(icons)
    table, blob = b"", b""
    for ic in icons:
        table += struct.pack("<BBBBII", ic["w"], ic["h"], ic["a"], ic["b"], ic["flags"], data_off + len(blob))
        blob += b"".join(ic["rows"])
    return head + table + blob


def to_image(ic):
    img = Image.new("L", (max(ic["w"], 1), max(ic["h"], 1)), 0)
    for y, row in enumerate(ic["rows"]):
        for x in range(ic["w"]):
            if row[x // 8] & (0x80 >> (x % 8)):
                img.putpixel((x, y), 255)
    return img


def from_image(img, template):
    img = img.convert("L")
    w, h = img.size
    stride = (w + 7) // 8
    rows = []
    for y in range(h):
        row = bytearray(stride)
        for x in range(w):
            if img.getpixel((x, y)) >= 128:
                row[x // 8] |= 0x80 >> (x % 8)
        rows.append(bytes(row))
    # byte `a` is the glyph's top offset in the 16 px line: keep the centre where it was
    a = max(0, template["a"] + (template["h"] - h) // 2)
    return dict(template, w=w, h=h, a=a, rows=rows)


def name(meta, i):
    return f"icon_{i:02d}_U{meta['first'] + i:04X}.png"


def export(path, outdir):
    d = open(path, "rb").read()
    off, size = locate(d)
    meta = parse(d[off:off + size])
    os.makedirs(outdir, exist_ok=True)
    scale, cell, cols = 8, 26, 8
    rows = (len(meta["icons"]) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * cell * scale // 2, rows * (cell * scale // 2 + 18)), (24, 24, 24))
    draw = ImageDraw.Draw(sheet)
    for i, ic in enumerate(meta["icons"]):
        img = to_image(ic)
        img.save(os.path.join(outdir, name(meta, i)))
        big = img.resize((img.width * scale // 2, img.height * scale // 2), Image.NEAREST).convert("RGB")
        cx, cy = (i % cols) * cell * scale // 2, (i // cols) * (cell * scale // 2 + 18)
        sheet.paste(big, (cx + 6, cy + 20))
        draw.text((cx + 6, cy + 4), f"{i:02d} U+{meta['first'] + i:04X} {ic['w']}x{ic['h']}", fill=(200, 200, 90))
    sheet.save(os.path.join(outdir, "sheet.png"))
    print(f"exported {len(meta['icons'])} icons to {outdir}")


def do_import(path, indir, out, keep_size=True):
    d = bytearray(open(path, "rb").read())
    off, size = locate(d)
    meta = parse(bytes(d[off:off + size]))
    changed = 0
    for i, ic in enumerate(meta["icons"]):
        p = os.path.join(indir, name(meta, i))
        if not os.path.exists(p):
            continue
        new = from_image(Image.open(p), ic)
        if new["rows"] != ic["rows"] or new["w"] != ic["w"] or new["h"] != ic["h"]:
            meta["icons"][i] = new
            changed += 1
    pack = build(meta)
    if keep_size:
        # Never touch the outer header: the pack must fit its original part size.
        if len(pack) > size:
            raise SystemExit(f"icon pack grew to {len(pack)} B (part is {size} B): shrink an unused glyph")
        d[off:off + size] = pack + bytes(size - len(pack))
    else:
        fw_size = u32(d, 0x1F4)
        if off + len(pack) > fw_size - LIMIT_SLACK:
            raise SystemExit(f"icon pack too large: {len(pack)} B, room for {fw_size - LIMIT_SLACK - off}")
        d[off:off + max(size, len(pack))] = pack + bytes(max(0, size - len(pack)))
        struct.pack_into("<I", d, 0x7C + 8 * PART_INDEX, len(pack))
    open(out, "wb").write(d)
    seal(out)
    print(f"{changed} icon(s) replaced, pack {size} -> {len(pack)} B")


def blank(path, codes, out):
    """Reduce glyphs nobody draws any more to an empty 1 x 1 entry (1 byte of bitmap).
    The table entry stays, so every other code point keeps its glyph; the pack keeps its
    part size, so the outer header is untouched."""
    d = bytearray(open(path, "rb").read())
    off, size = locate(d)
    meta = parse(bytes(d[off:off + size]))
    n = 0
    for cp in codes:
        i = cp - meta["first"]
        if not 0 <= i < len(meta["icons"]):
            raise SystemExit(f"U+{cp:04X} is not in the pack")
        ic = meta["icons"][i]
        if (ic["w"], ic["h"], ic["rows"]) != (1, 1, [b"\0"]):
            meta["icons"][i] = dict(ic, w=1, h=1, rows=[b"\0"])
            n += 1
    pack = build(meta)
    d[off:off + size] = pack + bytes(size - len(pack))
    open(out, "wb").write(d)
    seal(out)
    print(f"{n} unused icon(s) blanked, pack {len(pack)} of {size} B")


if __name__ == "__main__":
    cmd = sys.argv[1]
    if cmd == "export":
        export(sys.argv[2], sys.argv[3])
    elif cmd == "import":
        do_import(sys.argv[2], sys.argv[3], sys.argv[4])
