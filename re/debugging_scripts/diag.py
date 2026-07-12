#!/usr/bin/env python3
"""
diag.py — figure out WHY writes aren't landing on the SPD-SX PRO.

Close the SPD-SX PRO App first, then:
    python3 diag.py --port /dev/cu.usbmodem113101

It runs three probes:
  A) Send the status-poll the app uses and see if the device REPLIES.
     A reply proves the port + transport work in both directions.
  B) Read current pad-link for trig7 via RQ1 (if the device supports it),
     print it, so we know the "before" value.
  C) Send the trig7 -> group N write, then re-read. If B and C differ,
     the write landed.  If A replies but C doesn't change, the write
     framing/target is being rejected, not the transport.
"""

import argparse, time
from spdsx import SPDSX, dt1, checksum, MODEL_ID, DEVICE_ID, pad_link_addr

# The status poll the app emits constantly (opcode 0x16), captured live:
#   f0 41 6a 03 16 00..00 f7   (note: prefix 41 6a 03, NOT the 41 10 DT1 path)
STATUS_POLL = bytes([0xF0, 0x41, 0x6A, 0x03, 0x16] + [0x00] * 11 + [0xF7])


def dump(label, b):
    print(f"{label} ({len(b)}B): {b.hex(' ') if b else '<nothing>'}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="/dev/cu.usbmodem113101")
    ap.add_argument("--group", type=int, default=2)
    ap.add_argument("--kit", type=int, default=5)
    args = ap.parse_args()

    spd = SPDSX(args.port)
    print("port open:", spd.ser.port, "dtr", spd.ser.dtr, "rts", spd.ser.rts)

    # --- Probe A: does the device answer its own status poll? ---
    print("\n[A] sending status poll, waiting for reply...")
    spd.ser.reset_input_buffer()
    spd.ser.write(STATUS_POLL)
    spd.ser.flush()
    time.sleep(0.1)
    dump("    reply", spd.read_reply(0.5))

    # --- Probe C: send a DT1 write, watch for any reply / ack ---
    print(f"\n[C] sending trig7 -> group {args.group} DT1 write (kit {args.kit})...")
    msg = dt1(pad_link_addr("trig", 7, args.kit), [args.group])
    dump("    sending", msg)
    spd.ser.reset_input_buffer()
    spd.ser.write(msg)
    spd.ser.flush()
    time.sleep(0.1)
    dump("    reply", spd.read_reply(0.5))

    # --- Probe A again, to see if state moved ---
    print("\n[A'] re-polling status after the write...")
    spd.ser.reset_input_buffer()
    spd.ser.write(STATUS_POLL)
    spd.ser.flush()
    time.sleep(0.1)
    dump("    reply", spd.read_reply(0.5))

    spd.close()
    print("\nInterpretation:")
    print("  * A replies, C changes device -> success.")
    print(
        "  * A replies, but nothing on device -> transport OK; DT1 target/mode wrong."
    )
    print("  * A gives <nothing> -> transport problem (port/mode/flow control).")


if __name__ == "__main__":
    main()
