#!/usr/bin/env python3
"""Turn a Podium diagnostic dump (/BTDUMP.BIN) into text, in the layout of the mod's "bt: dump" lines.

    py -3 -I tools/re/re_btdump.py BTDUMP.BIN > podium-dump.txt

The dump comes from a Podium 4.6.1 build with one addition (dac_path_write_dump in board/dac.c,
called 5 s into the first Bluetooth stream): little-endian words "BTD1", GRF 0x000-0x0FC, CRU
0x000-0x0FC, GPIO0/1/2 data, direction and input, I2S1 0x00-0x3C, ACODEC 0x200-0x21C, then one
byte per DAC register for the chips at 0x60 and 0x66 (0xEE = read failed). It records the state of
a player that streams to Bluetooth, to compare with the same registers under RECHO
(docs/12-bluetooth.md).
"""
import struct
import sys

DAC_MAPS = [0x010001, 0x010002, 0x010003, 0x010004, 0x010006, 0x01000C, 0x01000D, 0x010010,
            0x020000, 0x040010, 0x040011, 0x040012, 0x040013, 0x040014, 0x040015, 0x040016,
            0x040017, 0x040018, 0x040019, 0x050000, 0x050001, 0x05000A, 0x05000B, 0x080000,
            0x090000, 0x090001, 0x090002, 0x090003, 0x090004, 0x0D0000, 0x0D0001, 0x0F0010,
            0x0F0011, 0x180000]


def main():
    d = open(sys.argv[1], "rb").read()
    words = struct.unpack_from("<%dI" % 162, d, 0)
    if words[0] != 0x31445442:
        raise SystemExit("not a BTD1 dump")
    w = list(words[1:])

    def block(name, count):
        vals, w[:] = w[:count], w[count:]
        for o in range(0, count, 8):
            print("dump %s+%02x %s" % (name, o * 4, " ".join("%08x" % v for v in vals[o:o + 8])))

    block("GRF", 64)
    block("CRU", 64)
    for g in range(3):
        data, direction, level = w[0], w[1], w[2]
        w[:] = w[3:]
        print("dump GPIO%d data %08x dir %08x in %08x" % (g, data, direction, level))
    block("I2S1", 16)
    block("ACODEC+200", 8)
    dac = d[162 * 4:162 * 4 + 68]
    for c, addr in enumerate((0x60, 0x66)):
        for i, m in enumerate(DAC_MAPS):
            v = dac[c * 34 + i]
            print("DAC %02x %06x %s" % (addr, m, "??" if v == 0xEE else "%02x" % v))


if __name__ == "__main__":
    main()
