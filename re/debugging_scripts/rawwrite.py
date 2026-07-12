#!/usr/bin/env python3
"""
rawwrite.py — minimal, low-level probe. No pyserial. Opens the device node
raw via termios, optionally toggles modem lines, sends the status poll the
device is KNOWN to answer, and reports whether anything comes back.

    # list all candidate serial nodes:
    python3 rawwrite.py --list

    # probe a specific node:
    python3 rawwrite.py /dev/cu.usbmodem113101

If one node gives <nothing>, TRY THE OTHERS the --list shows. The SPD-SX PRO
exposes more than one USB interface; the app may use a different node than the
first one.
"""

import os, sys, time, termios, glob, fcntl


def list_ports():
    nodes = sorted(
        set(glob.glob("/dev/cu.usbmodem*") + glob.glob("/dev/tty.usbmodem*"))
    )
    if not nodes:
        print("no usbmodem nodes found — is the device on and connected?")
    for n in nodes:
        print("  ", n)
    return nodes


def probe(port):
    print(f"\n=== probing {port} ===")
    poll = bytes([0xF0, 0x41, 0x6A, 0x03, 0x16] + [0x00] * 11 + [0xF7])
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    try:
        # clear O_NONBLOCK for the reads after open
        flags = fcntl.fcntl(fd, fcntl.F_GETFL)
        fcntl.fcntl(fd, fcntl.F_SETFL, flags & ~os.O_NONBLOCK)

        a = termios.tcgetattr(fd)
        # cfmakeraw-equivalent
        a[0] = 0  # iflag
        a[1] = 0  # oflag
        a[3] = 0  # lflag
        a[2] |= termios.CLOCAL | termios.CREAD  # ignore modem ctrl, enable rx
        if hasattr(termios, "CRTSCTS"):
            a[2] &= ~termios.CRTSCTS  # no hw flow control
        termios.tcsetattr(fd, termios.TCSANOW, a)
        termios.tcflush(fd, termios.TCIOFLUSH)

        os.write(fd, poll)
        # blocking read with timeout via select
        import select

        r, _, _ = select.select([fd], [], [], 0.6)
        if r:
            data = os.read(fd, 512)
            print("  REPLY:", data.hex(" ") if data else "<empty>")
        else:
            print("  REPLY: <nothing within 0.6s>")
    finally:
        os.close(fd)


if __name__ == "__main__":
    if len(sys.argv) < 2 or sys.argv[1] == "--list":
        print("candidate serial nodes:")
        nodes = list_ports()
        if len(sys.argv) >= 2 and sys.argv[1] == "--list":
            sys.exit(0)
        # if no arg, probe all cu.* nodes
        for n in nodes:
            if n.startswith("/dev/cu."):
                try:
                    probe(n)
                except Exception as e:
                    print(f"  {n}: error {e}")
    else:
        probe(sys.argv[1])
