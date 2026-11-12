"""Inspect, verify and re-seal RKnanoFW 5 MiB images (RECHO / EchoPod / ReChord family).

Usage:
  py -3 -I rkimg.py info  IMAGE
  py -3 -I rkimg.py seal  IMAGE   (rewrites header self-copy and CRC trailer in place)
"""
import struct
import sys

POLY = 0x04C10DB7
RAM_OF_IMG = 0x0304F298  # RAM address = RAM_OF_IMG + IMG offset (fw1 region)


def _table():
    t = []
    for i in range(256):
        v = i << 24
        for _ in range(8):
            v = ((v << 1) ^ POLY) if v & 0x80000000 else (v << 1)
            v &= 0xFFFFFFFF
        t.append(v)
    return t


T = _table()


def crc32(data):
    v = 0
    for b in data:
        v = ((v << 8) & 0xFFFFFFFF) ^ T[(v >> 24) ^ b]
    return v


def u32(d, o):
    return struct.unpack_from("<I", d, o)[0]


def fw_end(fw_size):
    return (fw_size + 0x104000) // 0x100000 * 0x100000


def parts(d):
    hdr_off, count = u32(d, 0x70), u32(d, 0x74)
    out = []
    for i in range(min(count, 32)):
        off, size = u32(d, 0x78 + 8 * i), u32(d, 0x7C + 8 * i)
        out.append((off, size))
    return hdr_off, out


def info(d):
    fw_size = u32(d, 0x1F4)
    end = fw_end(fw_size)
    print(f"brand   {d[0x10:0x18].rstrip(b'\0').decode('latin1')!r}  tag {d[0x30:0x38].rstrip(b'\0').decode('latin1')!r}")
    print(f"version word {u32(d, 4):#010x} patch {u32(d, 8)}  magic {u32(d, 0xC):#x}")
    print(f"fw_size {fw_size:#x}  crc at {end:#x}")
    stored = u32(d, end)
    calc = crc32(d[:end])
    print(f"crc stored {stored:#010x} calc {calc:#010x} {'OK' if stored == calc else 'MISMATCH'}")
    copy_ok = d[fw_size:fw_size + 0x200] == d[:0x200]
    print(f"header self-copy at {fw_size:#x}: {'OK' if copy_ok else 'MISMATCH'}")
    h, ps = parts(d)
    print(f"part header @ {h:#x}")
    for i, (o, s) in enumerate(ps):
        print(f"  part {i}: {o:#x}..{o + s:#x} ({s} B)  starts {d[o:o + 16]!r}")
    return stored == calc and copy_ok


def seal(path):
    d = bytearray(open(path, "rb").read())
    fw_size = u32(d, 0x1F4)
    d[fw_size:fw_size + 0x200] = d[:0x200]
    end = fw_end(fw_size)
    struct.pack_into("<I", d, end, crc32(d[:end]))
    open(path, "wb").write(d)
    print(f"sealed {path}: crc {u32(d, end):#010x}")


if __name__ == "__main__":
    cmd, path = sys.argv[1], sys.argv[2]
    if cmd == "info":
        sys.exit(0 if info(open(path, "rb").read()) else 1)
    elif cmd == "seal":
        seal(path)
