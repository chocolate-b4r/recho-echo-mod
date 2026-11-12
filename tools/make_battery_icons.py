"""Generate the 19x7 four-cell battery set (U+E007..U+E00C) from the user's design.

Cells are 4x7 pills with 1 px gaps: lit = solid pill, unlit = hollow pill.
  py -3 -I make_battery_icons.py OUTDIR
"""
import os
import sys

from PIL import Image

W, H, CELL, GAP = 19, 7, 4, 1

SOLID = [".##.", "####", "####", "####", "####", "####", ".##."]
HOLLOW = [".##.", "#..#", "#..#", "#..#", "#..#", "#..#", ".##."]
# Charging: solid end cells with a lightning bolt between them.
CHARGING = [
    ".##.......##....##.",
    "####.....##....####",
    "####....##.....####",
    "####...#####...####",
    "####.....##....####",
    "####....##.....####",
    ".##....##.......##.",
]


def cells(lit):
    rows = []
    for y in range(H):
        line = ""
        for c in range(4):
            line += (SOLID if c < lit else HOLLOW)[y][:CELL]
            if c < 3:
                line += "."
        rows.append(line)
    return rows


def charging():
    return CHARGING


def save(rows, path):
    img = Image.new("L", (W, H), 0)
    for y, r in enumerate(rows):
        for x, ch in enumerate(r):
            if ch == "#":
                img.putpixel((x, y), 255)
    img.save(path)


def main(out):
    os.makedirs(out, exist_ok=True)
    for lit in range(5):
        save(cells(lit), os.path.join(out, f"icon_{7 + lit:02d}_UE00{7 + lit:X}.png"))
    save(charging(), os.path.join(out, "icon_12_UE00C.png"))
    print(f"wrote 6 battery icons to {out}")


if __name__ == "__main__":
    main(sys.argv[1])
