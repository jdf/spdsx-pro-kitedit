"""
spdsx.py — minimal driver for talking to a Roland SPD-SX PRO over its
USB CDC-ACM serial port using the confirmed DT1 protocol.

Confirmed from live captures:
  frame:    F0 41 10 00 00 00 00 16 12 <6-byte addr> <data...> <cksum> F7
  cmd 0x12: DT1 (write data to address)
  checksum: (128 - (sum(addr+data) mod 128)) & 0x7F      [verified]
  model id: 00 00 00 00 16   device id: 10

  kit num:  value=(kit-1); data = [value>>4, value&0x0F]  [verified 3..200]

NEXT: learn per-parameter addresses (pad link, layer type, level, wave
assign, ...) by changing ONE thing in the app and capturing its DT1 write.
Each new address just gets a helper method like select_kit() below.
"""

import fcntl
import os
import select
import struct
import termios

# ---- transport framing (reverse-engineered) ----------------------------
# Every message on the wire is:
#   0d 60 e0 <ch>  <4B junk>  <4B junk>  01 00 00 00  <len LE32>  <payload>
# hdr[3] is a CHANNEL selector that depends on the message family:
#   0x07 = 41 10 DT1 parameter writes
#   0x09 = 41 6a device control / status / heartbeat
# hdr[4:12] are app-side junk (flags + a heap pointer) the device ignores;
# we send zeros. hdr[12:16] is constant 01 00 00 00.
FRAME_HEAD = bytes.fromhex("0d60e0")  # first 3 bytes, constant
FRAME_TAIL = bytes.fromhex("01000000")
CH_DT1 = 0x07
CH_CONTROL = 0x09
IOSSIOSPEED = 0x80085402  # macOS non-standard baud ioctl (from app capture)
BAUD = 230400
DEFAULT_PORT = "/dev/tty.usbmodem113101"  # the app uses the tty.* node


def channel_for(payload: bytes) -> int:
    if len(payload) >= 3 and payload[1] == 0x41:
        if payload[2] == 0x10:  # DT1 parameter family
            return CH_DT1
        if payload[2] == 0x6A:  # control/status family
            return CH_CONTROL
    return CH_DT1


def wrap(payload: bytes) -> bytes:
    hdr = FRAME_HEAD + bytes([channel_for(payload)]) + b"\x00" * 8 + FRAME_TAIL
    return hdr + struct.pack("<I", len(payload)) + payload


def unwrap(buf: bytes):
    """Pull the payload out of a framed reply (best-effort)."""
    if len(buf) < 20:
        return None
    ln = struct.unpack("<I", buf[16:20])[0]
    return buf[20 : 20 + ln]


DEVICE_ID = 0x10
MODEL_ID = [0x00, 0x00, 0x00, 0x00, 0x16]
DT1 = 0x12
RQ1 = 0x11


def checksum(body):
    """Roland checksum over address+data bytes. Verified against captures."""
    return (128 - (sum(body) % 128)) & 0x7F


def dt1(address, data):
    """Build a DT1 write message. address+data are lists of 7-bit ints."""
    body = list(address) + list(data)
    return bytes(
        [0xF0, 0x41, DEVICE_ID] + MODEL_ID + [DT1] + body + [checksum(body), 0xF7]
    )


def encode_kit(kit):
    """Kit number (1-200) -> two data bytes.
    Confirmed: value=(kit-1), high=value>>4, low=value&0x0F.
    Verified against captures: kit3->00 02, 128->07 0f, 200->0c 07."""
    if not 1 <= kit <= 200:
        raise ValueError("kit must be 1-200")
    v = kit - 1
    return [(v >> 4) & 0x7F, v & 0x0F]


# Address of the "current kit" select parameter, from captures.
KIT_SELECT_ADDR = [0x00, 0x00, 0x00, 0x00, 0x00, 0x00]


class SPDSX:
    """Raw framed transport to the SPD-SX PRO over its tty.* serial node.
    No pyserial: opens the device node directly, sets 230400 baud via the
    macOS IOSSIOSPEED ioctl, and wraps every payload in the device frame."""

    def __init__(self, port=DEFAULT_PORT):
        self.fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        flags = fcntl.fcntl(self.fd, fcntl.F_GETFL)
        fcntl.fcntl(self.fd, fcntl.F_SETFL, flags & ~os.O_NONBLOCK)
        a = termios.tcgetattr(self.fd)
        a[0] = 0
        a[1] = 0
        a[3] = 0  # raw iflag/oflag/lflag
        a[2] |= termios.CLOCAL | termios.CREAD
        if hasattr(termios, "CRTSCTS"):
            a[2] &= ~termios.CRTSCTS
        termios.tcsetattr(self.fd, termios.TCSANOW, a)
        try:
            fcntl.ioctl(self.fd, IOSSIOSPEED, struct.pack("Q", BAUD))
        except Exception as e:
            print("warning: could not set baud:", e)
        termios.tcflush(self.fd, termios.TCIOFLUSH)

    def _read_exact(self, n, deadline):
        import time

        buf = b""
        while len(buf) < n:
            remaining = deadline - time.time()
            if remaining <= 0:
                break
            r, _, _ = select.select([self.fd], [], [], remaining)
            if not r:
                break
            chunk = os.read(self.fd, n - len(buf))
            if not chunk:
                break
            buf += chunk
        return buf

    def read_frame(self, timeout=0.4) -> bytes:
        """Read exactly one framed reply and return its unwrapped payload."""
        import time

        deadline = time.time() + timeout
        hdr = self._read_exact(20, deadline)
        if len(hdr) < 20:
            return b""
        ln = struct.unpack("<I", hdr[16:20])[0]
        if ln <= 0 or ln > 4096:
            return b""
        return self._read_exact(ln, deadline)

    def command(self, payload: bytes, timeout=0.4) -> bytes:
        """Write a framed payload and wait for the device's framed reply.
        Use for messages that DO reply (kit-select, object-focus, reads)."""
        os.write(self.fd, wrap(payload))
        return self.read_frame(timeout)

    def send(self, payload: bytes):
        """Fire-and-forget: frame and write, no reply expected.
        Parameter writes (pad-link etc.) do not ack, so use this for them."""
        os.write(self.fd, wrap(payload))

    def write_param(self, address, data, expect_reply=False, timeout=0.4):
        """Write a DT1 parameter. Parameter writes don't ack by default, so
        this is fire-and-forget unless expect_reply=True."""
        payload = dt1(address, data)
        if expect_reply:
            return self.command(payload, timeout)
        self.send(payload)
        return b""

    def read_reply(self, timeout=0.4) -> bytes:
        return self.read_frame(timeout)

    def ping(self) -> bytes:
        return self.command(
            bytes([0xF0, 0x41, 0x6A, 0x03, 0x16] + [0x00] * 11 + [0xF7])
        )

    def select_kit(self, kit):
        """Navigate to a kit. Replies with current-kit state."""
        return self.command(dt1(KIT_SELECT_ADDR, encode_kit(kit)))

    def select_object(self, kind, index):
        """Focus a pad/trigger for editing. Replies with the object's state.
        Required before a pad-link write is accepted."""
        return self.command(dt1(OBJECT_SELECT_ADDR, [select_value(kind, index)]))

    def set_pad_link(self, kit, kind, index, group, pace=0.02):
        """Assign a pad/trigger to a pad-link group in a SPECIFIC kit.
        Focus (which replies) then the pad-link write (fire-and-forget)."""
        import time

        self.select_object(kind, index)  # replies; drains
        self.send(dt1(pad_link_addr(kind, index, kit), [group & 0x7F]))
        time.sleep(pace)  # small pacing after a no-ack write

    def close(self):
        os.close(self.fd)


# ---- object focus / selection (address 28 00 00 00) --------------------
OBJECT_SELECT_ADDR = [0x28, 0x00, 0x00, 0x00]


def select_value(kind, index):
    """Data byte for the focus write (address 28 00 00 00). CONFIRMED:
      pad N  -> N-1   (pad7 -> 0x06 captured)
      trig N -> 8+N   (trig2->0x0a, trig3->0x0b, trig7->0x0f captured)
    Continuous block: pads 1-9 = 0x00-0x08, triggers 1-8 = 0x09-0x10."""
    if kind == "pad":
        if not 1 <= index <= 9:
            raise ValueError("pad 1-9")
        return index - 1
    elif kind == "trig":
        if not 1 <= index <= 8:
            raise ValueError("trigger 1-8")
        return 8 + index
    raise ValueError("kind must be 'pad' or 'trig'")


# ---- pad-link addressing (kit-encoded) ---------------------------------
# addr = <prefix_hi> <prefix_lo> <object> <param> ; data = group number
#   prefix flat value = 512 + 2*(kit-1), split into two 7-bit bytes:
#       hi = flat >> 7 ; lo = flat & 0x7F
#   object: pad N = 0x1F+N ; trig N = 0x28+N
#   param : 0x0C triggers, 0x0D pads
# Verified against captures on kits 5, 10, 20, and 200 (incl. the 7-bit carry).
# NOTE: what looked like a "scratch" 07 0e prefix earlier was simply kit 200.
def pad_link_prefix(kit):
    if not 1 <= kit <= 200:
        raise ValueError("kit 1-200")
    flat = 512 + 2 * (kit - 1)
    return [(flat >> 7) & 0x7F, flat & 0x7F]


def pad_link_addr(kind, index, kit):
    pre = pad_link_prefix(kit)
    if kind == "pad":
        if not 1 <= index <= 9:
            raise ValueError("pad 1-9")
        return pre + [0x1F + index, 0x0D]
    elif kind == "trig":
        if not 1 <= index <= 8:
            raise ValueError("trigger 1-8")
        return pre + [0x28 + index, 0x0C]
    raise ValueError("kind must be 'pad' or 'trig'")


def bulk_link_all_kits(
    port,
    group=11,
    first=1,
    last=200,
    pairs=(("trig", 7), ("pad", 7)),
    dry_run=True,
    verbose=False,
    pace=0.02,
):
    """Put the given (kind,index) objects into `group` for every kit first..last.
    Per kit: select_kit (replies), then for each object focus (replies) then
    pad-link write (fire-and-forget). select/focus drain their replies to keep
    the half-duplex link in sync; writes don't ack, so they're paced instead."""
    import time

    lines = []
    spd = None if dry_run else SPDSX(port)
    try:
        if spd:
            r = spd.ping()
            lines.append(f"ping ok: {r.hex(' ')}" if r else "ping: NO REPLY")
            if not r:
                raise RuntimeError("device did not respond to ping")
        for kit in range(first, last + 1):
            sel = dt1(KIT_SELECT_ADDR, encode_kit(kit))
            lines.append(f"kit {kit:3d} select   : {sel.hex(' ')}")
            if spd:
                rep = spd.command(sel)
                if verbose:
                    lines.append(f"    <- {rep.hex(' ') if rep else '(no reply)'}")
            for kind, idx in pairs:
                foc = dt1(OBJECT_SELECT_ADDR, [select_value(kind, idx)])
                wr = dt1(pad_link_addr(kind, idx, kit), [group & 0x7F])
                lines.append(f"        focus {kind}{idx} : {foc.hex(' ')}")
                if spd:
                    rep = spd.command(foc)
                    if verbose:
                        lines.append(f"    <- {rep.hex(' ') if rep else '(no reply)'}")
                lines.append(f"        write {kind}{idx} : {wr.hex(' ')}")
                if spd:
                    spd.send(wr)  # fire-and-forget (no ack)
                    time.sleep(pace)
    finally:
        if spd:
            spd.close()
    return lines


if __name__ == "__main__":
    # smoke test: build the exact kit-3 select message we captured and
    # confirm it matches byte-for-byte.
    cases = {
        3: "f0 41 10 00 00 00 00 16 12 00 00 00 00 00 00 00 02 7e f7",
        128: "f0 41 10 00 00 00 00 16 12 00 00 00 00 00 00 07 0f 6a f7",
        129: "f0 41 10 00 00 00 00 16 12 00 00 00 00 00 00 08 00 78 f7",
        130: "f0 41 10 00 00 00 00 16 12 00 00 00 00 00 00 08 01 77 f7",
        131: "f0 41 10 00 00 00 00 16 12 00 00 00 00 00 00 08 02 76 f7",
        200: "f0 41 10 00 00 00 00 16 12 00 00 00 00 00 00 0c 07 6d f7",
    }
    allok = True
    for kit, hexstr in cases.items():
        captured = bytes.fromhex(hexstr.replace(" ", ""))
        built = dt1(KIT_SELECT_ADDR, encode_kit(kit))
        ok = built == captured
        allok &= ok
        print(f"kit {kit:3d}: {'MATCH' if ok else 'MISMATCH'}   {built.hex(' ')}")
    print("\n--- pad-link captures (kit-encoded address) ---")
    padlink = [
        (5, "trig", 7, 3, "f0 41 10 00 00 00 00 16 12 04 08 2f 0c 03 36 f7"),
        (10, "trig", 7, 5, "f0 41 10 00 00 00 00 16 12 04 12 2f 0c 05 2a f7"),
        (20, "trig", 7, 5, "f0 41 10 00 00 00 00 16 12 04 26 2f 0c 05 16 f7"),
        (200, "trig", 7, 1, "f0 41 10 00 00 00 00 16 12 07 0e 2f 0c 01 2f f7"),
        (200, "pad", 7, 11, "f0 41 10 00 00 00 00 16 12 07 0e 26 0d 0b 2d f7"),
    ]
    for kit, kind, idx, grp, hexstr in padlink:
        cap = bytes.fromhex(hexstr.replace(" ", ""))
        built = dt1(pad_link_addr(kind, idx, kit), [grp])
        ok = built == cap
        allok &= ok
        print(
            f"kit {kit:3d} {kind}{idx} grp{grp:2d}: {'MATCH' if ok else 'MISMATCH'}   {built.hex(' ')}"
        )

    print("\n--- your task: trig7 & pad7 -> group 11, sample kits ---")
    for kit in (1, 50, 200):
        for kind, idx in (("trig", 7), ("pad", 7)):
            print(
                f"  kit {kit:3d} {kind}{idx}:",
                dt1(pad_link_addr(kind, idx, kit), [11]).hex(" "),
            )

    print("\nALL MATCH" if allok else "\nSOME MISMATCH")
