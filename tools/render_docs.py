#!/usr/bin/env python3
"""Render the pictures used by the documentation into docs/img/.

    py -3 tools/render_docs.py

  controls.png          button cheat sheet (both players)
  now-playing-mini.png  Echo Mini Now Playing, 320 x 170, zones from patches/ui_np.c
  menu-mini.png         Echo Mini main menu (pixel-art tabs read from patches/ui_np.c)
  files-mini.png        Files: Internal, SD card, Favourites, Artists; time | date in the header
  artists-mini.png      Files -> Artists (Echo Mini)
  favourites-mini.png   Files -> Favourites: pick the song to start with (Echo Mini)
  now-playing-echo.png  Echo Now Playing, 480 x 222 panel: picture, player strip, Up Next

Layouts use the coordinates in the C sources. RECHO's two bitmap fonts live in the firmware, so
the screen pictures draw text with Segoe UI at the same sizes. Sample track data is made up.
"""
import os
import re

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "docs", "img")
FONTS = "C:/Windows/Fonts"
S = 3  # supersampling for the screen pictures


def rgb565(v):
    r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
    return (r * 255 // 31, g * 255 // 63, b * 255 // 31)


WHITE, SOFT, GREY, DIM, RULE = (rgb565(v) for v in (0xF79E, 0xBDD7, 0x8410, 0x52AA, 0x4208))
NEXT1, NEXTN, GREEN = rgb565(0xE71C), rgb565(0x6B4D), rgb565(0x47E8)


def font(name, px):
    return ImageFont.truetype(os.path.join(FONTS, name), px)


# ---------------------------------------------------------------- screen pictures (3x) ----
class Screen:
    def __init__(self, w, h):
        self.im = Image.new("RGB", (w * S, h * S), (0, 0, 0))
        self.d = ImageDraw.Draw(self.im)
        self.f = {0: font("segoeui.ttf", 11 * S), 1: font("segoeui.ttf", 14 * S),
                  2: font("segoeui.ttf", 26 * S), 3: font("segoeui.ttf", 21 * S),
                  "sym": font("seguisym.ttf", 13 * S)}

    def text(self, f, x, y_mid, s, c, anchor="lm", maxw=None):
        if maxw:                                  # clip like RECHO's draw_text: whole glyphs only
            while s and self.width(f, s) > maxw:
                s = s[:-1]
        self.d.text((x * S, y_mid * S), s, font=self.f[f], fill=c, anchor=anchor)

    def width(self, f, s):
        return self.d.textlength(s, font=self.f[f]) / S

    def rect(self, x0, y0, x1, y1, c):
        self.d.rectangle((x0 * S, y0 * S, (x1 + 1) * S - 1, (y1 + 1) * S - 1), fill=c)

    def save(self, path, scale=2):
        w, h = self.im.size
        self.im.resize((w * scale // S, h * scale // S), Image.LANCZOS).save(path)


def top_bar(sc, x0, right, right_c=SOFT, last=None):
    sc.text(1, x0 + 8, 10, "21:47", WHITE)
    w = sc.width(1, "21:47")
    sc.rect(x0 + 8 + w + 5, 4, x0 + 8 + w + 5, 16, GREY)
    sc.text(1, x0 + 8 + w + 11, 10, "Fri 13 Nov", SOFT)
    xr = x0 + 320 - 8
    if last:
        sc.text(1, xr, 10, last, WHITE, "rm")
        xr -= sc.width(1, last) + 8
    sc.text(1, xr, 10, right, right_c, "rm")


def progress(sc, x0, y, frac=0.38):
    sc.text(0, x0 + 10, y, "1:26", WHITE)
    sc.text(0, x0 + 310, y, "3:47", WHITE, "rm")
    b0, b1 = x0 + 50, x0 + 269
    sc.rect(b0, y - 2, b1, y + 2, RULE)
    sc.rect(b0, y - 2, b0 + int((b1 - b0) * frac), y + 2, WHITE)


QUEUE = ["Next Artist - Opening Track", "Next Artist - Second Song", "Another Band - Long Song Name",
         "Another Band - Interlude", "Someone - Ballad", "Someone - Closing Time", "Duo - Night Drive",
         "Duo - Morning", "Trio - Encore", "Trio - Outro", "Solo - Last One"]


DOT = "\u00b7"


def top_bar_mini(sc, pre, right, last="87%", right_c=SOFT):
    """time | date left; pre (grey), right, last (battery) on the right, as top_bar_3"""
    top_bar(sc, 0, right, right_c, last=last)
    if pre:
        xr = 320 - 8 - sc.width(1, last) - 6 - sc.width(1, right) - 10
        sc.text(1, xr, 10, pre, GREY, "rm")
    sc.rect(0, 21, 319, 21, RULE)


def heart(sc, x, y):
    rows = [".##...##.", "####.####", "#########", "#########", ".#######.", "..#####..", "...###...", "....#...."]
    for ry, row in enumerate(rows):
        for rx, ch in enumerate(row):
            if ch == "#":
                sc.rect(x + rx, y + ry, x + rx, y + ry, (248, 0, 0))


def now_playing_mini():
    sc = Screen(320, 170)
    top_bar_mini(sc, "9/1000", "62dB" + DOT + "3.5mm")
    cx = 8 + 146 // 2
    big = 3                                       # the title at 1.5x, wrapped, centred
    l1, l2, artist = "For the First", "Time", "Mac DeMarco"
    w1 = sc.width(big, l1)
    sc.text(big, cx, 40, l1, WHITE, "mm")
    heart(sc, cx - int(w1 / 2) - 14, 36)          # in the margin: the title does not move
    w2, wa = sc.width(big, l2), sc.width(1, artist)
    x0 = cx - int((w2 + 10 + wa) / 2)
    sc.text(big, x0, 64, l2, WHITE, "lm")
    sc.text(1, x0 + w2 + 10, 67, artist, SOFT, "lm")
    sc.text(1, cx, 87, "FLAC 44.1kHz" + DOT + "24bit", GREY, "mm")
    sc.text(1, cx, 108, "the line being sung,", WHITE, "mm")
    sc.text(1, cx, 126, "wrapped onto two lines", WHITE, "mm")
    sc.rect(160, 28, 160, 143, RULE)
    for i in range(9):
        sc.text(0, 168, 33 + 13 * i, QUEUE[i], NEXT1 if i == 0 else NEXTN, maxw=144)
    progress(sc, 0, 158)
    sc.save(os.path.join(OUT, "now-playing-mini.png"))


def c_bitmap(name):
    src = open(os.path.join(ROOT, "patches", "ui_np.c"), encoding="utf-8").read()
    m = re.search(r"static const char \*const " + name + r"\[\d+\] = \{(.*?)\};", src, re.S)
    return re.findall(r'"([.#]+)"', m.group(1))


def bitmap(sc, rows, x0, y0, c):
    for ry, row in enumerate(rows):
        for rx, ch in enumerate(row):
            if ch == "#":
                sc.rect(x0 + rx, y0 + ry, x0 + rx, y0 + ry, c)


def menu_mini():
    sc = Screen(320, 170)
    top_bar_mini(sc, "", "62dB" + DOT + "3.5mm")
    sc.text("sym", 320 - 8 - sc.width(1, "87%") - 6 - sc.width(1, "62dB" + DOT + "3.5mm") - 6, 10, "\u21c4", SOFT, "rm")
    big = font("segoeui.ttf", 28 * S)
    sc.f["big"] = big
    sc.text("big", 16, 44, "Now playing", WHITE)
    sc.text(0, 16, 70, "Mac DeMarco - For the First Time", SOFT)
    sc.text(0, 16, 87, "Playing", GREY)
    tabs = [("TAB_FOLDER", -8, -7), ("TAB_SYS", -10, -4), ("TAB_DISC", -8, -8), ("TAB_BT", -6, -8),
            ("TAB_WRENCH", -8, -7)]
    for i, (name, dx, dy) in enumerate(tabs):
        cx, cy = i * 64 + 32, 125
        on = i == 2
        bitmap(sc, c_bitmap(name), cx + dx, cy + dy, WHITE if on else GREY)
        if on:
            sc.text(1, cx - 24, cy, "<", WHITE)
            sc.text(1, cx + 14, cy, ">", WHITE)
    progress(sc, 0, 158)
    sc.save(os.path.join(OUT, "menu-mini.png"))


def header(sc, title):
    """RECHO's screen header with the mod's time | date where its icon was"""
    sc.text(1, 8, 10, "21:47", WHITE)
    w = sc.width(1, "21:47")
    sc.rect(8 + w + 5, 4, 8 + w + 5, 16, GREY)
    sc.text(1, 8 + w + 11, 10, "Fri 13 Nov", SOFT)
    x = 8 + w + 11 + sc.width(1, "Fri 13 Nov") + 10
    sc.text(1, x, 10, title, WHITE)
    sc.text(1, 312, 10, "87%", WHITE, "rm")
    sc.text(1, 312 - sc.width(1, "87%") - 8, 10, "62 dB", WHITE, "rm")


def list_rows(sc, rows, sel, chevron=False):
    for i, label in enumerate(rows):
        y = 22 + 20 * i
        fg = WHITE
        if i == sel:
            sc.rect(8, y, 311, y + 19, rgb565(0x24DF))
            fg = (0, 0, 0)
        sc.text(1, 12, y + 10, label, fg)
        if chevron:
            sc.text(1, 308, y + 10, "\u203a", fg, "rm")


def files_mini():
    sc = Screen(320, 170)
    header(sc, "Files")
    list_rows(sc, ["Internal", "SD card", "Favourites", "Artists"], 3, chevron=True)
    sc.save(os.path.join(OUT, "files-mini.png"))


def artists_mini():
    sc = Screen(320, 170)
    header(sc, "Files")
    list_rows(sc, ["Halsey", "Joji", "Lamp", "Mac DeMarco", "Mitski", "Palace", "Tame Impala"], 3)
    sc.save(os.path.join(OUT, "artists-mini.png"))


def favourites_mini():
    sc = Screen(320, 170)
    header(sc, "Files")
    list_rows(sc, ["Flora Cash - What Is Wrong", "Mac DeMarco - Heart To Heart", "Palace - Bitter",
                   "Tame Impala - New Person, Same Old Mistakes", "Tinariwen - Sagherat Assani",
                   "Mitski - I Will", "Joji - Strange Home"], 1)
    sc.save(os.path.join(OUT, "favourites-mini.png"))


def now_playing_echo():
    sc = Screen(480, 222)
    top_bar(sc, 0, "62 dB \u00b7 3.5 mm")
    sc.text(1, 160, 34, "Artist Name", SOFT, "mm")
    sc.text(2, 160, 58, "Song Title", WHITE, "mm")
    sc.text(1, 160, 82, "Album Name", GREY, "mm")
    sc.text(1, 160, 102, "FLAC  44.1 kHz \u00b7 24 bit \u00b7 1682 kbps \u00b7 9/10", GREY, "mm")
    sc.rect(60, 118, 259, 118, RULE)
    sc.text(3, 160, 133, "the line being sung, wrapped", WHITE, "mm")
    sc.text(3, 160, 157, "onto a second row", WHITE, "mm")
    # player strip: line above the progress bar on Now Playing
    sc.rect(8, 196, 311, 196, RULE)
    progress(sc, 0, 209)
    # Up Next column
    sc.rect(320, 0, 320, 221, GREY)
    sc.text(1, 328, 10, "Up Next", WHITE)
    sc.text("sym", 328 + sc.width(1, "Up Next") + 6, 10, "\u21c4", SOFT)
    sc.text(1, 472, 10, "87%", WHITE, "rm")
    sc.rect(320, 21, 479, 21, RULE)
    for i in range(11):
        sc.text(1, 328, 22 + 18 * i + 9, QUEUE[i], NEXT1 if i == 0 else NEXTN, maxw=146)
    sc.save(os.path.join(OUT, "now-playing-echo.png"))


# ---------------------------------------------------------------- controls cheat sheet ----
BG, CARD, EDGE, INK, MUTED, ACCENT = (17, 18, 22), (28, 30, 36), (52, 56, 66), (236, 238, 242), (150, 156, 168), (122, 162, 255)
KEYS = {"play": "\u25b6\u2016", "menu": "\u2261", "back": "\u21a9", "vol+": "+", "vol-": "\u2212",
        "power": "\u23fb"}

SECTIONS = [
    ("Now Playing", [
        (["play"], "tap", "play / pause"), (["play"], "hold", "next track"),
        (["back"], "tap", "back to the main menu"), (["back"], "hold", "previous track"),
        (["menu"], "tap", "Files"), (["menu"], "hold", "favourite: add or take off"),
        (["vol+", "vol-"], "tap / hold", "volume (shown in the top bar)")]),
    ("Main menu", [
        (["vol+", "vol-"], "tap", "move between tabs"), (["play"], "tap", "open the tab"),
        (["play"], "hold on Files", "play every song on the player"),
        (["menu"], "tap / hold", "Files, from any screen (never selects)")]),
    ("Lists (Files, Settings)", [
        (["vol+", "vol-"], "tap", "move"), (["play"], "tap", "select / play the folder"),
        (["play"], "hold on a folder", "play the folder and all folders in it"),
        (["play"], "on Favourites", "play your favourites"),
        (["back"], "tap", "back")]),
    ("Screen off", [
        (["play"], "tap", "play / pause"), (["play"], "hold", "next track"),
        (["back"], "hold", "previous track"), (["power"], "tap", "screen on")]),
    ("Power and cable", [
        (["power"], "tap", "screen on / off"), (["power"], "hold", "power off"),
        (["back"], "tap in USB disk", "leave USB disk (screen stays on)"),
        (["vol+", "vol-"], "cable chooser", "pick Charge only or USB disk"),
        ([], "no key for 5 s", "chooser: off into the charging screen")]),
    ("Echo only", [
        (["menu"], "hold 10 s", "maskrom (USB recovery mode)")]),
]


def keycap(d, x, y, label, f):
    w = max(34, int(d.textlength(label, font=f)) + 18)
    d.rounded_rectangle((x, y + 3, x + w, y + 33), 8, fill=(10, 11, 14))
    d.rounded_rectangle((x, y, x + w, y + 30), 8, fill=(58, 63, 75), outline=(92, 99, 116), width=2)
    d.text((x + w / 2, y + 15), label, font=f, fill=INK, anchor="mm")
    return w


def controls():
    W, col_w, pad = 1300, 620, 30
    h_title, f_key = font("segoeuib.ttf", 30), font("seguisym.ttf", 17)
    f_sec, f_txt, f_when = font("seguisb.ttf", 21), font("segoeui.ttf", 18), font("segoeuii.ttf", 15)
    row_h = 44

    def card_h(rows):
        return 58 + row_h * len(rows) + 14

    cols = [[], []]
    heights = [0, 0]
    for sec in SECTIONS:
        i = 0 if heights[0] <= heights[1] else 1
        cols[i].append(sec)
        heights[i] += card_h(sec[1]) + 22
    H = 110 + max(heights) + 30
    im = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(im)
    d.text((pad, 34), "Controls", font=h_title, fill=INK)
    d.text((pad + 150, 42), "RECHO mod \u00b7 Echo Mini and Echo \u00b7 + and \u2212 are the volume keys",
           font=f_txt, fill=MUTED)
    for ci, col in enumerate(cols):
        x0, y = pad + ci * (col_w + 20), 100
        for title, rows in col:
            ch = card_h(rows)
            d.rounded_rectangle((x0, y, x0 + col_w, y + ch), 14, fill=CARD, outline=EDGE, width=2)
            d.rounded_rectangle((x0, y, x0 + 6, y + ch), 3, fill=ACCENT)
            d.text((x0 + 22, y + 28), title, font=f_sec, fill=INK, anchor="lm")
            ry = y + 56
            for keys, when, what in rows:
                kx = x0 + 22
                for k in keys:
                    kx += keycap(d, kx, ry, KEYS[k], f_key) + 6
                d.text((x0 + 130, ry + 15), when, font=f_when, fill=MUTED, anchor="lm")
                d.text((x0 + 270, ry + 15), what, font=f_txt, fill=INK, anchor="lm")
                ry += row_h
            y += ch + 22
    im.save(os.path.join(OUT, "controls.png"))


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    controls()
    now_playing_mini()
    now_playing_echo()
    menu_mini()
    files_mini()
    artists_mini()
    favourites_mini()
    print("docs/img: controls.png now-playing-mini.png now-playing-echo.png menu-mini.png files-mini.png"
          " artists-mini.png favourites-mini.png")
