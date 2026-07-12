# spdsx-patchedit — orientation for Claude

Interactive kit designer for the Roland SPD-SX PRO. C++20, JUCE 8, CMake +
vcpkg. Public repo: github.com/jdf/spdsx-pro-kitedit (note: repo name differs
from this directory). Push with jdf's `jj push-main` alias.

## Conventions (STRICT — jdf cares)
- **Google C++ style throughout**: `.cc`/`.h` files, `#ifndef SPDSX_PATCHEDIT_SOURCE_<FILE>_H_` guards (never `#pragma once`), UpperCamelCase methods (trivial accessors like `name()`/`set_name()` may be snake_case), `kUpperCamelCase` enumerators/constants, `snake_case_` data members. JUCE overrides keep their inherited names.
- **jj (not git) for VCS**, colocated. Commit when asked; make commits self-contained (each builds and works). Advance `main` bookmark manually, then `jj push-main`. Co-author trailer: `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`.
- Also honored in sibling repos: `github.com/jdf/specgram` (the spectrogram lib, dir `../specgram`) and `github.com/jdf/spdsx-py` (`../spdsx-py`, the protocol RE).

## Build / run
```
cmake --preset default && cmake --build --preset default
./build/spdsx-patchedit_artefacts/RelWithDebInfo/spdsx-patchedit.app/Contents/MacOS/spdsx-patchedit
```
Targets: the GUI app; `link_all_kits` (device-protocol CLI: `--selftest` / `--dry-run`); `spdsx_device` (JUCE-free static lib). Preset is RelWithDebInfo (breakpoints). VS Code: open `../hax.code-workspace` (multi-root); `--load <slot> <wav>` pre-fills slots for testing.

## Architecture (source/)
- `kit_model.{h}` — **source of truth**: name + 9 `Pad`s, each a top/bottom sample pair. Observable (`KitModel::Listener`). UI/engine/document all react to it.
- `actions.h` — `SetSampleAction` (undoable). `main_component` wraps edits in `undo_` transactions (Load, MoveSample, MovePad).
- `kit_document.{h,cc}` — `KitDocument : FileBasedDocument`, versioned JSON `.kit` (`KitFormat` enum; `pads` array; absolute sample paths; legacy flat-`slots` still loads; loading clears undo history).
- `main_component.{h,cc}` — the grid, undo, File/Edit/View menus (via ApplicationCommandManager; `main.cc` must call `setApplicationCommandManagerToWatch` or menu enablement goes stale), MIDI in (ch 10 notes 60-68 -> pads), drag (move/copy one slot; ⌘ = whole pad; ⌥ = duplicate).
- `sample_slot.{h,cc}` — a slot: spectrogram + info bar + transport buttons; drag source + FileDragAndDropTarget + DragAndDropTarget; hover highlight driven by parent (`on_drag_target`/`SetDragTarget`).
- `sample_browser.{h,cc}` — left panel FileTree; drags onto slots; autoplay (View menu, persisted).
- `audio.{h,cc}` — AudioEngine (per-slot transports + a preview channel), miniaudio-free (pure JUCE).
- `spectro.{h,cc}` — renders spectrogram thumbnails via specgram (consumed through the vcpkg overlay port in `ports/specgram`, pinned to a `github.com/jdf/specgram` sha; bump REF there after pushing specgram).
- `device/` — SPD-SX PRO serial protocol (JUCE-free): `protocol` (DT1 message building, addresses, nibble codec), `serial_port` (termios + macOS IOSSIOSPEED), `spdsx_device` (frame wrap/unwrap + ops). See `link_all_kits --selftest`.

## Verification pattern (used constantly)
No test target. To verify runtime logic: add a temporary `if (std::getenv("SPDSX_SELFTEST")) { juce::Timer::callAfterDelay(...) { ...checks with PASS/FAIL printf...; std::fflush(stdout); std::_Exit(0); } }` block in the MainComponent ctor, run with `SPDSX_SELFTEST=../specgram/test.wav <app> 2>/dev/null`, then REMOVE before commit. (`_Exit` avoids the quit save-dialog hang; stdout must be flushed.) For device protocol, `link_all_kits --selftest` checks message bytes against captures.

## Device reverse-engineering (the big ongoing thread)
NOT MIDI — it's Roland DT1 SysEx wrapped in a proprietary transport frame over USB CDC-ACM serial (230400 baud). See `[[project-spdsx-patchedit]]` memory for the full protocol map. Cracked so far: kit select, object focus, pad-link, kit name (read+write), pad wave assignment (read+write, nibble-encoded). Device state = ~8MB bulk block transfer on the `41 6c` family, banked (0x10 = kit/settings ~1.4MB with 3528-byte-per-kit records; 0x20 = samples ~6.5MB). Method to map a parameter: dump image, change ONE thing in the official app, dump again, diff. Capture + RE tooling lives in **`re/`** (frida capture scripts, `parse_capture.py`, and the original `spdsx.py`/`link_all_kits.py` Python reference this C++ was ported from; see `re/README.md`). frida at `~/frida-env/bin/frida`, SIP disabled; keep attached via `tail -f /dev/null | frida -f <app> -l <script>`. An offline ~8MB device image is cached at **`../re-cache/device-image.bin`** with a memo — the source for all documented storage offsets. jdf drives the GUI + hardware for captures. (Also mirrored at github.com/jdf/spdsx-py.)

**Live transport VERIFIED** (2026-07-11): `link_all_kits --probe --port /dev/cu.usbmodem1101` opened the port and pinged the real device, which replied byte-identically to the frida capture (`f0 41 6a 02 ... 16 40 00 00 00 04 01 00 00 00 f7`). So the whole C++ transport stack (serial open + baud ioctl, frame wrap/unwrap, round-trip) works against hardware. The `/dev/cu.usbmodem<N>` node number changes every replug — there is no stable path; with no `--port`, link_all_kits scans `/dev/cu.usbmodem*` and pings each node until the device answers. Close the official app first (one program per port). Read paths (bulk `6c 03` block reads) and write paths (SetKitName/SetPadWave) are still unexercised live — probe/ping is read-only.

## Open items
- Bulk-read (`6c 03 05` block requests) and the write ops (SetKitName/SetPadWave) are not yet implemented/driven live from C++ — probe only pings.
