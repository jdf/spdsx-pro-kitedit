# re/ — SPD-SX PRO reverse-engineering tooling

The scripts that reverse-engineered the device's USB serial protocol, kept
next to the C++ implementation they produced (`../source/device/`). Originally
the standalone repo github.com/jdf/spdsx-py; consolidated here now that the
protocol lives in this project.

**Not built** (CMake ignores this dir). Python 3 stdlib + [frida](https://frida.re) only.

## Reference implementation (ported to C++)
- `spdsx.py` — the original Python driver: DT1 framing, checksum, kit-select,
  object-focus, pad-link addressing. Reimplemented in `../source/device/`
  (`protocol`, `serial_port`, `spdsx_device`). Its `__main__` has byte-exact
  message cases — the same ones `link_all_kits --selftest` now checks.
- `link_all_kits.py` — original CLI; reimplemented as the C++ `link_all_kits`
  target (with `--selftest` / `--probe`).

## Live capture (frida) — `frida-scripts/`
Hook the official **SPD-SX PRO App**'s serial I/O while you drive it. See
`frida-scripts/README.md`. frida is at `~/frida-env/bin/frida` (SIP is
disabled). It exits if backgrounded plainly — keep it attached with:
```
tail -f /dev/null | ~/frida-env/bin/frida -f "/Applications/Roland/SPDSXPROAPP.app/Contents/MacOS/SPDSXPROAPP" -l frida-scripts/<script>.js
```
- `statelog.js` — both directions (read+write) of a full "load current state"
  dump; the workhorse for state RE.
- `dt1log.js` / `editlog.js` — outgoing DT1 parameter writes (editlog filters
  the heartbeat poll).
- `portlog.js` — how the app opens/configures the port. `framelog.js` — the
  transport frame header. `sniff.js` — locate DT1 payloads.
- `parse_capture.py` — reassemble a capture log's `41 6c 02` blocks into an
  `.image.bin`, and `--diff` two images to locate a changed parameter. The RE
  loop: dump A → change ONE thing in the app → dump B → diff.
- `debugging_scripts/` — misc probes (`diag.py`, `frametest.py`, `rawwrite.py`).

## Data
A full ~8 MB device image (the source of every documented storage offset) is
cached at `../../re-cache/device-image.bin` with its own memo. The protocol map
and current frontier are in `../CLAUDE.md`.
