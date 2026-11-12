"""Independent pre-flash cross-check of a patched RECHO image against the original.

  py -3 -I verify_image.py ORIGINAL.IMG PATCHED.IMG

Exits non-zero on any failed check. Covers the failure modes recorded in
ReChord's brick log (load rows, .data/.bss placement, loader base, header vs
self-copy, CRCs) plus checks specific to these patches.
"""
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rkimg import crc32, u32, fw_end  # noqa: E402

RAM_OF_IMG = 0x0304F298
CAVE_LO, CAVE_HI = 0x0306D92C, 0x03074B70      # zero gap: end of .text .. .data image
HOOK_SITE, HOOK_ORIG = 0x0305A678, 0x03051E28
LOADER_BASE = 0x03088000
M47 = 0x2B224
ICONS_PART = 2
# fw1 entry points the hook is allowed to call (all read from RECHO's own code)
ALLOWED_CALLS = {0x03051E28, 0x03051C84, 0x03051638, 0x03051C48, 0x03051C60, 0x03051C70,
                 0x03051610, 0x030583A8, 0x0305837C, 0x0306A4F0, 0x0306A7C4, 0x0305C51C, 0x0306BF14, 0x0306BF2C,
                 0x030599EC, 0x0306BC70, 0x0306BDD4}
ALLOWED_CALLS |= {0x0305B234, 0x0305BA3C}   # key remap (patches/keys.c): open module n, go to main menu
ALLOWED_DATA = {0x03004000, 0x030401EC, 0x0303E73C, 0x030399C8}  # + current module id (key remap)  # base reg for the brightness read: ldr r7, [base, #0x1d4] = 0x030041D4

# Both players: screens drawn by the mod (patches/ui_np.c) and the clock (patches/clock.c)
UI_CALLS = {0x03058D84, 0x03058D20, 0x0305AEEC,                 # draw text, text width, RECHO's HINTS draw
            0x03057BD0, 0x03057BE4, 0x03057BDC, 0x0305A194, 0x0305A1B0, 0x0305A1BC, 0x0305A8F8,
            0x0305A1CC, 0x0305A1C4, 0x03057DB4, 0x03057DC0, 0x03059AE8,  # player / queue / tag / format
            0x0305C41C, 0x0305AE28,                                       # current screen, bar message
            0x0305DA2C, 0x0305DAC4,                                       # clock chip read / decode
            0x03055EC0,                                                   # RECHO's jack detection (bit 0 3.5 mm, bit 1 4.4 mm)
            0x0307A2B8,                                                   # m47's own draw (About / Supporters panels), only from menu_draw
            0x03065FE4,                                                   # queue "next" (pure computation): position in the play order
            0x0306C290}                                                   # RECHO's boot text routine (boot_screen)
UI_DATA = {0x03034B48,                                                    # font table
           0x030041D0, 0x0303AB8C,                                        # volume dB, last volume change
           0x030041B4, 0x030399B8, 0x030399BC,                            # chooser cursor, countdown
           0x0303EDD0,                                                    # settings block (play mode, byte 7)
           0x03008A00, 0x0303BCC0,                                        # frame buffer (2x title), main menu state
           0x0303AB90, 0x0303AB98, 0x0303ABA8, 0x0303BD74, 0x0303BD78}    # queue object, seed, pass, active, current
ALLOWED_CALLS |= UI_CALLS
ALLOWED_DATA |= UI_DATA
# Bluetooth (patches/bt.c): RECHO's GPIO function / log, the module's UART1, the UART1 pin
# group, clock and reset registers, the "Log to file" settings byte, and the screen plumbing the
# cable chooser uses (close the loaded module, switch screen, list keys).
BT_CALLS = {0x03051570, 0x03051590, 0x0306C9EC,   # gpio function, gpio set, log
            0x0305BA54, 0x0305C3E4, 0x03059FE8,   # close module, switch screen, list keys
            0x03057C94, 0x0305AB80}               # pause / play and skip (headphone buttons)
SHARED_CONSTS = set()   # `tbb` jump-table bytes are skipped by hook_literals(), so nothing here
BT_LOG_FNS = {"bt_tick", "bt_power", "bt_key", "handle_frame", "add_device", "connect", "receive",
              "lyrics_tick", "set_stream", "bt_route", "follow_player", "log_audio_state", "raw_flush", "dump_regs"}   # + one line per track from the lyrics loader (Log to file on)
BT_DATA = {0x0303EDD0}                           # settings block (byte 5 = log to file)
BT_REGS = {0x400B0000, 0x400B0004, 0x400B000C, 0x400B0010, 0x400B0014, 0x400B007C, 0x400B0088,
           0x400B0098, 0x400B009C, 0x400B00A0, 0x400B00A8, 0x500100BC,
           0x40180038, 0x40180084, 0x401800A0, 0x401800C0,
           0x500100C4, 0x50010014, 0x50010054,   # stock's start-up settings applied once before streaming (Podium)
           0x40030000, 0x40030004, 0x40030008, 0x4003000C, 0x40030010, 0x4003001C,  # I2S1, read for the log only
           0x50010000, 0x40180000, 0x40160000, 0x40170000, 0x50030000}  # GRF / CRU / GPIO bases: read-only dump
ALLOWED_CALLS |= BT_CALLS
ALLOWED_DATA |= BT_DATA

ECHO = False          # set by --echo: verify a full-size Echo stage-1 image
HOOKS = "hooks"
# Extra firmware routines / data / registers the Echo hooks may use (see patches/echo_port.c)
ECHO_CALLS = {0x0306AF98, 0x0306AF6C, 0x0305DA2C, 0x0305DAC4,  # + clock chip read / decode
                                   # panel send, panel mode
              0x0306B534,                                  # wait until no LCD DMA band is in flight
              0x03058D84, 0x03065FE4, 0x03064B64,          # sidebar: text, queue next, entry name
              0x03058D20, 0x0305AEEC,                      # text width, RECHO's button-bar (HINTS) draw
              # stage 5 screens: player state, elapsed, format, queue loaded/index/info, tag record,
              # file / folder name, ReplayGain mode / gain, m:ss formatter, current screen,
              # bar message, request redraw
              0x03057BD0, 0x03057BE4, 0x03057BDC, 0x0305A194, 0x0305A1B0, 0x0305A1BC, 0x0305A8F8,
              0x0305A1CC, 0x0305A1C4, 0x03057DB4, 0x03057DC0, 0x03059AE8, 0x0305C41C, 0x0305AE28,
              0x0305B234, 0x0305BA3C,                      # open module n, go to main menu (key remap)
              # lyrics (patches/lyrics.c): volume of a number, find a name in a folder, file object, file read
              0x0305A02C, 0x030638FC, 0x03053900, 0x03053934}
ECHO_TRAMPOLINE = 0x03051594                                # back into GPIO set after its prologue
ECHO_DATA = {0x030089EC, 0x030041A8,                       # VOP-active flag, key-ladder raw value
             0x03006174, 0x03004114, 0x03034B48, 0x03008A00,  # backlight duty, clip rect, font table, frame buffer
             0x0303AB90, 0x0303AB94, 0x0303AB98, 0x0303ABA8,  # queue object, count, seed, shuffle pass
             0x0303BD74, 0x0303BD78, 0x0303BD80, 0x0303EDD0,  # queue active, current, store, settings (play mode = byte 7)
             0x03044000, 0x03048200, 0x03048400,              # shared strip (sidebar + band), name buffer, lyrics buffer (free RAM)
             0x030041D8, 0x0303E730,                          # content area rect, "screen has a button bar" flag
             0x030041D0, 0x0303AB8C,                          # volume dB, time of the last volume change
             0x030041B4, 0x030399B8, 0x030399BC,              # cable chooser cursor, countdown flag / start
             0x030399C8}                                      # current module id
ECHO_CONSTS = {0xDF014001,  # not an address: column window bytes 01 40 01 DF (cols 320..479)
               0x811C9DC5, 0x811CC79F,  # not addresses: FNV-1a start values (button-bar change check)
               0x8F015000}  # not an address: column window bytes 00 50 01 8F (cols 80..399, centred charge screen)
FREE_RAM = (0x03043002, 0x0304A240)   # no RECHO code or module refers to anything in here
# Text routines may be used by the sidebar, the info line and the Now Playing shims; the
# queue read only by the sidebar tick. The charging screen and power paths use none of them.
# Functions that must never draw text or touch storage: the charging screen and power paths.
NO_TEXT_FNS = {"charge_screen", "read_level", "draw", "anim_level", "power_tick_wrap", "status_level_shim",
               "echo_hatch_tick", "echo_panel_init", "gpio_set_echo", "gpio_set_orig", "echo_gpio_read",
               "echo_flush_centred", "echo_panel_fill", "echo_blank_outside", "loader_reboot"}
STORAGE_FNS = {"echo_sidebar_tick", "draw_entry", "load", "lyrics_tick"}  # queue name lookups (Up Next, lyrics)
TAG_FNS = {"np_draw", "track_line_xy", "track_title", "track_album", "footer_row", "next_line", "menu_draw"}  # RECHO's tag record: Now Playing and the Echo player strip
ECHO_REGS = {0x60070024, 0x6007002C,                        # LCD controller status / data
             0x40180080, 0x401800A0, 0x401800A8, 0x401800C0, 0x401800C4, 0x401800C8, 0x401800CC,  # CRU
             0x50010098, 0x500100B4, 0x500100B8, 0x500100C4,  # GRF PLL / boot select
             0x5002004C,                                     # PMU boot flag
             0x50010054,                                     # GRF port-2 pull: GPIO2.8 (4.4 mm sense) pull on
             0xE000E010, 0xE000E180, 0xE000E184, 0xE000E280, 0xE000E284, 0xE000ED08, 0xE000ED0C}
PODIUM_PANEL = None

fails = []


def hook_literals(dis):
    """Every `.word` in the hook disassembly except the bytes of `tbb`/`tbh` jump tables."""
    out, in_table = set(), False
    for ln in dis.splitlines():
        if re.search(r"\btb[bh]\b", ln):
            in_table = True
            continue
        m = re.search(r"\.word\s+0x([0-9a-f]+)", ln)
        if m:
            if not in_table:
                out.add(int(m.group(1), 16))
            continue
        if re.match(r"^\s+[0-9a-f]+:\s", ln) or re.match(r"^[0-9a-f]+ <", ln):
            in_table = False
    return out


def check(cond, msg):
    print(("  ok   " if cond else "  FAIL ") + msg)
    if not cond:
        fails.append(msg)


def bl_target(d, img_off, src):
    hw1, hw2 = struct.unpack_from("<HH", d, img_off)
    if (hw1 & 0xF800) != 0xF000 or (hw2 & 0xD000) != 0xD000:
        return None
    s = (hw1 >> 10) & 1
    imm10, imm11 = hw1 & 0x3FF, hw2 & 0x7FF
    j1, j2 = (hw2 >> 13) & 1, (hw2 >> 11) & 1
    i1, i2 = ~(j1 ^ s) & 1, ~(j2 ^ s) & 1
    off = (s << 24) | (i1 << 23) | (i2 << 22) | (imm10 << 12) | (imm11 << 1)
    if s:
        off -= 1 << 25
    return src + 4 + off


def bw_target(d, img_off, src):
    hw1, hw2 = struct.unpack_from("<HH", d, img_off)
    if (hw1 & 0xF800) != 0xF000 or (hw2 & 0xD000) != 0x9000:
        return None
    return bl_target(struct.pack("<HH", hw1, hw2 | 0x4000), 0, src)


def pc_literals(code, base):
    """Values loaded by every PC-relative ldr / ldr.w, decoded at every halfword."""
    out = []
    for k in range(0, len(code) - 3, 2):
        hw = struct.unpack_from("<H", code, k)[0]
        if hw & 0xF800 == 0x4800:
            la = ((base + k + 4) & ~3) + (hw & 0xFF) * 4
        elif hw in (0xF8DF, 0xF85F):
            imm = struct.unpack_from("<H", code, k + 2)[0] & 0xFFF
            la = ((base + k + 4) & ~3) + (imm if hw == 0xF8DF else -imm)
        else:
            continue
        if 0 <= la - base <= len(code) - 4:
            out.append(u32(code, la - base))
    return out


def modules(d):
    out, o = [], 0
    while True:
        o = d.find(b"SFMD", o + 1)
        if o < 0 or o > 0x3B8E8:
            return out
        if u32(d, o + 8) == 0x1E9D3409 and 0x100 < u32(d, o + 12) < 0x20000:
            out.append(o)


def main(orig_path, new_path):
    a = open(orig_path, "rb").read()
    b = open(new_path, "rb").read()

    print("[1] container / brick-log checks")
    check(len(a) == len(b) == 0x500004, f"size {len(b):#x} unchanged")
    check(a[:0x200] == b[:0x200], "outer header 0x000-0x1FF byte-identical (version, part chain, fw size)")
    check(a[0x1F8:0xD68] == b[0x1F8:0xD68], "RKnanoFW header + all 91 load rows byte-identical")
    fw_size = u32(b, 0x1F4)
    check(b[fw_size:fw_size + 0x200] == b[:0x200], f"header self-copy at {fw_size:#x} matches header")
    end = fw_end(fw_size)
    check(u32(b, end) == crc32(b[:end]), f"image CRC-32 trailer at {end:#x} valid")
    row0_end = u32(b, 0x208 + 12)
    check(row0_end < LOADER_BASE, f"code row ends {row0_end:#x} < loader base {LOADER_BASE:#x}")
    def same_except(lo, hi, word):
        return a[lo:word] == b[lo:word] and a[word + 4:hi] == b[word + 4:hi]
    check(same_except(0x273D4, 0x28ED0, 0x27584), ".data image (0x273D4..0x28ED0) unchanged except the USB chooser row count")
    data_img = CAVE_HI - RAM_OF_IMG
    check(same_except(data_img, data_img + 0x1AFC, 0x25A88), ".data load image at 0x03074B70 unchanged except the USB chooser row count")
    check(u32(a, 0x25A88) == 3 == u32(a, 0x27584) and u32(b, 0x25A88) == 2 == u32(b, 0x27584),
          "USB chooser row count 3 -> 2 in both .data images")

    print("[2] every byte that changed is in an expected place")
    allowed = []
    hook_img = HOOK_SITE - RAM_OF_IMG
    allowed.append((hook_img, hook_img + 4, "hook call"))
    allowed.append((CAVE_LO - RAM_OF_IMG, CAVE_HI - RAM_OF_IMG, "code cave"))
    allowed.append((M47, M47 + u32(a, M47 + 12), "module m47 (main menu)"))
    sb = 0x0305945A - RAM_OF_IMG
    allowed.append((sb, sb + 10, "status bar branch + level shim"))
    pt = 0x0306AC68 - RAM_OF_IMG
    allowed.append((pt, pt + 4, "POWER tick pointer"))
    for addr, n, what in ((0x0305BA70, 2, "chooser opener"), (0x0305B948, 2, "chooser select"),
                          (0x03059D96, 2, "chooser row note"), (0x03059FC2, 2, "chooser hint"),
                          (0x03060AE4, 24, "chooser icon/label tables"),
                          (0x0305B9B0, 4, "chooser auto-select call")):
        allowed.append((addr - RAM_OF_IMG, addr - RAM_OF_IMG + n, what))
    allowed.append((0x25A88, 0x25A8C, "chooser count (.data load image)"))
    allowed.append((0x27584, 0x27588, "chooser count (.data copy)"))
    allowed.append((0x2BDD8, 0x2BDD8 + u32(a, 0x2BDD8 + 12), "module m48 (Settings)"))
    for off, mid, name in ((0x2CD58, 49, "Spectrum"), (0x2E43C, 50, "Debug")):
        ln = u32(a, off + 12)
        check(a[off:off + 4] == b"SFMD" and u32(a, off + 4) == mid and not any(b[off:off + ln]),
              f"module m{mid} ({name}) blanked: {ln} bytes, nothing opens it")
        allowed.append((off, off + ln, f"module m{mid} ({name}) blanked"))
    allowed.append((0x2A494, 0x2A494 + u32(a, 0x2A494 + 12), "module m45 (Now Playing)"))
    allowed.append((0x0305BF18 - RAM_OF_IMG, 0x0305BF18 - RAM_OF_IMG + 4, "hold-menu call"))
    for addr, n, what in ((0x0305C4B8, 2, "content area bottom (1)"), (0x0305C4EE, 2, "content area bottom (2)"),
                          (0x03060BBC, 4, "HINTS layer draw pointer"), (0x03060AD4, 4, "cable chooser draw pointer"),
                          (0x03059504, 4, "menu bar headphone glyph"),
                          (0x030594D4, 4, "menu bar volume text"), (0x0305A4B0, 4, "volume message text"),
                          (0x03050D7C, 4, "boot splash call"),
                          (0x03059B3A if ECHO else 0x03059474, 2 if ECHO else 4,
                           "menu header play time" if ECHO else "menu bar battery glyph")):
        allowed.append((addr - RAM_OF_IMG, addr - RAM_OF_IMG + n, what))
    if ECHO:
        for addr, n, what in ((0x0306B9F4, 4, "echo: panel init (boot)"), (0x0306C206, 4, "echo: panel init (wake)"),
                              (0x0306B20C, 4, "echo: window row offsets"), (0x03051590, 4, "echo: GPIO set entry"),
                              (0x03055EE8, 4, "echo: 4.4 mm read"), (0x03055F6A, 4, "echo: GPIO2.0 read"),
                              (0x03050FBE, 4, "echo: FW1 repair call"), (0x0305F3BF, 11, "echo: update file name"),
                              (0x0305F3CB, 6, "echo: update file prefix"),
                              (0x030594A4, 8, "echo: status-bar battery glyph and volume x")):
            allowed.append((addr - RAM_OF_IMG, addr - RAM_OF_IMG + n, what))
    io, isz = u32(a, 0x78 + 8 * ICONS_PART), u32(a, 0x7C + 8 * ICONS_PART)
    allowed.append((io, io + isz, "icon pack"))
    allowed.append((end, end + 4, "CRC trailer"))
    stray, i = [], 0
    while i < len(a):
        if a[i] != b[i] and not any(lo <= i < hi for lo, hi, _ in allowed):
            stray.append(i)
        i += 1
    check(not stray, "no changes outside hook/cave/m47/icons/trailer"
          + (f" (stray at {[hex(x) for x in stray[:8]]})" if stray else ""))
    for lo, hi, name in allowed:
        n = sum(1 for k in range(lo, hi) if a[k] != b[k])
        print(f"         {name:24s} {n:5d} byte(s) changed")

    print("[3] code cave")
    BLOB_END = (0x0306E000 + 0xFFF + len(open(os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "build", HOOKS + ".bin"), "rb").read())) & ~0xFFF
    lo, hi = CAVE_LO - RAM_OF_IMG, CAVE_HI - RAM_OF_IMG
    check(not any(a[lo:hi]), "cave is all zero in the original")
    used = [k for k in range(lo, hi) if b[k]]
    first, last = (used[0], used[-1]) if used else (0, 0)
    check(bool(used) and first >= lo and last < hi - 0x100,
          f"patch code sits at {first + RAM_OF_IMG:#x}..{last + RAM_OF_IMG:#x}, inside the gap with margin")
    refs = []
    for m in modules(a):
        body = a[m:m + u32(a, m + 12)]
        for k in range(0, len(body) - 3, 4):   # literal pools are word aligned
            v = u32(body, k)
            if CAVE_LO + 0x600 <= v < CAVE_HI:
                refs.append(v)
    for v in pc_literals(a[0xD68:0x273D4], 0x03050000):
        if 0x0306E000 <= v < BLOB_END:
            refs.append(v)
    check(not refs, f"nothing in the original fw1 code or modules points at 0x0306E000..{BLOB_END:#x}")

    print("[4] hook")
    tgt = bl_target(a, hook_img, HOOK_SITE)
    check(tgt == HOOK_ORIG, f"original instruction at {HOOK_SITE:#x} is bl {HOOK_ORIG:#x}")
    tgt = bl_target(b, hook_img, HOOK_SITE)
    check(tgt is not None and CAVE_LO <= tgt < CAVE_HI, f"patched instruction is bl {tgt:#x} (into the cave)")
    blob = b[0x0306E000 - RAM_OF_IMG:last + 1]
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    built = open(os.path.join(root, "build", HOOKS + ".bin"), "rb").read()
    check(blob.rstrip(bytes(1)) == built.rstrip(bytes(1)), "cave bytes == compiled build/hooks.bin")
    import re
    import subprocess
    objdump = os.environ.get("OBJDUMP", os.path.join("C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi",
                                                     "12.2 mpacbti-rel1/bin/arm-none-eabi-objdump.exe"))
    dis = subprocess.run([objdump, "-d", os.path.join(root, "build", HOOKS + ".elf")],
                         capture_output=True, text=True).stdout
    lits = hook_literals(dis)
    fw_ptrs = {v for v in lits if 0x03000000 <= v < 0x03100000 and not (0x0306E000 <= v < BLOB_END)}
    calls = {v & ~1 for v in fw_ptrs if v & 1}
    data = {v for v in fw_ptrs if not v & 1}
    allowed_calls, allowed_data = set(ALLOWED_CALLS), set(ALLOWED_DATA)
    if ECHO:
        check(ECHO_TRAMPOLINE in calls, "echo: GPIO trampoline re-enters GPIO set after its 4-byte prologue")
        calls.discard(ECHO_TRAMPOLINE)
        allowed_calls |= ECHO_CALLS
        allowed_data |= ECHO_DATA
    check(calls <= allowed_calls, f"hook only calls known RECHO routines ({len(calls)} distinct)"
          + ("" if calls <= allowed_calls else f" unknown: {[hex(x) for x in calls - allowed_calls]}"))
    hw = {v for v in lits if 0x40000000 <= v < 0xF0000000} - (ECHO_CONSTS if ECHO else set()) - SHARED_CONSTS
    hw_ok = {0x400D0000} | BT_REGS | (ECHO_REGS if ECHO else set())
    check(hw <= hw_ok, f"hook touches only the expected hardware registers ({len(hw)})"
          + ("" if hw <= hw_ok else f" (also {[hex(x) for x in hw - hw_ok]})"))
    banned = {0x03058D84, 0x030599EC, 0x03059AB0, 0x03058D20, 0x030583A8, 0x0306C9EC, 0x03052806}
    if True:
        # Which function's literal pool holds each banned call? Screens may draw text; the
        # charging screen and the power paths may not; storage only where intended.
        owner, where = None, {}
        for ln in dis.splitlines():
            m = re.match(r"^([0-9a-f]+) <(\w+)>:", ln)
            if m:
                owner = m.group(2)
            m = re.search(r"\.word\s+0x([0-9a-f]+)", ln)
            if m and owner:
                where.setdefault(int(m.group(1), 16) & ~1, set()).add(owner)
        used = {c: where.get(c, set()) for c in calls & banned}
        logs = used.pop(0x0306C9EC, set())
        check(logs <= BT_LOG_FNS, f"RECHO's log is used only by the Bluetooth code ({sorted(logs)})")
        text_ok = set(used) <= {0x03058D84, 0x03058D20} and not any(f & NO_TEXT_FNS for f in used.values())
        check(text_ok, "charging screen and power paths draw no text; only the two text routines are used"
              + f" ({ {hex(k): sorted(v) for k, v in used.items()} })")
        tags = where.get(0x0305A8F8, set())
        check(tags and tags <= TAG_FNS, f"tag record read only by Now Playing and the player strip ({sorted(tags)})")
        qread = where.get(0x03064B64, set())
        if ECHO:
            check(qread and qread <= STORAGE_FNS, f"queue reads only from the sidebar and the lyrics loader ({sorted(qread)})")
        else:
            check(not qread, "no queue reads on the Echo Mini")
    sbw = struct.unpack_from("<HH", b, 0x0305945A - RAM_OF_IMG)
    check(struct.unpack_from("<HH", a, 0x0305945A - RAM_OF_IMG) == (0xF040, 0x8084) and sbw == (0xBF00, 0xBF00),
          "status bar: on-cable branch to the bolt glyph replaced by two NOPs")
    entries = dict(l.split() for l in open(os.path.join(root, "build", HOOKS + ".entries")))
    shim = int(entries["status_level_shim"], 16)
    lvl = 0x0305945E - RAM_OF_IMG
    check(a[lvl:lvl + 6] == bytes.fromhex("9a68d3f81480") and bl_target(b, lvl, 0x0305945E) == shim
          and b[lvl + 4:lvl + 6] == bytes.fromhex("00bf"), f"status bar level load -> bl status_level_shim ({shim:#x}) + nop")
    wrap = int(entries["power_tick_wrap"], 16)
    check(u32(a, pt) == 0x0306A7C5 and u32(b, pt) == (wrap | 1),
          f"POWER tick pointer 0x0306A7C5 -> power_tick_wrap ({wrap:#x})")
    check(0x0306A7C5 in lits, "wrapper still calls the original POWER tick")
    check(data <= allowed_data, "hook only uses the expected RECHO variables" +
          ("" if data <= allowed_data else f" (also {[hex(x) for x in data - allowed_data]})"))
    for f in sorted(calls):
        o = f - 0x03050000 + 0xD68
        first_hw = struct.unpack_from("<H", a, o)[0]
        print(f"         call {f:#x}: first halfword {first_hw:04x}")

    print("[5] modules")
    for m in modules(b):
        ln = u32(b, m + 12)
        body = bytearray(b[m:m + ln])
        want = u32(body, 24)
        body[24:28] = b"\0\0\0\0"
        same_hdr = a[m:m + 24] == b[m:m + 24] and a[m + 28:m + 36] == b[m + 28:m + 36]
        check(crc32(body) == want and same_hdr, f"m{u32(b, m + 4)} @ {m:#x}: CRC valid, header fields unchanged")

    print("[6] main menu (m47)")
    def m47(addr):
        return M47 + addr - 0x0307A000
    labels = []
    for k in range(9):
        p = u32(b, m47(0x0307AA64) + 4 * k)
        o = m47(p) if p >= 0x0307A000 else p - RAM_OF_IMG     # the Bluetooth label lives in the cave
        s = b[o:b.index(0, o)].decode("latin1")
        labels.append(s)
    count = b[m47(0x0307A67C)]
    bound = b[m47(0x0307A19C)]
    print(f"         rows: {labels[:count]}")
    check(b[m47(0x0307A67D)] == 0x23 and count == bound + 1, f"row count {count} and dispatch bound {bound} agree")
    tbb = b[m47(0x0307A1A4):m47(0x0307A1A4) + 9]
    orig_tbb = a[m47(0x0307A1A4):m47(0x0307A1A4) + 9]
    orig_labels = [a[m47(u32(a, m47(0x0307AA64) + 4 * k)):].split(b"\0")[0].decode("latin1") for k in range(9)]
    target_of = {orig_labels[k]: orig_tbb[k] for k in range(9)}
    target_of["Bluetooth"] = target_of["Supporters"]           # Supporters' case now opens Bluetooth
    check(all(tbb[k] == target_of[labels[k]] for k in range(count)), "each row still dispatches to its own handler")
    usb = labels.index("USB disk")
    check(b[m47(0x0307A04C)] == usb and b[m47(0x0307A6DA)] == usb, f"USB-disk row checks use row {usb}")
    if "Power off" in labels[:count]:
        pwr = labels.index("Power off")
        check(b[m47(0x0307A0E6)] == pwr and b[m47(0x0307A6E4)] == pwr, f"Power-off row checks use row {pwr}")
    else:
        check(b[m47(0x0307A0E6)] == 15 and b[m47(0x0307A6E4)] == 15 and 15 >= count,
              "Power off removed; its row checks point at row 15, which no row has")
    check("Spectrum" not in labels[:count] and "Supporters" not in labels[:count], "Spectrum and Supporters gone")
    ent6 = {k: int(v, 16) for k, v in (l.split() for l in open(os.path.join(
        os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "build", HOOKS + ".entries")))}
    check(labels[:count] == ["Files", "USB disk", "Now playing", "Bluetooth", "Settings"],
          "main menu: Files, USB disk, Now playing (centre), Bluetooth, Settings")
    dp = m47(0x0307AAAC)
    check(u32(a, dp) == 0x0307A2B9 and u32(b, dp) == (ent6["menu_draw"] | 1) and a[m47(0x0307AA88):m47(0x0307AA88) + 4] == b"MENU",
          "main menu descriptor draw 0x307a2b9 -> menu_draw")
    so = m47(0x0307A1E4)
    check(a[so:so + 8] == bytes.fromhex("01220023c5e90823") and bl_target(b, so, 0x0307A1E4) == ent6["bt_request_open"]
          and b[so + 4:so + 6] == bytes.fromhex("efe7"),
          "Supporters case 0x307a1e4 -> bl bt_request_open; b.n 0x307a1ca")

    print("[7] icon pack")
    pk = b[io:io + isz]
    check(pk[:8] == b"EchoIcon", "magic intact")
    n = struct.unpack_from("<H", pk, 10)[0]
    check(n == 44, f"{n} glyphs")
    ok = True
    for k in range(n):
        w, h, ya, _, _, off = struct.unpack_from("<BBBBII", pk, 16 + 12 * k)
        if off + ((w + 7) // 8) * h > isz or ya + h > 16:
            ok = False
    check(ok, "every glyph's bitmap is inside the pack and inside the 16 px line")
    # The firmware's own loader rules (fw1 0x3058bc8..0x3058c66): no checksum, structure only.
    ver, cnt, first, lh = struct.unpack_from("<HHHH", pk, 8)
    rules = ver == 1 and cnt > 0 and first >= 0xE000 and first + cnt - 1 < 0xF900 and 1 <= lh <= 64         and isz >= 16 + 12 * cnt and 16 <= isz <= 0x1000
    for k in range(cnt):
        w, h, _, bb, fl, off = struct.unpack_from("<BBBBII", pk, 16 + 12 * k)
        rules = rules and 1 <= w <= 64 and 1 <= h <= 64 and bb <= 15 and (fl & 0xFF) <= 15             and off <= isz and ((w + 7) // 8) * h <= isz - off
    check(rules, "passes RECHO's icon loader validation rules (else it falls back to built-in glyphs)")
    for k in range(7, 13):
        w, h, ya = struct.unpack_from("<BBB", pk, 16 + 12 * k)
        print(f"         U+E{k:03X}: {w}x{h} at y+{ya}")


    print("[8] USB chooser")
    def fws(img, addr):
        o = addr - RAM_OF_IMG
        return img[o:img.index(0, o)].decode("latin1")
    labels = [fws(b, u32(b, 0x03060AF0 - RAM_OF_IMG + 4 * k)) for k in range(u32(b, 0x25A88))]
    print(f"         rows: {labels}")
    check(labels == ["Charge only", "USB disk"], "rows are Charge only, USB disk (no USB DAC)")
    check(b[0x0305BA70 - RAM_OF_IMG:0x0305BA72 - RAM_OF_IMG] == bytes.fromhex("0123"),
          "opener always starts on row 0 with the 5 s countdown (movs r3,#1)")
    check(bl_target(b, 0x0305B948 - RAM_OF_IMG, 0x0305B948) is None and
          b[0x0305B948 - RAM_OF_IMG:0x0305B94A - RAM_OF_IMG] == bytes.fromhex("53b1"),
          "select: row 0 (cbz) -> leave = charge only; row 1 -> USB disk")

    check(bl_target(a, 0x0305B9B0 - RAM_OF_IMG, 0x0305B9B0) == 0x0305B934
          and bl_target(b, 0x0305B9B0 - RAM_OF_IMG, 0x0305B9B0) == 0x0305A618,
          "countdown expiry: select -> power off (0x305a618), which ends in the charging screen on a cable")
    # the select routine itself stays reachable from the keys (select key at 0x305ba1e)
    check(bl_target(b, 0x0305BA1E - RAM_OF_IMG, 0x0305BA1E) == 0x0305B934, "manual select (key) still uses the select routine")

    print("[9] Settings (m48)")
    A0, B0 = 0x2BDD8, 0x0307A000
    def m48s(img, addr):
        o = A0 + addr - B0 if addr >= B0 else addr - RAM_OF_IMG   # a label may live in the cave
        return img[o:img.index(0, o)].decode("latin1")
    def rows(img):
        n = img[A0 + 0x0307AC1E - B0]
        labs = [m48s(img, u32(img, A0 + 0x0307AF08 - B0 + 4 * k)) for k in range(17)]
        sel = img[A0 + 0x0307A73E - B0:A0 + 0x0307A73E - B0 + 17]
        tbh = struct.unpack_from("<15H", img, A0 + 0x0307A1EE - B0)
        vb = img[A0 + 0x0307A1E4 - B0]
        return n, labs, sel, tbh, vb
    n0, l0, s0, t0, v0 = rows(a)
    n1, l1, s1, t1, v1 = rows(b)
    print(f"         rows: {l1[:n1]}")
    hidden = {"Show tags", "On USB connect", "Debug strip", "Swap up/down", "Equalizer"}
    check(n1 == 17 - len(hidden) + 1 and not hidden & set(l1[:n1]) and l1[n1 - 1] == "About",
          f"{n1} rows; {', '.join(sorted(hidden))} hidden; About last")
    orig_sel = {l0[k]: s0[k] for k in range(17)}
    orig_val = {l0[k]: (t0[k] if k <= v0 else None) for k in range(17)}
    orig_sel["About"], orig_val["About"] = orig_sel["Debug strip"], None
    ent9 = {k: int(v, 16) for k, v in (l.split() for l in open(os.path.join(
        os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "build", HOOKS + ".entries")))}
    so = A0 + 0x0307A804 - B0
    check(a[so:so + 8] == bytes.fromhex("674a137983f00103") and bl_target(b, so, 0x0307A804) == ent9["about_request_open"]
          and b[so + 4:so + 6] == bytes.fromhex("a7e7"),
          "Debug strip select case 0x307a804 -> bl about_request_open; b.n 0x307a75a")
    check(all(s1[k] == orig_sel[l1[k]] for k in range(n1)), "every Settings row selects its original handler")
    check(all((t1[k] if k <= v1 else None) == orig_val[l1[k]] for k in range(n1)),
          "every Settings row shows its original value")
    diff9 = {k for k in range(u32(a, A0 + 12)) if a[A0 + k] != b[A0 + k]}
    print(f"         m48: {len(diff9)} byte(s) changed")
    def row_of(img, addr):
        return img[A0 + addr - B0]
    l1 = l1[:n1] + [l0[k] for k in range(17) if l0[k] not in l1[:n1] and l0[k] != "Debug strip"]
    check(row_of(b, 0x0307A262) == l1.index("Clock") and row_of(b, 0x0307A0C2) == l1.index("Clock"),
          "Clock row checks follow the Clock row")
    check(row_of(b, 0x0307A456) == l1.index("Equalizer") and row_of(b, 0x0307A0B6) == l1.index("Equalizer"),
          "Equalizer row checks follow the Equalizer row")
    check(row_of(b, 0x0307A0BA) == l1.index("Restore defaults"), "Restore-defaults hint follows its row")
    check(b[A0 + 0x0307A736 - B0] == n1 - 1, "select bound = row count - 1")

    print("[12] layout (both players): no button bar, Now Playing and cable chooser screens")
    entl = {k: int(v, 16) for k, v in entries.items()}
    def atl(addr):
        return addr - RAM_OF_IMG
    for site in (0x0305C4B8, 0x0305C4EE):
        check(a[atl(site):atl(site) + 2] == bytes([143, 0x22]) and b[atl(site):atl(site) + 2] == bytes([169, 0x22]),
              f"content area bottom at {site:#x}: movs r2,#143 -> #169")
    check(u32(a, atl(0x03060BBC)) == 0x0305AEED and u32(b, atl(0x03060BBC)) == (entl["hints_stub"] | 1)
          and a[atl(0x03060B98):atl(0x03060B9D)] == b"HINTS",
          "HINTS layer draw pointer -> hints_stub (labels gone; messages kept)")
    o = 0x2A494 + 0x0307A1F0 - 0x0307A000
    check(a[o:o + 4] == bytes.fromhex("2de9f04f") and bw_target(b, o, 0x0307A1F0) == entl["np_draw"],
          "m45 draw entry 0x307a1f0: push {r4-r11,lr} -> b.w np_draw")
    check(u32(a, atl(0x03060AD4)) == 0x03059EDD and u32(b, atl(0x03060AD4)) == (entl["usb_draw"] | 1)
          and a[atl(0x03060AB0):atl(0x03060AB6)] == b"USBSEL",
          "cable chooser (USBSEL) draw pointer -> usb_draw")
    hg = 0x03059504 - RAM_OF_IMG
    check(a[hg:hg + 4] == bytes.fromhex("012d4cd9") and b[hg:hg + 4] == bytes.fromhex("3c462ae0"),
          "menu bar: headphone glyph skipped (cmp r5,#1; bls -> mov r4,r7; b.n 0x305955e)")
    if ECHO:
        tc = 0x03059B3A - RAM_OF_IMG
        check(a[tc:tc + 2] == bytes.fromhex("26d8") and b[tc:tc + 2] == bytes.fromhex("26e0"),
              "menu header: play time skipped on the Echo (bhi.n -> b.n 0x3059b8a)")
    else:
        bg = 0x03059474 - RAM_OF_IMG
        check(a[bg:bg + 4] == bytes.fromhex("53f82250") and bl_target(b, bg, 0x03059474) == entl["battery_pct_shim"],
              "menu bar: battery glyph load -> bl battery_pct_shim (percentage text)")
    for site, kind in ((0x030594D4, "bl"), (0x0305A4B0, "b.w")):
        o = site - RAM_OF_IMG
        t0 = bl_target(a, o, site) if kind == "bl" else bw_target(a, o, site)
        t1 = bl_target(b, o, site) if kind == "bl" else bw_target(b, o, site)
        check(t0 == 0x0305C09E and t1 == entl["vol_text"], f"volume text at {site:#x}: {kind} 0x305c09e -> vol_text")
    bs = 0x03050D7C - RAM_OF_IMG
    check(bl_target(a, bs, 0x03050D7C) == 0x0306C3E4 and bl_target(b, bs, 0x03050D7C) == entl["boot_screen"],
          "boot splash 0x3050d7c: bl 0x306c3e4 -> bl boot_screen")
    print("[13] buttons (both players)")
    M45k = 0x2A494
    entk = {k: int(v, 16) for k, v in entries.items()}
    o = M45k + 0x0307A12C - 0x0307A000
    check(a[o:o + 6] == bytes.fromhex("174de8790130") and bl_target(b, o, 0x0307A12C) == 0x0305BA3C
          and b[o + 4:o + 6] == bytes.fromhex("1ee0"),
          "back key tap on Now Playing: play-mode cycle -> bl 0x305ba3c (main menu); b.n 0x307a170")
    for site, (old, sym, text) in {0x0307A1E8: (0x0307AC11, "str_back", b"BACK"),
                                   0x0307A1D8: (0x0307AC07, "str_settings", b"settings")}.items():
        o = M45k + site - 0x0307A000
        new = u32(b, o)
        check(u32(a, o) == old and new == entk[sym] and b[new - RAM_OF_IMG:new - RAM_OF_IMG + len(text) + 1] == text + b"\0",
              f"label literal {site:#x} -> {text.decode()!r}")
    hm = 0x0305BF18 - RAM_OF_IMG
    check(bl_target(a, hm, 0x0305BF18) == 0x0305BA3C and bl_target(b, hm, 0x0305BF18) == entk["hold_menu"],
          "hold menu (0x305bf18) -> hold_menu: Settings on Now Playing, main menu elsewhere")
    body_a, body_b = a[M45k:M45k + u32(a, M45k + 12)], b[M45k:M45k + u32(b, M45k + 12)]
    diff = {k for k in range(len(body_a)) if body_a[k] != body_b[k]}
    ok_sites = set(range(24, 28)) | set(range(0x12C, 0x132)) | set(range(0x1D8, 0x1DC)) | set(range(0x1E8, 0x1EC))
    ok_sites |= set(range(0x1F0, 0x1F4))
    check(diff <= ok_sites, f"m45 changed only at the patch sites and its CRC ({len(diff)} bytes)")

    if ECHO:
        print("[10] full-size Echo stage 1")
        ent = {k: int(v, 16) for k, v in entries.items()}

        def at(addr):
            return addr - RAM_OF_IMG
        for site in (0x0306B9F4, 0x0306C206):
            check(bl_target(a, at(site), site) == 0x0306B000 and bl_target(b, at(site), site) == ent["echo_panel_init"],
                  f"panel init call at {site:#x}: Mini init -> echo_panel_init")
        check(a[at(0x0306B20C):at(0x0306B210)] == bytes([35, 0x35, 35, 0x34])
              and b[at(0x0306B20C):at(0x0306B210)] == bytes([49, 0x35, 49, 0x34]),
              "window rows +35 -> +49 (RECHO at the top of the visible rows, player strip below)")
        check(a[at(0x03051590):at(0x03051594)] == bytes.fromhex("70b50d46")
              and bw_target(b, at(0x03051590), 0x03051590) == ent["gpio_set_echo"],
              "GPIO set entry -> b.w gpio_set_echo")
        tr = ent["gpio_set_orig"]
        check(b[at(tr):at(tr) + 4] == bytes.fromhex("70b50d46") and u32(b, at(tr) + 8) == 0x03051595,
              "trampoline replays the original 4 bytes and returns to 0x3051594")
        for site in (0x03055EE8, 0x03055F6A):
            check(bl_target(a, at(site), site) == 0x03051610 and bl_target(b, at(site), site) == ent["echo_gpio_read"],
                  f"GPIO read at {site:#x} -> echo_gpio_read")
        check(bl_target(a, at(0x03050FBE), 0x03050FBE) == 0x0305444C and b[at(0x03050FBE):at(0x03050FBE) + 4] == bytes.fromhex("012000bf"),
              "FW1 repair call at 0x3050fbe -> movs r0,#1 (repair stays off)")
        check(bl_target(b, at(0x0305090C), 0x0305090C) == 0x03054094, "updater search call intact (updates by file copy)")
        check(a[at(0x0305F3BF):at(0x0305F3BF) + 12] == b"ECHOMINIIMG\0" and b[at(0x0305F3BF):at(0x0305F3BF) + 12] == b"RECHOECHIMG\0"
              and a[at(0x0305F3CB):at(0x0305F3CB) + 7] == b"HIFIEC\0" and b[at(0x0305F3CB):at(0x0305F3CB) + 7] == b"RECHOE\0",
              "update names ECHOMINI.IMG / HIFIECnn.IMG -> RECHOECH.IMG / RECHOEnn.IMG (Echo only)")
        refs = [v for v in pc_literals(b[0xD68:0x273D4], 0x03050000) if v in (0x0305F3BF, 0x0305F3CB)]
        check(len(refs) == 2, "only the updater's name matcher uses those two strings")
        nm = subprocess.run([objdump.replace("objdump", "nm"), os.path.join(root, "build", HOOKS + ".elf")],
                            capture_output=True, text=True).stdout
        tab = int(re.search(r"([0-9a-f]+) r ST7796S", nm).group(1), 16)
        steps, k = [], at(tab)
        while True:
            cmd, ln = b[k], b[k + 1]
            ptr = u32(b, k + 4)
            steps.append((cmd, bytes(b[at(ptr):at(ptr) + ln]) if ln else b""))
            k += 8
            if cmd == 0x21:
                break
        if not PODIUM_PANEL or not os.path.exists(PODIUM_PANEL):
            print("  skip panel init vs Podium: set PODIUM_PANEL to podium-4.6.0/platform/device/logic/panel.c")
            src = None
        else:
            src = open(PODIUM_PANEL, encoding="utf-8").read()
    if ECHO and src:
        start = src.index("ST7796S_STEPS[] = {") + 19
        body = src[start:src.index("STEP_END", start)]
        toks = [t.strip() for t in body.replace("\n", " ").split(",") if t.strip()]
        vals = [0x28 if t == "PANEL_ECHO_MADCTL" else int(t, 0) for t in toks]
        pod, i = [], 0
        while i < len(vals):
            c, n = vals[i], vals[i + 1]
            pod.append((c, bytes(vals[i + 2:i + 2 + n])))
            i += 2 + n
        pod = [x for x in pod if x[0] not in (0x11, 0x29, 0x2A, 0x2B, 0x2C)]
        check(steps == pod, f"panel init in the image == Podium's ST7796S_STEPS ({len(steps)} commands)")
    if ECHO:

        print("[12b] Echo top bars")
        check(bl_target(a, at(0x030594A4), 0x030594A4) == 0x03058D84 and b[at(0x030594A4):at(0x030594A8)] == bytes.fromhex("00bf00bf")
              and a[at(0x030594A8):at(0x030594AC)] == bytes.fromhex("c4f59874") and b[at(0x030594A8):at(0x030594AC)] == bytes.fromhex("4ff49c74"),
              "status bar: battery glyph not drawn (Up Next shows %), volume moves to the right edge")
        print("[11] full-size Echo stage 2 (Up Next sidebar)")
        bufs = [(0x03044000, 160 * 52 * 2, "shared strip"), (0x03048200, 276, "name buffer")]
        for lo, n, what in bufs:
            check(FREE_RAM[0] <= lo and lo + n <= FREE_RAM[1], f"{what} {lo:#x}+{n} inside free RAM {FREE_RAM[0]:#x}..{FREE_RAM[1]:#x}")
        check(bufs[0][0] + bufs[0][1] <= bufs[1][0], "strip and name buffers do not overlap")
        refs = set()
        # Every PC-relative literal load in fw1 and the modules (16-bit ldr and 32-bit ldr.w),
        # decoded at every halfword so nothing is missed, plus every word of the .data image.
        def literal_values(code, base):
            out = []
            for k in range(0, len(code) - 3, 2):
                hw = struct.unpack_from("<H", code, k)[0]
                if hw & 0xF800 == 0x4800:
                    la = ((base + k + 4) & ~3) + (hw & 0xFF) * 4
                elif hw == 0xF8DF or hw == 0xF85F:
                    imm = struct.unpack_from("<H", code, k + 2)[0] & 0xFFF
                    la = ((base + k + 4) & ~3) + (imm if hw == 0xF8DF else -imm)
                else:
                    continue
                o = la - base
                if 0 <= o <= len(code) - 4:
                    out.append(u32(code, o))
            return out
        vals = literal_values(a[0xD68:0x273D4], 0x03050000)
        for m in modules(a):
            vals += literal_values(a[m:m + u32(a, m + 12)], 0x0307A000)
        dimg = a[0x273D4:0x28ED0]
        vals += [u32(dimg, k) for k in range(0, len(dimg) - 3, 4)]
        refs = {v for v in vals if FREE_RAM[0] - 2 <= v < FREE_RAM[1]}
        check(not refs, "no word in RECHO's code or modules points into the free RAM block"
              + ("" if not refs else f" ({sorted(hex(x) for x in refs)[:6]})"))
        # The store must be closed before USB disk mode, so the sidebar's reads stop then.
        close = at(0x0305BC30)
        check(a[close:close + 0x30] == b[close:close + 0x30]
              and u32(a, at(0x0305BC60)) == 0x0303BD74 and u32(a, at(0x0305BC64)) == 0x0303BD80
              and bl_target(a, at(0x0305BC52), 0x0305BC52) == 0x03063DC4,
              "USB-disk entry (0x305BC30, unchanged) clears queue-active and closes the store first")
        check(bl_target(a, at(0x0305118C), 0x0305118C) == 0x0305BC30, "main loop calls it before USB disk mode")
        # Echo hooks must keep the POWER tick wrapper calling both Echo ticks.
        tick = [ln for ln in dis.split("<power_tick_wrap>:")[1].split("\n\n")[0].splitlines() if "bl\t" in ln]
        check(all(any(f"<{f}>" in ln for ln in tick) for f in ("echo_hatch_tick", "echo_sidebar_tick", "echo_band_tick")),
              "POWER tick wrapper calls echo_hatch_tick, echo_sidebar_tick and echo_band_tick")
        cs = [ln for ln in dis.split("<charge_screen>:")[1].split("\n\n")[0].splitlines() if "bl\t" in ln]
        dr = [ln for ln in dis.split("<draw>:")[1].split("\n\n")[0].splitlines() if "bl\t" in ln or "b.w\t" in ln]
        check(any("<echo_panel_fill>" in ln for ln in cs) and any("<echo_flush_centred>" in ln for ln in dr)
              and not any("<lcd_flush>" in ln or "0x306bc70" in ln for ln in dr),
              "charging screen blanks the whole panel and is shown centred (echo_flush_centred, not lcd_flush)")
        init = [ln for ln in dis.split("<echo_panel_init>:")[1].split("\n\n")[0].splitlines() if "bl\t" in ln or "b.w\t" in ln]
        check(any("<echo_sidebar_invalidate>" in ln for ln in init) and any("<echo_panel_fill>" in ln for ln in init),
              "panel init marks the sidebar stale and clears all 480 x 320 of panel memory")

    print()
    if fails:
        print(f"{len(fails)} check(s) FAILED - do not flash")
        sys.exit(1)
    print("all checks passed")


if __name__ == "__main__":
    args = [x for x in sys.argv[1:] if not x.startswith("--")]
    if "--echo" in sys.argv:
        ECHO, HOOKS = True, "hooks-echo"
        PODIUM_PANEL = os.environ.get("PODIUM_PANEL")   # optional: cross-check the panel table
    main(args[0], args[1])
