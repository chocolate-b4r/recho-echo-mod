"""Apply the RECHO 0.10.1 (2d11bdcd) tweaks to a copy of the image.

  py -3 -I patch_recho.py IN.IMG OUT.IMG [--drop-rows 2,5] [--no-charge-screen] [--icons DIR]

Every patch first checks the exact original bytes, so it refuses to run on any
other build. The output is re-sealed (module CRC, header self-copy, image CRC).
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rkimg import crc32, u32, seal, info  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RAM_OF_IMG = 0x0304F298           # fw1: RAM = 0x0304F298 + IMG offset
MOD_BASE = 0x0307A000              # every SFMD module loads here (header included)

EXPECT_BRAND, EXPECT_TAG = b"RECHO", b"2d11bdcd"

# ---- module m47 (main menu) -------------------------------------------------
M47_IMG = 0x2B224
M47_LEN = 0xBB4


def m47(addr):
    return M47_IMG + (addr - MOD_BASE)


def thumb_bl(src, dst):
    off = dst - (src + 4)
    if not -(1 << 24) <= off < (1 << 24):
        raise ValueError("bl out of range")
    s = (off >> 24) & 1
    i1, i2 = (off >> 23) & 1, (off >> 22) & 1
    j1, j2 = (~i1 ^ s) & 1, (~i2 ^ s) & 1
    imm10, imm11 = (off >> 12) & 0x3FF, (off >> 1) & 0x7FF
    hw1 = 0xF000 | (s << 10) | imm10
    hw2 = 0xD000 | (j1 << 13) | (j2 << 11) | imm11
    return struct.pack("<HH", hw1, hw2)


def put(d, off, old, new, what):
    cur = bytes(d[off:off + len(old)])
    if cur != old:
        raise SystemExit(f"{what}: unexpected bytes at IMG {off:#x}: {cur.hex()} (want {old.hex()})")
    d[off:off + len(new)] = new


# Original 9 rows: 0 Files, 1 Now playing, 2 Spectrum, 3 USB disk, 4 Settings,
# 5 Supporters, 6 Debug, 7 Power off, 8 About.
ORIG_ROWS = 9
ROW_USB, ROW_POWER = 3, 7
TBB = bytes([0x05, 0x09, 0x0B, 0x0D, 0x1E, 0x20, 0x28, 0x2A, 0x2C])


def cmp_imm(reg, imm):
    return bytes([imm, 0x28 | reg])


NO_ROW = 15                        # a row number no menu has (row checks that should never match)


def remove_menu_rows(d, keep):
    """Rebuild the m47 main menu with the original rows `keep`, in that order."""
    keep = list(keep)
    n = len(keep)
    if ROW_USB not in keep or n < 2:
        raise SystemExit("the USB disk row must stay")
    # Power off may go (the Power key does the same); its row checks then match no row.
    new_usb = keep.index(ROW_USB)
    new_power = keep.index(ROW_POWER) if ROW_POWER in keep else NO_ROW
    labels = struct.unpack_from("<9I", d, m47(0x0307AA64))
    if labels[2] != 0x0307A8C2 or labels[5] != 0x0307A78B:
        raise SystemExit("menu label table is not the expected one")
    for base in (0x0307AA40, 0x0307AA64):
        o = m47(base)
        ptrs = struct.unpack_from("<9I", d, o)
        rows = [ptrs[r] for r in keep]
        struct.pack_into("<9I", d, o, *(rows + [rows[-1]] * (ORIG_ROWS - n)))
    tbb = bytes(TBB[r] for r in keep)
    put(d, m47(0x0307A1A4), TBB, tbb + tbb[-1:] * (ORIG_ROWS - n), "menu tbb")
    put(d, m47(0x0307A19C), cmp_imm(3, 8), cmp_imm(3, n - 1), "dispatch bound (cmp r3,#8)")
    put(d, m47(0x0307A67C), b"	#", bytes([n, 0x23]), "row count (movs r3,#9)")
    put(d, m47(0x0307A04C), cmp_imm(0, ROW_USB), cmp_imm(0, new_usb), "draw: USB row (cmp r0,#3)")
    put(d, m47(0x0307A0E6), cmp_imm(6, ROW_POWER), cmp_imm(6, new_power), "draw: Power row (cmp r6,#7)")
    put(d, m47(0x0307A6DA), cmp_imm(3, ROW_USB), cmp_imm(3, new_usb), "hint: USB row (cmp r3,#3)")
    put(d, m47(0x0307A6E4), cmp_imm(1, ROW_POWER), cmp_imm(1, new_power), "hint: Power row (cmp r1,#7)")
    reseal_module(d, M47_IMG)
    return keep


def reseal_module(d, off):
    if d[off:off + 4] != b"SFMD":
        raise SystemExit(f"no SFMD module at {off:#x}")
    length = u32(d, off + 12)
    body = bytearray(d[off:off + length])
    body[24:28] = b"\0\0\0\0"
    struct.pack_into("<I", d, off + 24, crc32(body))


# ---- fw1: off-state charging screen ----------------------------------------
HOOK_SITE = 0x0305A678             # `bl 0x3051e28` (power rail cut) in power-off routine
HOOK_ORIG = 0x03051E28
CAVE_ADDR = 0x0306E000             # zero gap between fw1 .text end and .data image
CAVE_LIMIT = 0x03074B70


STATUS_LEVEL_SITE = 0x0305945E    # `ldr r2,[r3,#8]; ldr.w r8,[r3,#20]` in the status-bar draw
POWER_TICK_SLOT = 0x0306AC68      # POWER service descriptor: tick function pointer
POWER_TICK_ORIG = 0x0306A7C5


def load_hooks(name="hooks"):
    # Refuse a stale build: if build_hooks.sh failed, the old .bin is still there and would be
    # patched in silently (this happened once, caught only because the build error was printed).
    bin_path = os.path.join(ROOT, "build", name + ".bin")
    pdir = os.path.join(ROOT, "patches")
    newest = max(os.path.getmtime(os.path.join(pdir, f)) for f in os.listdir(pdir) if f.endswith((".c", ".h")))
    if not os.path.exists(bin_path) or os.path.getmtime(bin_path) < newest:
        sys.exit(f"build/{name}.bin is older than patches/ - run tools/build_hooks.sh"
                 + (" echo" if "echo" in name else "") + " and fix its errors first")
    blob = open(bin_path, "rb").read()
    entries = {}
    for line in open(os.path.join(ROOT, "build", name + ".entries")):
        n, addr = line.split()
        entries[n] = int(addr, 16)
    return blob, entries


def thumb_bw(src, dst):
    """Thumb-2 B.W (T4): same immediate layout as BL, bit 14 of the second halfword clear."""
    hw1, hw2 = struct.unpack("<HH", thumb_bl(src, dst))
    return struct.pack("<HH", hw1, hw2 & ~0x4000)


# ---- full-size Echo port (stage 1) ------------------------------------------
ECHO_PANEL_INIT_CALLS = (0x0306B9F4, 0x0306C206)   # `bl 0x306B000` after sleep-out (boot, wake)
ECHO_WINDOW_ROWS = (0x0306B20C, 0x0306B20E)        # `adds r5,#35` / `adds r4,#35` in the window routine
ECHO_ROW_OFFSET = 49                               # RECHO at the top of the 222 visible rows; player strip below
GPIO_SET = 0x03051590                              # `push {r4-r6,lr}; mov r5,r1` -> b.w gpio_set_echo
ECHO_READ_SITES = (0x03055EE8, 0x03055F6A)         # bl 0x3051610 reading GPIO2.8 / GPIO2.0
REPAIR_CALL = 0x03050FBE                           # bl 0x305444C (FW1-from-FW2 repair)
UPDATE_FIND_CALL = 0x0305090C                      # bl 0x3054094 (update image search)
# RECHO's own updater works on the Echo: it takes the firmware area from the eMMC ID block
# (Echo boot log: P 0x28000, slot 2 at P/2 = 0x14000 = the R the loader reports, user volume
# at stock's place 0x8E000, both slots valid). Only the file names change, so a Mini image
# (ECHOMINI.IMG / HIFIECnn.IMG) is never picked up on the Echo, nor an Echo image on the Mini.
UPDATE_NAME_FIXED = 0x0305F3BF                     # 11-char FAT name "ECHOMINIIMG"
UPDATE_NAME_PREFIX = 0x0305F3CB                    # 6-char prefix "HIFIEC" (+ 2 digits + IMG)
ECHO_UPDATE_FIXED = b"RECHOECHIMG"                 # -> RECHOECH.IMG
ECHO_UPDATE_PREFIX = b"RECHOE"                     # -> RECHOEnn.IMG


# Layout (stage 4): RECHO's button bar ("HINTS" layer) moves to the bottom of the panel,
# screens keep rows 144..169 for content; Now Playing and cable chooser are drawn by patches/ui_np.c.
CONTENT_BOTTOM_SITES = (0x0305C4B8, 0x0305C4EE)    # `movs r2,#143` (content ends above the bar)
HINTS_DRAW_SLOT = 0x03060BBC                       # HINTS layer draw pointer (0x0305AEED)
M45_IMG = 0x2A494                                  # module m45 (Now Playing), runs at MOD_BASE
NP_DRAW = 0x0307A1F0                               # m45 draw entry `push {r4-r11,lr}` -> b.w np_draw
USBSEL_DRAW_SLOT = 0x03060AD4                      # USBSEL screen draw pointer (0x03059EDD)
NP_BACK_TAP = 0x0307A12C                           # m45: back-key tap = play-mode cycle -> Back (main menu)
NP_BACK_RESUME = 0x0307A170                        # m45: common tail after the key cases
NP_LABEL_MODE = 0x0307A1E8                         # m45 literal -> "MODE"  (back key label)
NP_LABEL_MENU = 0x0307A1D8                         # m45 literal -> "menu"  (menu key hold label)
HOLD_MENU_CALL = 0x0305BF18                        # fw1: hold menu -> bl 0x305BA3C (main menu)


BATTERY_GLYPH_DRAW = 0x030594A4                    # status bar: bl draw_text (battery glyph)
VOLUME_X_BASE = 0x030594A8                         # status bar: rsb r4, r4, #304 (left of the glyph)


def ui_layout(d, e):
    """Both players: no button bar (content to row 169), new Now Playing and cable chooser."""
    for site in CONTENT_BOTTOM_SITES:
        put(d, site - RAM_OF_IMG, bytes([143, 0x22]), bytes([169, 0x22]), "content area to row 169")
    put(d, HINTS_DRAW_SLOT - RAM_OF_IMG, struct.pack("<I", 0x0305AEED), struct.pack("<I", e["hints_stub"] | 1),
        "HINTS layer draw -> hints_stub")
    o = M45_IMG + NP_DRAW - MOD_BASE
    put(d, o, bytes.fromhex("2de9f04f"), thumb_bw(NP_DRAW, e["np_draw"]), "m45 draw -> b.w np_draw")
    reseal_module(d, M45_IMG)
    put(d, USBSEL_DRAW_SLOT - RAM_OF_IMG, struct.pack("<I", 0x03059EDD), struct.pack("<I", e["usb_draw"] | 1),
        "USBSEL draw -> usb_draw")


def echo_status_bar(d):
    """Echo: the battery % in Up Next replaces the battery glyph in RECHO's own top bars."""
    put(d, BATTERY_GLYPH_DRAW - RAM_OF_IMG, thumb_bl(BATTERY_GLYPH_DRAW, 0x03058D84), bytes.fromhex("00bf00bf"),
        "status bar battery glyph -> nop")
    put(d, VOLUME_X_BASE - RAM_OF_IMG, bytes.fromhex("c4f59874"), bytes.fromhex("4ff49c74"),
        "status bar volume x base: rsb r4,r4,#304 -> mov.w r4,#312")



HEADPHONE_GLYPH = 0x03059504   # menu bar: `cmp r5,#1; bls 0x30595a2` -> headphone / no-headphone glyph
TIMECODE_BRANCH = 0x03059B3A   # menu header: `bhi.n 0x3059b8a` skips "▶ m:ss" unless playing / paused
BATTERY_GLYPH_LOAD = 0x03059474  # menu bar: `ldr.w r5,[r3,r2,lsl #2]` = battery glyph string by level


VOL_TEXT_BAR = 0x030594D4    # menu bar: bl 0x305C09E (snprintf "%d dB", volume)
VOL_TEXT_MSG = 0x0305A4B0    # volume message: b.w 0x305C09E ("%d dB")


BOOT_SPLASH_CALL = 0x03050D7C   # bl 0x306C3E4("RECHO 0.10.1", ...): the boot splash


def boot_splash(d, e):
    """Both players: terminal-style boot screen (patches/ui_np.c boot_screen)."""
    put(d, BOOT_SPLASH_CALL - RAM_OF_IMG, thumb_bl(BOOT_SPLASH_CALL, 0x0306C3E4),
        thumb_bl(BOOT_SPLASH_CALL, e["boot_screen"]), "boot splash -> boot_screen")


USB_KEY_CALL = 0x0305062C   # USB-disk loop callback: `bl power_key_down` (armed) -> leave USB disk


USB_TEXT = 0x0305E877         # "eject on the computer, or press power" on the USB disk screen
M44_IMG = 0x28ED4             # module m44 (Files)
FILES_KEY_PTR = 0x0307B5A8    # m44 BROWSE descriptor +28: key handler (0x307AE11)
FILES_ENTER_PTR = 0x0307B5A4  # m44 BROWSE descriptor +24: open (0x307A899)
FILES_DRAW_PTR = 0x0307B5B0   # m44 BROWSE descriptor +36: draw (0x307A911)


def usb_back_key(d, e):
    """Both players: Back leaves USB disk, after the key is released (patches/keys.c)."""
    put(d, USB_KEY_CALL - RAM_OF_IMG, thumb_bl(USB_KEY_CALL, 0x03051C70),
        thumb_bl(USB_KEY_CALL, e["usb_key_exit"]), "USB disk key -> usb_key_exit")
    put(d, USB_TEXT - RAM_OF_IMG, b"eject on the computer, or press power\x00",
        b"eject on the computer, or press back\x00\x00", "USB disk text: press back")


def files_favourites(d, e):
    """Both players: Files' drive list gets a Favourites row that plays FAVORITE.M3U (patches/keys.c)."""
    o = M44_IMG + FILES_KEY_PTR - 0x0307A000
    put(d, o, struct.pack("<I", 0x0307AE11), struct.pack("<I", e["files_key"] | 1), "Files key -> files_key")
    o = M44_IMG + FILES_ENTER_PTR - 0x0307A000
    put(d, o, struct.pack("<I", 0x0307A899), struct.pack("<I", e["files_enter"] | 1), "Files open -> files_enter")
    o = M44_IMG + FILES_DRAW_PTR - 0x0307A000
    put(d, o, struct.pack("<I", 0x0307A911), struct.pack("<I", e["files_draw"] | 1), "Files draw -> files_draw")
    reseal_module(d, M44_IMG)


AFTER_USB_SCREEN = 0x0305BC8A   # after USB disk: `movs r2,#44` = the screen to open (Files)


def after_usb_main_menu(d):
    """Both players: leaving USB disk opens the main menu (m47) instead of Files (m44)."""
    put(d, AFTER_USB_SCREEN - RAM_OF_IMG, bytes.fromhex("2c22"), bytes.fromhex("2f22"),
        "after USB disk: Files -> main menu")


def volume_text(d, e):
    """Both players: volume shown from 0 dB (RECHO's -90 dB) upward."""
    put(d, VOL_TEXT_BAR - RAM_OF_IMG, thumb_bl(VOL_TEXT_BAR, 0x0305C09E), thumb_bl(VOL_TEXT_BAR, e["vol_text"]),
        "menu bar volume text -> vol_text")
    put(d, VOL_TEXT_MSG - RAM_OF_IMG, thumb_bw(VOL_TEXT_MSG, 0x0305C09E), thumb_bw(VOL_TEXT_MSG, e["vol_text"]),
        "volume message text -> vol_text")


def menu_bar(d, e, echo):
    """Menu bars: no headphone glyph (both); no play time on the Echo (the player strip has it);
    battery as a percentage on the Mini (the Echo's Up Next shows it)."""
    put(d, HEADPHONE_GLYPH - RAM_OF_IMG, bytes.fromhex("012d4cd9"), bytes.fromhex("3c462ae0"),
        "menu bar: headphone glyph skipped (mov r4,r7; b.n 0x305955e)")
    if echo:
        put(d, TIMECODE_BRANCH - RAM_OF_IMG, bytes.fromhex("26d8"), bytes.fromhex("26e0"),
            "menu header: play time skipped (bhi -> b.n 0x3059b8a)")
    else:
        put(d, BATTERY_GLYPH_LOAD - RAM_OF_IMG, bytes.fromhex("53f82250"),
            thumb_bl(BATTERY_GLYPH_LOAD, e["battery_pct_shim"]), "menu bar: battery glyph -> percentage text")


def key_remap(d, e):
    """Both players: back tap on Now Playing -> main menu, hold back = previous (unchanged),
    hold menu on Now Playing -> Settings; bar labels BACK / hold settings (patches/keys.c)."""
    o = M45_IMG + NP_BACK_TAP - MOD_BASE
    b_off = (NP_BACK_RESUME - (NP_BACK_TAP + 4) - 4) >> 1
    put(d, o, bytes.fromhex("174de8790130"),
        thumb_bl(NP_BACK_TAP, 0x0305BA3C) + struct.pack("<H", 0xE000 | (b_off & 0x7FF)), "m45 back tap -> main menu")
    for site, old, new in ((NP_LABEL_MODE, 0x0307AC11, e["str_back"]), (NP_LABEL_MENU, 0x0307AC07, e["str_settings"])):
        put(d, M45_IMG + site - MOD_BASE, struct.pack("<I", old), struct.pack("<I", new), f"m45 label literal {site:#x}")
    reseal_module(d, M45_IMG)
    put(d, HOLD_MENU_CALL - RAM_OF_IMG, thumb_bl(HOLD_MENU_CALL, 0x0305BA3C), thumb_bl(HOLD_MENU_CALL, e["hold_menu"]),
        "hold menu -> hold_menu (Settings on Now Playing)")


def echo_port(d, e):
    for site in ECHO_PANEL_INIT_CALLS:
        put(d, site - RAM_OF_IMG, thumb_bl(site, 0x0306B000), thumb_bl(site, e["echo_panel_init"]),
            "panel init call")
    put(d, ECHO_WINDOW_ROWS[0] - RAM_OF_IMG, bytes([35, 0x35]), bytes([ECHO_ROW_OFFSET, 0x35]), "window rows (r5)")
    put(d, ECHO_WINDOW_ROWS[1] - RAM_OF_IMG, bytes([35, 0x34]), bytes([ECHO_ROW_OFFSET, 0x34]), "window rows (r4)")
    put(d, GPIO_SET - RAM_OF_IMG, bytes.fromhex("70b50d46"), thumb_bw(GPIO_SET, e["gpio_set_echo"]),
        "GPIO set entry")
    for site in ECHO_READ_SITES:
        put(d, site - RAM_OF_IMG, thumb_bl(site, 0x03051610), thumb_bl(site, e["echo_gpio_read"]), "jack read")
    # The FW1-from-FW2 repair stays off (not needed for updates; one less writer).
    put(d, REPAIR_CALL - RAM_OF_IMG, thumb_bl(REPAIR_CALL, 0x0305444C), bytes.fromhex("012000bf"),
        "FW1 repair (-> result 1, normal start)")
    # Updater on, under Echo-only file names.
    put(d, UPDATE_NAME_FIXED - RAM_OF_IMG, b"ECHOMINIIMG", ECHO_UPDATE_FIXED, "update file name (fixed)")
    put(d, UPDATE_NAME_PREFIX - RAM_OF_IMG, b"HIFIEC", ECHO_UPDATE_PREFIX, "update file name (numbered)")


def add_hooks(d, animation=True, echo=False):
    blob, e = load_hooks("hooks-echo" if echo else "hooks")
    if CAVE_ADDR + len(blob) > CAVE_LIMIT:
        raise SystemExit("hooks too large for the cave")
    o = CAVE_ADDR - RAM_OF_IMG
    if any(d[o:o + len(blob) + 64]):
        raise SystemExit("code cave is not empty")
    d[o:o + len(blob)] = blob
    s = HOOK_SITE - RAM_OF_IMG
    put(d, s, thumb_bl(HOOK_SITE, HOOK_ORIG), thumb_bl(HOOK_SITE, e["charge_screen"]), "power-off hook site")
    if animation:
        s = STATUS_LEVEL_SITE - RAM_OF_IMG
        put(d, s, bytes.fromhex("9a68d3f81480"),
            thumb_bl(STATUS_LEVEL_SITE, e["status_level_shim"]) + bytes.fromhex("00bf"), "status-bar level load")
        s = POWER_TICK_SLOT - RAM_OF_IMG
        put(d, s, struct.pack("<I", POWER_TICK_ORIG), struct.pack("<I", e["power_tick_wrap"] | 1),
            "POWER service tick pointer")
    key_remap(d, e)
    menu_bar(d, e, echo)
    volume_text(d, e)
    boot_splash(d, e)
    usb_back_key(d, e)
    files_favourites(d, e)
    after_usb_main_menu(d)
    ui_layout(d, e)
    if not echo:                                   # Bluetooth routing holds GPIO2.0 / GPIO2.14
        put(d, GPIO_SET - RAM_OF_IMG, bytes.fromhex("70b50d46"), thumb_bw(GPIO_SET, e["gpio_set_mini"]),
            "GPIO set entry (Mini)")
    if echo:
        if not animation:
            raise SystemExit("the Echo escape hatch rides on the POWER tick wrapper; keep the animation on")
        echo_port(d, e)
        echo_status_bar(d)



# ---- USB chooser ("USBSEL", fw1): Charge only first, 5 s auto-select, no DAC ----
USBSEL_ICONS, USBSEL_LABELS = 0x03060AE4, 0x03060AF0
USBSEL_COUNT_DATA = (0x25A88, 0x27584)   # list count word (RAM 0x030041B0) in both .data images


def fw(addr):
    return addr - RAM_OF_IMG


def usb_chooser_charge_first(d):
    icons = struct.unpack_from("<3I", d, fw(USBSEL_ICONS))
    labels = struct.unpack_from("<3I", d, fw(USBSEL_LABELS))
    if labels != (0x0305E84D, 0x03060A35, 0x03060A3D):
        raise SystemExit("USB chooser labels are not USB disk / USB DAC / Charge only")
    struct.pack_into("<3I", d, fw(USBSEL_ICONS), icons[2], icons[0], icons[0])
    struct.pack_into("<3I", d, fw(USBSEL_LABELS), labels[2], labels[0], labels[0])
    for o in USBSEL_COUNT_DATA:
        put(d, o, struct.pack("<I", 3), struct.pack("<I", 2), "USB chooser row count (.data)")
    # opener: ignore the usb= setting; always cursor 0 (Charge only) with the 5 s countdown
    put(d, fw(0x0305BA70), bytes.fromhex("db78"), bytes.fromhex("0123"), "chooser opener (ldrb usb mode -> movs r3,#1)")
    # select: row 0 -> leave (charge only), row 1 -> USB disk; DAC branch unreachable
    put(d, fw(0x0305B948), bytes.fromhex("1bb9"), bytes.fromhex("53b1"), "chooser select (cbnz -> cbz to leave)")
    # row draw: 'not yet' note was for row 1 (DAC); no row has it now
    put(d, fw(0x03059D96), bytes.fromhex("012e"), bytes.fromhex("022e"), "chooser row note (cmp r6,#1 -> #2)")
    # countdown expiry (no key pressed, so the cursor is on Charge only): power off instead
    # of returning to the menu. On a cable the power-off ends in the off-charging screen.
    put(d, fw(0x0305B9B0), thumb_bl(0x0305B9B0, 0x0305B934), thumb_bl(0x0305B9B0, 0x0305A618),
        "chooser auto-select -> power off")
    # soft-key hint: no SELECT on row 1 (DAC) -> every row selectable
    put(d, fw(0x03059FC2), bytes.fromhex("012b"), bytes.fromhex("022b"), "chooser hint (cmp r3,#1 -> #2)")


# ---- Settings (module m48): hide Show tags, On USB connect, Debug strip ----
M48_IMG = 0x2BDD8
SETTINGS_ROWS = 17


def m48(addr):
    return M48_IMG + (addr - MOD_BASE)


# Both players: Swap up/down hidden too, and Debug strip (row 12) becomes the Bluetooth row,
# second after Play mode (see settings_bluetooth_row).
# Debug strip's slot (row 12) becomes About, last and without a value.
ECHO_SETTINGS_ORDER = [0, 3, 4, 5, 6, 7, 8, 9, 13, 14, 15, 16, 12, 1, 2, 10, 11]
ECHO_SETTINGS_SHOWN = 13
NO_VALUE_ROWS = {12}
ROW_DEBUG_STRIP = 12
BT_SELECT_SITE = 0x0307A804   # select case of Debug strip: ldr r2,=settings; ldrb r3,[r2,#4]; eor ...
BT_VALUE_SITE = 0x0307A3FE    # value case of Debug strip: ldr r3,=settings; ldrb r1,[r3,#4]; b.n 0x307a3ec


def settings_bluetooth_row(d, echo=False):
    """Debug strip -> Bluetooth: label, On/Off from the module state, select opens the screen."""
    _, e = load_hooks("hooks-echo" if echo else "hooks")
    idx = ECHO_SETTINGS_ORDER.index(ROW_DEBUG_STRIP)
    lab = m48(0x0307AF08) + 4 * idx
    put(d, lab, struct.pack("<I", 0x0307AE94), struct.pack("<I", e["str_bluetooth"]), "Settings Bluetooth label")
    # select: bl bt_request_open ; b.n 0x307a75a (save + redraw, as every other row)
    sel_old = bytes.fromhex("674a1379")
    put(d, m48(BT_SELECT_SITE), sel_old + bytes.fromhex("83f00103"),
        thumb_bl(BT_SELECT_SITE, e["bt_request_open"]) + thumb_bn(BT_SELECT_SITE + 4, 0x0307A75A),
        "Settings Bluetooth select")
    # value: bl bt_value_r1 (r1 = module on) ; the b.n 0x307a3ec that follows stays
    put(d, m48(BT_VALUE_SITE), bytes.fromhex("294b1979"), thumb_bl(BT_VALUE_SITE, e["bt_value_r1"]),
        "Settings Bluetooth value")
    reseal_module(d, M48_IMG)


ROW_SUPPORTERS = 5
SUPPORTERS_HANDLER = 0x0307A1E4   # tbb case of Supporters: movs r2,#1; movs r3,#0; strd r2,r3,[r5,#32] ...
MENU_DONE = 0x0307A1CA            # common tail of the select cases


def menu_bluetooth_row(d, echo=False):
    """Main menu: the Supporters row (kept in --menu-order) becomes Bluetooth."""
    _, e = load_hooks("hooks-echo" if echo else "hooks")
    labels = struct.unpack_from("<9I", d, m47(0x0307AA64))
    idx = labels.index(0x0307A78B)                       # "Supporters", wherever the order put it
    put(d, m47(0x0307AA64) + 4 * idx, struct.pack("<I", 0x0307A78B), struct.pack("<I", e["str_bluetooth"]),
        "main menu Bluetooth label")
    put(d, m47(SUPPORTERS_HANDLER), bytes.fromhex("01220023c5e90823"),
        thumb_bl(SUPPORTERS_HANDLER, e["bt_request_open"]) + thumb_bn(SUPPORTERS_HANDLER + 4, MENU_DONE),
        "main menu Bluetooth select")
    reseal_module(d, M47_IMG)


MENU_DRAW_PTR = 0x0307AAAC     # m47 descriptor (0x307AA88) + 36: draw
DEAD_MODULES = ((0x2CD58, 49), (0x2E43C, 50))   # Spectrum, Debug: no menu row leads to them


def main_menu_draw(d, echo=False):
    _, e = load_hooks("hooks-echo" if echo else "hooks")
    put(d, m47(MENU_DRAW_PTR), struct.pack("<I", 0x0307A2B9), struct.pack("<I", e["menu_draw"] | 1),
        "main menu draw -> menu_draw")
    reseal_module(d, M47_IMG)


MENU_KEY_PTR = 0x0307AAA4   # m47 descriptor +28: key handler (0x307A109)


def main_menu_key(d, echo=False):
    """Both players: hold Play on the Files tab plays the whole internal storage (patches/keys.c)."""
    _, e = load_hooks("hooks-echo" if echo else "hooks")
    put(d, m47(MENU_KEY_PTR), struct.pack("<I", 0x0307A109), struct.pack("<I", e["menu_key"] | 1),
        "main menu key -> menu_key")
    reseal_module(d, M47_IMG)


def settings_about_row(d, echo=False):
    """Settings: Debug strip's slot = About (opens RECHO's own About panel, then back)."""
    _, e = load_hooks("hooks-echo" if echo else "hooks")
    idx = ECHO_SETTINGS_ORDER.index(ROW_DEBUG_STRIP)
    put(d, m48(0x0307AF08) + 4 * idx, struct.pack("<I", 0x0307AE94), struct.pack("<I", e["str_about"]),
        "Settings About label")
    put(d, m48(BT_SELECT_SITE), bytes.fromhex("674a137983f00103"),
        thumb_bl(BT_SELECT_SITE, e["about_request_open"]) + thumb_bn(BT_SELECT_SITE + 4, 0x0307A75A),
        "Settings About select")
    reseal_module(d, M48_IMG)


def remove_modules(d):
    """Blank modules nothing can open any more. RECHO finds modules through its directory
    table, so the others are unaffected; asking for a blanked one fails its magic/CRC check."""
    for off, mid in DEAD_MODULES:
        if d[off:off + 4] != b"SFMD" or u32(d, off + 4) != mid:
            raise SystemExit(f"module m{mid} not at {off:#x}")
        d[off:off + u32(d, off + 12)] = bytes(u32(d, off + 12))


def thumb_bn(src, dst):
    off = dst - (src + 4)
    if not -2048 <= off < 2048 or off & 1:
        raise ValueError("b.n out of range")
    return struct.pack("<H", 0xE000 | ((off >> 1) & 0x7FF))


def settings_hide_rows(d, P, n):
    new_of = {orig: i for i, orig in enumerate(P)}
    lab = struct.unpack_from("<17I", d, m48(0x0307AF08))
    if lab[1] != 0x0307AE10 or lab[10] != 0x0307AE78 or lab[12] != 0x0307AE94:
        raise SystemExit("Settings labels are not the expected ones")
    struct.pack_into("<17I", d, m48(0x0307AF08), *[lab[i] for i in P])
    # value-draw tbh (15 halfwords, rows 0..14); rows that have no value fall to the default
    tbh = list(struct.unpack_from("<15H", d, m48(0x0307A1EE)))
    newtbh = [tbh[P[i]] for i in range(n) if P[i] <= 14 and P[i] not in NO_VALUE_ROWS]
    assert all(P[i] <= 14 and P[i] not in NO_VALUE_ROWS for i in range(len(newtbh))), "value rows must come first"
    vrows = len(newtbh)                       # rows 0..vrows-1 have a value
    newtbh += [newtbh[-1]] * (15 - len(newtbh))
    struct.pack_into("<15H", d, m48(0x0307A1EE), *newtbh)
    put(d, m48(0x0307A1E4), cmp_imm(0, 14), cmp_imm(0, vrows - 1), "Settings value bound (cmp r0,#14)")
    # select tbb (17 bytes)
    tbb = d[m48(0x0307A73E):m48(0x0307A73E) + 17]
    d[m48(0x0307A73E):m48(0x0307A73E) + 17] = bytes(tbb[P[i]] for i in range(17))
    put(d, m48(0x0307A736), cmp_imm(3, 16), cmp_imm(3, n - 1), "Settings select bound (cmp r3,#16)")
    # row-specific checks
    put(d, m48(0x0307A262), cmp_imm(6, 15), cmp_imm(6, new_of[15]), "Settings Clock row (cmp r6,#15)")
    put(d, m48(0x0307A456), cmp_imm(6, 2), cmp_imm(6, new_of[2]), "Settings Equalizer row (cmp r6,#2)")
    put(d, m48(0x0307A0B6), cmp_imm(2, 2), cmp_imm(2, new_of[2]), "Settings hint Equalizer (cmp r2,#2)")
    put(d, m48(0x0307A0BA), cmp_imm(2, 16), cmp_imm(2, new_of[16]), "Settings hint Restore (cmp r2,#16)")
    put(d, m48(0x0307A0C2), cmp_imm(2, 15), cmp_imm(2, new_of[15]), "Settings hint Clock (cmp r2,#15)")
    put(d, m48(0x0307AC1E), bytes([SETTINGS_ROWS, 0x23]), bytes([n, 0x23]), "Settings row count (movs r3,#17)")
    reseal_module(d, M48_IMG)
    return [lab[i] for i in P][:n]


STATUS_CABLE_BRANCH = 0x0305945A   # status bar: `bne.w 0x3059566` = on cable -> bolt/full glyph


def charging_shows_level(d):
    """On cable, draw the normal 0-4 cell level glyph instead of the bolt."""
    o = STATUS_CABLE_BRANCH - RAM_OF_IMG
    put(d, o, bytes.fromhex("40f08480"), bytes.fromhex("00bf00bf"), "status bar cable branch")


# Glyphs nothing draws any more once these patches are in (checked against every UTF-8
# reference in fw1 and the modules; see docs/05-icons.md):
UNUSED_ICONS = (0xE002,                    # stop: only RECHO's Now Playing (replaced by np_draw)
                0xE00C,                    # charging bolt: status-bar cable branch (NOPed), old chooser
                0xE014,                    # wave: Spectrum row and DAC chooser row (both removed)
                0xE016,                    # terminal: Debug row / Debug screen (removed)
                0xE018, 0xE019,            # stereo / mono circles: RECHO's Now Playing
                0xE01B,                    # menu lines: soft-key labels (no button bar any more)
                0xE01C,                    # heart: Supporters row (removed)
                0xE022, 0xE023, 0xE024, 0xE025, 0xE026, 0xE027, 0xE028, 0xE029)  # Now Playing badges
# Both players now: the menu bar shows no headphone glyph (Now Playing names the output) and the
# Mini's menu bar shows the battery as a percentage, so these glyphs are drawn nowhere.
BAR_UNUSED_ICONS = (0xE007, 0xE008, 0xE009, 0xE00A, 0xE00B, 0xE00E, 0xE00F, 0xE010)
# The main menu is drawn by menu_draw with its own bitmaps: RECHO's Power off and About row icons
# are drawn nowhere now. On the Echo the menu header's play / pause glyphs are skipped too.
MENU_UNUSED_ICONS = (0xE017, 0xE02A)
ECHO_ONLY_UNUSED_ICONS = (0xE000, 0xE001)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src")
    ap.add_argument("dst")
    ap.add_argument("--menu-order", default="0,3,1,5,4",
                    help="original menu rows to keep, in order (0 Files, 1 Now playing, 3 USB disk, 4 Settings, "
                         "5 Supporters = Bluetooth, 8 About; 2 Spectrum, 6 Debug, 7 Power off left out)")
    ap.add_argument("--icons", help="folder of edited icon PNGs (tools/icons.py naming)")
    ap.add_argument("--no-charge-screen", action="store_true")
    ap.add_argument("--keep-bolt", action="store_true", help="keep the bolt glyph while charging")
    ap.add_argument("--echo", action="store_true", help="build for the full-size Snowsky Echo (stage 1)")
    a = ap.parse_args()
    if a.echo and a.no_charge_screen:
        raise SystemExit("--echo needs the hook blob (drop --no-charge-screen)")

    d = bytearray(open(a.src, "rb").read())
    if d[0x10:0x18].rstrip(b"\0") != EXPECT_BRAND or d[0x30:0x38].rstrip(b"\0") != EXPECT_TAG:
        raise SystemExit("this patcher only knows RECHO build 2d11bdcd")
    keep = [int(x) for x in a.menu_order.split(",") if x.strip()]
    keep = remove_menu_rows(d, keep)
    print(f"patched: main menu rows {keep} (module m47 re-CRC'd)")
    if not a.no_charge_screen:
        add_hooks(d, animation=not a.keep_bolt, echo=a.echo)
        print("patched: off-state charging screen hooked at 0x305a678")
        if a.echo:
            print("patched: ECHO stage 1 - ST7796S panel init, rows +49 (UI at top, player strip below), GPIO remap, 4.4 mm polarity, "
                  "slot repair off, updater on as RECHOECH.IMG / RECHOEnn.IMG, Menu-10s maskrom hatch")
        if not a.keep_bolt:
            print("patched: status-bar charging animation (0x305945e shim, POWER tick wrapper)")
    usb_chooser_charge_first(d)
    print("patched: USB chooser = Charge only / USB disk; no key for 5 s -> power off (charging screen); DAC removed")
    settings_hide_rows(d, ECHO_SETTINGS_ORDER, ECHO_SETTINGS_SHOWN)
    print("patched: Settings without Show tags, On USB connect, Swap up/down, Debug strip, Equalizer")
    if not a.no_charge_screen:
        menu_bluetooth_row(d, echo=a.echo)
        main_menu_draw(d, echo=a.echo)
        main_menu_key(d, echo=a.echo)
        settings_about_row(d, echo=a.echo)
        print("patched: main menu = pixel-art tabs (menu_draw), Bluetooth in Supporters' slot; "
              "Settings last row = About (RECHO's About panel)")
    remove_modules(d)
    print("patched: unreachable modules m49 (Spectrum) and m50 (Debug) blanked")
    if not a.keep_bolt:
        charging_shows_level(d)
        print("patched: status bar shows the level cells while charging")
    open(a.dst, "wb").write(d)
    seal(a.dst)
    if a.icons:
        from icons import do_import
        do_import(a.dst, a.icons, a.dst)
    from icons import blank
    blank(a.dst, tuple(sorted(set(UNUSED_ICONS + BAR_UNUSED_ICONS + MENU_UNUSED_ICONS
                                  + (ECHO_ONLY_UNUSED_ICONS if a.echo else ())))), a.dst)
    ok = info(open(a.dst, "rb").read())
    if not ok:
        raise SystemExit("seal check failed")


if __name__ == "__main__":
    main()
