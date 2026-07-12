#!/usr/bin/env python3
"""Reassemble the SPD-SX PRO device-memory image from a statelog capture, and
diff two images. "Load current state" streams ~8 MB as 64 KB blocks on the
41 6c 02 family (channel 0x08); the way to locate a parameter is to dump,
change one thing in the app, dump again, and diff.

  python3 parse_capture.py <capture.log>            # -> <capture>.image.bin
  python3 parse_capture.py --diff <a.bin> <b.bin>   # changed byte ranges
  python3 parse_capture.py --writes <capture.log>   # non-poll writes the app sent
"""
import re, sys, collections


def read_stream(path, direction):
    out = bytearray()
    lines = open(path).read().splitlines()
    for i, ln in enumerate(lines):
        if direction in ln and i + 1 < len(lines):
            hx = lines[i + 1].strip()
            if re.fullmatch(r'[0-9a-f ]+', hx or ''):
                out += bytes(int(x, 16) for x in hx.split())
    return bytes(out)


def frames(stream, head):
    """Parse transport frames: <head> <ch> <8 junk> <4 tail> <len LE32> <payload>."""
    i, out = 0, []
    while i + 20 <= len(stream):
        if stream[i:i + 3] == head:
            ch = stream[i + 3]
            ln = int.from_bytes(stream[i + 16:i + 20], 'little')
            out.append((ch, bytes(stream[i + 20:i + 20 + ln])))
            i += 20 + ln
        else:
            i += 1
    return out


def image(path):
    """Concatenate the 6c-02 block payloads into the raw device image."""
    fr = frames(read_stream(path, '<< READ'), b'\x0d\xe0\x60')
    blocks = [p for ch, p in fr
              if ch == 0x08 and len(p) >= 4 and p[2] == 0x6c and p[3] == 0x02]
    return b''.join(blocks), len(blocks)


def writes(path):
    """Non-poll payloads the app sent (the change commands we care about)."""
    fr = frames(read_stream(path, '>> WRITE'), b'\x0d\x60\xe0')
    out = []
    for ch, p in fr:
        if len(p) >= 4 and p[1] == 0x41 and not (
                p[2] == 0x6a and p[3] == 0x03 and all(b == 0 for b in p[5:-1])):
            out.append((ch, p))
    return out


def diff(a_path, b_path):
    a = open(a_path, 'rb').read()
    b = open(b_path, 'rb').read()
    print(f"a: {len(a)} bytes   b: {len(b)} bytes")
    n = min(len(a), len(b))
    # Coalesce differing offsets into ranges.
    ranges, start = [], None
    for i in range(n):
        if a[i] != b[i]:
            if start is None:
                start = i
        elif start is not None:
            ranges.append((start, i)); start = None
    if start is not None:
        ranges.append((start, n))
    print(f"{len(ranges)} changed range(s):")
    for s, e in ranges[:60]:
        print(f"  @0x{s:06x}..0x{e:06x} ({e-s}B)  "
              f"a=[{a[s:e][:16].hex(' ')}]  b=[{b[s:e][:16].hex(' ')}]")
    if len(a) != len(b):
        print(f"NOTE: images differ in length ({len(a)} vs {len(b)}) — "
              "block structure may not be byte-aligned across dumps.")


if __name__ == "__main__":
    if sys.argv[1] == "--diff":
        diff(sys.argv[2], sys.argv[3])
    elif sys.argv[1] == "--writes":
        for ch, p in writes(sys.argv[2]):
            print(f"ch=0x{ch:02x}  {p.hex(' ')}")
    else:
        log = sys.argv[1]
        img, nblocks = image(log)
        out = log.rsplit('.', 1)[0] + ".image.bin"
        open(out, 'wb').write(img)
        print(f"{nblocks} blocks -> {len(img)} bytes  saved: {out}")
