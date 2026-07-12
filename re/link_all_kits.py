#!/usr/bin/env python3
"""
link_all_kits.py — put Trigger 7 and Pad 7 into pad-link group 11 for every
kit (1-200) on a Roland SPD-SX PRO, over its USB serial port.

The pad-link address encodes the kit number, so each write targets its kit
directly — no kit-select or on-device navigation needed.

Requires: pip install serial ; and spdsx.py in the same folder.

USAGE (in order):
  1. Dry run — prints every message, sends nothing:
       python3 link_all_kits.py --dry-run
  2. Single-kit live test — write ONE kit, then check it on the device
     (open that kit's pad-link screen; confirm trig7 + pad7 = group 11):
       python3 link_all_kits.py --port /dev/cu.usbmodem113101 --only 5
  3. Full run once the single-kit test looks right:
       python3 link_all_kits.py --port /dev/cu.usbmodem113101

Notes:
  * Close the SPD-SX PRO App first — two programs must not share the port.
  * Use the /dev/cu.usbmodem* node (callout), not /dev/tty.usbmodem*.
  * BACK UP the unit to a USB stick before the full run — it writes all 200 kits.
"""

import argparse
import sys
from spdsx import bulk_link_all_kits


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="/dev/cu.usbmodem113101")
    ap.add_argument("--group", type=int, default=11)
    ap.add_argument("--only", type=int, help="run a single kit number only")
    ap.add_argument("--first", type=int, default=1)
    ap.add_argument("--last", type=int, default=200)
    ap.add_argument(
        "--dry-run", action="store_true", help="print messages, send nothing"
    )
    ap.add_argument(
        "--verbose",
        action="store_true",
        help="print the device's reply after each command",
    )
    args = ap.parse_args()

    if args.only:
        first, last = args.only, args.only
    else:
        first, last = args.first, args.last

    if not args.dry_run:
        scope = f"kit {args.only}" if args.only else f"kits {first}-{last}"
        print(
            f"About to WRITE to {scope} on {args.port} "
            f"(trig7 + pad7 -> group {args.group})."
        )
        if input("Type 'yes' to proceed: ").strip().lower() != "yes":
            print("Aborted.")
            sys.exit(1)

    lines = bulk_link_all_kits(
        args.port,
        group=args.group,
        first=first,
        last=last,
        pairs=(("trig", 7), ("pad", 7)),
        dry_run=args.dry_run,
        verbose=args.verbose,
    )

    for ln in lines:
        print(ln)
    print(
        f"\n{'(dry run — nothing sent)' if args.dry_run else 'Done.'}  "
        f"{len(lines)} messages."
    )


if __name__ == "__main__":
    main()
