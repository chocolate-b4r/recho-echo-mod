#!/usr/bin/env python3
"""Render the main menu as it is drawn on each player, at the panels' real resolutions.

    py -3 tools/render_mockups.py [out_dir]          (default: docs/design)

Icons, colours and every coordinate come from patches/ui_np.c and the Echo strip / Up Next
sources, so positions are pixel-exact. The typeface is a stand-in: RECHO's two bitmap fonts
(about 16 px and 12 px tall) live in the firmware and are drawn here with Arial at those sizes.

Writes, per player, the screen at 1x and 4x and a blank template with the zones outlined:
    menu-mini-320x170.png        menu-mini-320x170-x4.png        template-mini-320x170.png
    menu-echo-480x222.png        menu-echo-480x222-x4.png        template-echo-480x222.png
"""
import os
import re
import sys

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = open(os.path.join(ROOT, "patches", "ui_np.c"), encoding="utf-8").read()


def c565(v):
    return (((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31)


WHITE, SOFT, GREY, DIM, RULE = c565(0xF79E), c565(0xBDD7), c565(0x8410), c565(0x52AA), c565(0x4208)
GREEN, TAB_DIM = c565(0x47E8), c565(0x4A69)
NEXT_FIRST, NEXT_DIM = c565(0xE71C), c565(0x6B4D)


def font(size):
    for name in ("arial.ttf", "DejaVuSans.ttf"):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            pass
    return ImageFont.load_default()


F1, F0, F2 = font(14), font(10), font(28)   # RECHO font 1 (~16 px rows), font 0 (~12 px), font 1 at 2x


def icon(name):
    m = re.search(r"static const char \*const " + name + r"\[(\d+)\] = \{(.*?)\};", SRC, re.S)
    return re.findall(r'"([.#]+)"', m.group(2))


ICONS = [icon(n) for n in ("ICON_FILES", "ICON_USB", "ICON_PLAY", "ICON_BT", "ICON_SETTINGS")]
TABS = ["Files", "USB disk", "Playing", "Bluetooth", "Settings"]


def text(d, x, y_mid, s, f, colour):
    """RECHO's text_at(): x is the left edge, y_mid the vertical centre."""
    top = y_mid - (f.size + 2) // 2
    d.text((x, top), s, fill=colour, font=f)
    return int(d.textlength(s, font=f))


def menu(d, ox, oy, battery):
    """menu_draw() into a 320 x 170 area at (ox, oy); the Bluetooth tab selected."""
    cur = 3
    text(d, ox + 8, oy + 10, "21:47", F1, WHITE)
    text(d, ox + 56, oy + 10, "Fri 13 Nov", F1, SOFT)
    right = "BT Ear (2)" + ("   88%" if battery else "")
    w = int(d.textlength(right, font=F1))
    text(d, ox + 320 - 8 - w, oy + 10, right, F1, GREEN)
    d.line((ox, oy + 21, ox + 319, oy + 21), fill=RULE)
    text(d, ox + 16, oy + 28 + 15, "Bluetooth", F2, WHITE)          # 2x title, top at y 28
    text(d, ox + 16, oy + 72, "Connected to Ear (2)", F1, GREEN)
    text(d, ox + 16, oy + 91, "Search and connect headphones", F1, GREY)
    d.line((ox, oy + 104, ox + 319, oy + 104), fill=RULE)
    for i, rows in enumerate(ICONS):
        x = ox + i * 64
        c = WHITE if i == cur else TAB_DIM
        for r, row in enumerate(rows):
            for col, ch in enumerate(row):
                if ch == "#":
                    d.rectangle((x + 20 + col * 2, oy + 114 + r * 2, x + 21 + col * 2, oy + 115 + r * 2), fill=c)
        f = F1 if d.textlength(TABS[i], font=F1) <= 62 else F0
        w = int(d.textlength(TABS[i], font=f))
        text(d, x + (64 - w) // 2, oy + 152, TABS[i], f, c)
        if i == cur:
            d.rectangle((x + 12, oy + 165, x + 51, oy + 166), fill=WHITE)


def echo_extras(d):
    """Up Next (x 320..479, full height) and the player strip (y 170..221) on the Echo."""
    x0 = 320
    d.line((x0, 0, x0, 221), fill=GREY)
    text(d, x0 + 8, 10, "Up Next", F1, WHITE)
    w = int(d.textlength("88%", font=F1))
    text(d, 480 - 8 - w, 10, "88%", F1, WHITE)
    d.line((x0, 21, 479, 21), fill=RULE)
    songs = ["Graveyard", "So Good", "Control", "The End", "clementine", "Ashley",
             "You should be sad", "Lonely is the Muse", "Panic Attack", "THE DEVIL", "Wind"]
    for i, s in enumerate(songs):
        text(d, x0 + 8, 22 + i * 18 + 9, s, F1, NEXT_FIRST if i == 0 else NEXT_DIM)
    d.line((8, 170, 311, 170), fill=RULE)
    lyric = "Test line 3 (0:10)"
    w = int(d.textlength(lyric, font=F1))
    text(d, (320 - w) // 2, 170 + 13, lyric, F1, WHITE)
    text(d, 10, 196 + 13, "0:12", F0, WHITE)
    w = int(d.textlength("3:18", font=F0))
    text(d, 320 - 10 - w, 196 + 13, "3:18", F0, WHITE)
    d.rectangle((50, 196 + 11, 269, 196 + 15), fill=RULE)
    d.rectangle((50, 196 + 11, 64, 196 + 15), fill=WHITE)


def template(size, zones, path):
    im = Image.new("RGB", size, (0, 0, 0))
    d = ImageDraw.Draw(im)
    for (x0, y0, x1, y1), label in zones:
        d.rectangle((x0, y0, x1, y1), outline=(90, 90, 90))
        d.text((x0 + 3, y0 + 2), label, fill=(120, 120, 120), font=F0)
    im.save(path)


def save(im, base):
    im.save(base + ".png")
    im.resize((im.width * 4, im.height * 4), Image.NEAREST).save(base + "-x4.png")


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "docs", "design")
    os.makedirs(out, exist_ok=True)

    mini = Image.new("RGB", (320, 170), (0, 0, 0))
    menu(ImageDraw.Draw(mini), 0, 0, battery=True)
    save(mini, os.path.join(out, "menu-mini-320x170"))
    template((320, 170), [((0, 0, 319, 21), "top bar 0-21"), ((0, 22, 319, 103), "title 28-58, line 72, tip 91"),
                          ((0, 104, 319, 169), "5 tabs x 64 px: icon 24 px at y 114, label y 152, bar y 165")],
             os.path.join(out, "template-mini-320x170.png"))

    echo = Image.new("RGB", (480, 222), (0, 0, 0))
    d = ImageDraw.Draw(echo)
    menu(d, 0, 0, battery=False)
    echo_extras(d)
    save(echo, os.path.join(out, "menu-echo-480x222"))
    template((480, 222), [((0, 0, 319, 21), "top bar"), ((0, 22, 319, 103), "title / line / tip"),
                          ((0, 104, 319, 169), "tabs"), ((320, 0, 479, 221), "Up Next 160 px: header 22, 11 x 18"),
                          ((0, 170, 319, 195), "strip row 1: lyric / song"), ((0, 196, 319, 221), "strip row 2: progress")],
             os.path.join(out, "template-echo-480x222.png"))
    print("written to", out)


if __name__ == "__main__":
    main()
