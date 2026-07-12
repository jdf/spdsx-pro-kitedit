#!/usr/bin/env python3
"""
frametest.py — determine whether the device accepts a framed message from us,
and which header fields it actually requires.

Opens /dev/tty.usbmodem113101 at 230400 (matching the app's setup, incl. the
IOSSIOSPEED ioctl), then sends the KNOWN-GOOD heartbeat payload wrapped in
several header variants, listening for a reply after each.

    python3 frametest.py [/dev/tty.usbmodem113101]

Interpretation:
  * Some variant gets a reply  -> transport cracked; that header works.
  * Only the exact-replay works -> hdr[8:12] is a real session token; we must
                                   capture the connect handshake.
  * Nothing replies            -> baud/node/framing still off; report output.
"""

import os, sys, time, termios, fcntl, select, struct

PORT = sys.argv[1] if len(sys.argv) > 1 else "/dev/tty.usbmodem113101"
IOSSIOSPEED = 0x80085402  # from the app's captured ioctl (T,2,speed_t 8B)

# The heartbeat payload the device answers constantly:
HEARTBEAT = bytes([0xF0, 0x41, 0x6A, 0x03, 0x16] + [0x00] * 11 + [0xF7])  # 17 bytes


def frame(hdr4to8, hdr8to12, payload):
    hdr = bytes.fromhex("0d60e009") + hdr4to8 + hdr8to12 + bytes.fromhex("01000000")
    return hdr + struct.pack("<I", len(payload)) + payload


VARIANTS = {
    "zeros[4:8]&[8:12]": frame(b"\x00\x00\x00\x00", b"\x00\x00\x00\x00", HEARTBEAT),
    "ZTUM, zero handle": frame(b"ZTUM", b"\x00\x00\x00\x00", HEARTBEAT),
    "stale-replay      ": frame(b"ZTUM", bytes.fromhex("00e03a02"), HEARTBEAT),
}


def open_port():
    fd = os.open(PORT, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    flags = fcntl.fcntl(fd, fcntl.F_GETFL)
    fcntl.fcntl(fd, fcntl.F_SETFL, flags & ~os.O_NONBLOCK)
    a = termios.tcgetattr(fd)
    a[0] = 0
    a[1] = 0
    a[3] = 0
    a[2] |= termios.CLOCAL | termios.CREAD
    if hasattr(termios, "CRTSCTS"):
        a[2] &= ~termios.CRTSCTS
    termios.tcsetattr(fd, termios.TCSANOW, a)
    # set 230400 via IOSSIOSPEED (macOS non-standard baud)
    try:
        fcntl.ioctl(fd, IOSSIOSPEED, struct.pack("Q", 230400))
    except Exception as e:
        print("  (IOSSIOSPEED failed:", e, ")")
    termios.tcflush(fd, termios.TCIOFLUSH)
    return fd


def main():
    print(f"port {PORT}, baud 230400")
    fd = open_port()
    try:
        for name, msg in VARIANTS.items():
            termios.tcflush(fd, termios.TCIFLUSH)
            os.write(fd, msg)
            r, _, _ = select.select([fd], [], [], 0.6)
            if r:
                data = os.read(fd, 512)
                print(f"  [{name}] -> REPLY {len(data)}B: {data.hex(' ')}")
            else:
                print(f"  [{name}] -> <nothing>")
            time.sleep(0.2)
    finally:
        os.close(fd)


if __name__ == "__main__":
    main()
