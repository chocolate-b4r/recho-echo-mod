"""Export the icon pack of an image as an editing kit: one PNG per glyph, a labelled
sheet and a CSV with what each glyph is and where RECHO uses it.

  py -3 -I icon_layout.py IMAGE OUTDIR
"""
import csv
import os
import sys

from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from icons import locate, parse, to_image, name  # noqa: E402

# Meaning (from the artwork) and use (from where the UTF-8 code appears in fw1 / modules).
INFO = {
    0x00: ("Play", "status bar, Now playing, Debug"),
    0x01: ("Pause", "status bar, Now playing"),
    0x02: ("Stop", "Now playing"),
    0x03: ("Play order: normal", "Now playing"),
    0x04: ("Play order: shuffle", "Now playing"),
    0x05: ("Repeat one", "Now playing"),
    0x06: ("Repeat all", "Now playing"),
    0x07: ("Battery 0 cells", "status bar (also charging animation)"),
    0x08: ("Battery 1 cell", "status bar (also charging animation)"),
    0x09: ("Battery 2 cells", "status bar (also charging animation)"),
    0x0A: ("Battery 3 cells", "status bar (also charging animation)"),
    0x0B: ("Battery 4 cells / full", "status bar"),
    0x0C: ("Charging bolt", "USB mode chooser (Charge only)"),
    0x0D: ("Headphones", "not referenced directly (may be computed)"),
    0x0E: ("Headphones, other jack", "status bar"),
    0x0F: ("Speaker / volume", "status bar"),
    0x10: ("Speaker muted", "status bar"),
    0x11: ("Cassette", "main menu: Now playing; status bar"),
    0x12: ("Folder", "main menu: Files; file browser"),
    0x13: ("USB plug", "main menu: USB disk; USB chooser"),
    0x14: ("Wave", "USB chooser: USB DAC (was Spectrum)"),
    0x15: ("Gear", "main menu: Settings; Settings title"),
    0x16: ("Terminal", "main menu: Debug; Debug title"),
    0x17: ("Power", "main menu: Power off"),
    0x18: ("Two circles", "Now playing"),
    0x19: ("Circle", "Now playing"),
    0x1A: ("Chevron >", "list rows (Files, menu, Settings)"),
    0x1B: ("Menu lines", "soft-key hint (menu button)"),
    0x1C: ("Heart (blanked)", "was Supporters; unused now, kept 1x1 for space"),
    0x1D: ("Previous", "not referenced directly (may be computed)"),
    0x1E: ("Next", "not referenced directly (may be computed)"),
    0x1F: ("Skip back", "not referenced directly (may be computed)"),
    0x20: ("Skip forward", "not referenced directly (may be computed)"),
    0x21: ("Dot", "Now playing separator"),
    0x22: ("FLAC badge", "Now playing format"),
    0x23: ("WAV badge", "Now playing format"),
    0x24: ("MP3 badge", "Now playing format"),
    0x25: ("EQ badge", "Now playing"),
    0x26: ("RG badge", "Now playing (ReplayGain)"),
    0x27: ("Small A", "Now playing (RG album)"),
    0x28: ("Small T", "Now playing (RG track)"),
    0x29: ("Small x", "Now playing"),
    0x2A: ("Info", "main menu: About"),
    0x2B: ("Infinite shuffle", "Now playing"),
}


def main(path, out):
    d = open(path, "rb").read()
    off, size = locate(d)
    meta = parse(d[off:off + size])
    os.makedirs(os.path.join(out, "png"), exist_ok=True)
    rows, total = [], 16 + 12 * len(meta["icons"])
    for i, ic in enumerate(meta["icons"]):
        to_image(ic).save(os.path.join(out, "png", name(meta, i)))
        cost = ((ic["w"] + 7) // 8) * ic["h"]
        total += cost
        what, where = INFO.get(i, ("?", "?"))
        rows.append((i, f"U+{meta['first'] + i:04X}", f"{ic['w']}x{ic['h']}", cost, what, where))
    with open(os.path.join(out, "icons.csv"), "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["index", "codepoint", "size", "bytes", "what", "where it is used"])
        w.writerows(rows)

    scale, cols, cw, chh = 6, 4, 330, 136
    sheet = Image.new("RGB", (cols * cw + 20, ((len(rows) + cols - 1) // cols) * chh + 60), (22, 22, 26))
    dr = ImageDraw.Draw(sheet)
    try:
        font = ImageFont.truetype("arial.ttf", 13)
        small = ImageFont.truetype("arial.ttf", 11)
        big = ImageFont.truetype("arialbd.ttf", 16)
    except OSError:
        font = small = big = ImageFont.load_default()
    dr.text((12, 10), f"RECHO icon pack — {len(rows)} glyphs, {total} of {size} bytes used "
            f"({size - total} free). Black = off, white = on.", fill=(235, 235, 235), font=big)
    for k, (i, cp, sz, cost, what, where) in enumerate(rows):
        x, y = 10 + (k % cols) * cw, 44 + (k // cols) * chh
        dr.rectangle((x, y, x + cw - 10, y + chh - 10), outline=(70, 70, 80))
        img = to_image(meta["icons"][i]).resize((meta["icons"][i]["w"] * scale, meta["icons"][i]["h"] * scale),
                                                Image.NEAREST).convert("RGB")
        sheet.paste(img, (x + 8, y + 8))
        dr.text((x + 8, y + chh - 50), f"{i:02d}  {cp}  {sz}  ({cost} B)", fill=(230, 200, 90), font=font)
        dr.text((x + 8, y + chh - 34), what, fill=(235, 235, 235), font=font)
        dr.text((x + 8, y + chh - 20), where[:52], fill=(150, 150, 160), font=small)
    sheet.save(os.path.join(out, "icon-sheet.png"))
    print(f"{len(rows)} icons, pack {total}/{size} B, sheet + png/ + icons.csv in {out}")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
