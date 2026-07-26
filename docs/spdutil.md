# spdutil — SPD-SX PRO command-line utility

`spdutil` talks to a Roland SPD-SX PRO over its USB serial port using the
same reverse-engineered protocol as the SPD-SX PROgram app. It reads and
writes device state directly: kits, pad parameters, layer settings, and the
sample pool (including audio upload/download). It is also the repro and
verification tool the protocol work itself is driven with.

```
spdutil [--port <dev>] <command> [options]
spdutil --version
```

`--version` prints the SPD-SX PROgram version this tool was built with.

Built by the normal CMake build; the binary lands at `build/spdutil`. It
also ships inside the app bundle, and the app menu's **Install
Command-Line Tool** symlinks it to `/usr/local/bin/spdutil`.

## Connecting

- With no `--port`, spdutil scans `/dev/cu.usbmodem*` and pings each node
  until the device answers. The node number changes on every replug, so this
  is the normal way to run it.
- **Close the official SPD-SX PRO App first** — one program per port.
- The SPD-SX PROgram app can stay open: it only holds the port during an
  active device operation, and its 2-second connection poll retries around
  short collisions.

## Flags are checked against the command

Every flag is rejected by any command that would not act on it, rather than
parsed and dropped. `spdutil ping --dry-run` and `spdutil setname --range
1-5 --name X` are errors, and so is a bare number for a command that takes
none (`spdutil kits 5`). This exists because a silently ignored flag caused
a real accident: `setmode --pad 9` swept all nine pads, and `setmode 108`
swept all 200 kits.

`--dry-run` is honored by `assign`, `setname`, `setparams`, `setlayer`,
`setmode`, and `padlink`; the other write commands reject it rather than
pretend.

## Working state vs. commit

Device writes land in **working state**: audible immediately, gone on power
cycle. Write commands take `--commit` to follow the writes with a flash
commit (the same `6a 21`/`22` handshake the official app's WRITE button
uses), which makes them durable. Uncommitted edits accumulate across
separate spdutil invocations, and one final `--commit`-ing command flushes
everything staged. `deletewave` and `sendwave` always commit — a delete or
an upload left half-done in working state is a trap, not a feature.

## Read-only commands

### `ping`
Opens the port and pings the device; prints the raw reply. The cheapest
"is anything there" check.

### `info`
Port path, ping status, and the firmware version query
(e.g. `version: 2.00 (build 0094)`).

### `currentkit`
Prints the device's **active** kit (1-200). Read by streaming only the head
of the kits bank, so it is sub-second.

### `kits [--from FILE]`
Lists all 200 kit names — live from the device, or offline from a saved
`dump` image via `--from`.

### `kit <N> [--from FILE]`
Shows kit N's per-pad parameters as a table: layer mode, fade point/end,
dynamics + curve, fixed velocity, trigger reserve, the hi-hat closed-pedal
trio, and each pad's top/bottom wave assignment. Live or `--from` a dump.

### `samples [--from FILE]`
Lists the device wave pool from the bank 0x20 sample directory: index,
wavename, category, duration, filename. **Directory only** — the state dump
carries no audio; use `readwave` for that.

### `dump`
Streams device memory banks to an image file.

| option | meaning |
| --- | --- |
| `--bank 0xNN` | one bank (repeatable): `0x10` kits, `0x20` sample directory, `0x30` active-kit mirror/meta, `0x40` config |
| `--all` | all four banks |
| `--out FILE` | write the reassembled image |
| `--verify FILE` | offline: report an existing image's block structure |

With neither `--bank` nor `--all`, dumps the kits bank. The images are what
`kits`/`kit`/`samples --from` read, and what the protocol RE diffs against.

### `readwave <N> [--out FILE]`
Reads wave N's audio off the device over the remote-file protocol and
reports its RFWV header (rate, channels, bits, duration). With `--out`, a
`.wav` path gets a converted WAV; any other path gets the raw `.SMP`.
Some factory preloads have no exportable file and fail cleanly.

## Kit and pad writes

`setname`, `assign`, `setparams`, and `setlayer` take a **required** kit
number `<K>`, plus `--commit` and `--dry-run`. `setmode` and `padlink`
take `<K>` or `--range` instead, also required. Nothing is implied: a
command that writes to kits will not guess which, because omitting the
kit used to mean kit 1 (or, for the sweeps, all 200). `selectkit` is its
own shape, below.

### `selectkit <N>`
Switches the device's playback kit (1-200). Instant, not a stored edit —
nothing to commit.

### `setname [K] --name TEXT`
Sets kit K's name (16 characters, space-padded/truncated).

### `assign [K] --sample N --pad P.S`
Assigns pool sample N to a pad layer. `--pad P.S` is pad 1-9 dot slot
(0 = top layer, 1 = bottom): `--pad 2.1` is pad 2's bottom layer.
Sample 0 clears the layer.

### `setparams [K] --pad N --params ...`
Writes one pad's ten hit-response parameters as a comma list, in order:
`mode,fadePoint,fadeEnd,dynamics,curve,fixedVel,hhVol,hhFadeIn,hhDecay,trigRsv`.

### `setlayer [K] --pad P.S [--volume dB] [--fadein N] [--decay N]`
Writes one layer's mix trio: volume in dB (e.g. `--volume -3.5`; stored in
0.1 dB steps), fade-in 0-127, decay 0-127 (127 = none). Options you leave
out keep their current values — the command reads the kit first to fill
them in, so naming only `--fadein` costs a bank read but changes nothing
else. (Before 2026-07-26 the unnamed ones were silently overwritten with
0.0 dB / 0 / 127.)

### `setmode [K] --mode M [--pad N] [--if-mode M] [--range A[-B]] [--dry-run]`
Bulk layer-mode writes across kits. Reads the kits bank first and writes
**only** the pads that need changing, so everything else about a pad is
untouched. Mode names: `MIX FADE1 FADE2 XFADE SWITCH SW(MONO) ALTERNATE
HI-HAT`.

Scope it deliberately: with no `--pad` this touches **all nine pads** of
every kit you name. A kit or `--range` is required — `--range 1-200` is how
you ask for all of them, on purpose.

| option | meaning |
| --- | --- |
| `--pad N` | only pad N, 1-9 (repeatable; default all nine) |
| `K` or `--range A[-B]` | one kit, or kit ranges (repeatable); one or the other, and one is required |
| `--if-mode M` | only pads currently in mode M |
| `--dry-run` | print the count, send nothing |

```sh
# pad 9 of kits 108-200 becomes HI-HAT, leaving pads 1-8 alone
spdutil setmode --mode HI-HAT --pad 9 --range 108-200 --commit
```

### `padlink`
Puts triggers/pads into a pad-link group across kits.

| option | meaning |
| --- | --- |
| `--group N` | link group (required) |
| `--trigger N` / `--pad N` | which objects to link (repeatable) |
| `--range A[-B]` | kits to touch (repeatable; **required**) |
| `--dry-run` | print the messages, send nothing |
| `--verbose` | show device replies |

Back the unit up before a full-range padlink run.

## Sample pool writes

### `sendwave <N> --from F.smp [--from G.smp ...] [--name X.wav]`
Uploads one or more waves on a single connection to consecutive pool
indices starting at N: writes each `.SMP` file to device flash AND
registers it in the pool directory (either alone is useless), commits the
whole batch once, then reads every wave back and reports `MATCH`/`FAIL`.
Use a fresh index range (`samples` shows what's taken). `--name` overrides
the stored filename, single-file uploads only.

Input is raw `.SMP` (RFWV) — the device plays 48 kHz/16-bit only, and the
header carries an MD5 the device checks, so build inputs with the app's
converter or round-trip them via `readwave`.

### `deletewave <N>`
Deletes sample N from the pool and commits. **DESTRUCTIVE and not undoable
on the device** — kits referencing the wave lose that layer.

## Exit status

`0` success; `1` a device/protocol operation failed (details on stderr);
`2` bad arguments (usage printed).
