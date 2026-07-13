# Sample upload + delete (fileops-1.log, 2026-07-13)

Captured the official app UPLOADING a short user wave and DELETING
sample 1586. Both use the file protocol (`f0 41 7a`, channel 0x06) plus
control frames (`f0 41 6a`, channel 0x09), and both end with the same
flash-commit handshake as pad-parameter writes (`6a 03 21` + poll
`6a 03 22` until the status word flips 0→1).

## Control-frame shape (channel 0x09)
32-byte frame `f0 41 6a 03 <sub> 00×5 40 00×4 <u32 arg @byte15> …zeros… f7`
(same layout as the firmware-version query `03/17`, arg at byte 15).
Short 17-byte frame `f0 41 6a 03 <sub> 00×11 f7` for the commit begin/poll.

## DELETE — simple, fully decoded, LIVE-VERIFIED (2026-07-13)
`spdutil deletewave 1586` removed it: the pool dropped 1586→1585, the
index vanished from `samples`, and a re-read errored cleanly. Works.

Delete sample N (1586 = `32 06 00 00` LE at byte 15):
```
6a 03 09  arg=1      begin session      -> 6a 7a ack
6a 03 1d  arg=N      DELETE sample N     -> 6a 7a ack
6a 03 09  arg=0      end session         -> 6a 7a ack
6a 03 21             flash-commit begin  -> 6a 7a ack
6a 03 22  (poll ~250ms) -> 6a 02 …22 40 00 00 00 04 <status LE32> f7
                        status 0 busy, 1 done (~0.5s)
```
(The app also issued `03/0d`, `03/18`, `03/23` status queries around it —
`03/0d` returned the current sample index, `03/18` a count; they look
optional. The essential path is 09/1d/09 + commit.)

## UPLOAD — captured, structure clear, some fields TBD
Writes the wave to its `.SMP` path then registers + commits. On ch 0x06
the write opcode is `03/06` (vs `03/04` read); `03/00` opens, `03/07`
seeks, `03/03` closes — same as the read side. Sequence for W<idx>:
```
6a 03 09 / 03 0a                     enter file session
7a 03 0a  <path>.TMP                 (delete stale temp; ack 7f 7f 7f 7f = absent)
7a 03 18  <path>.SMP + data          create/prealloc?
7a 03 0c  <dir D0nn>                 mkdir/stat dir
7a 03 0d                             handle op
7a 03 00  <path>.SMP                 OPEN for write
7a 03 07  seek                       position
7a 03 19  "/SPDSXREMOTE//"           begin/allocate
7a 03 06  <hdr10> <PCM bytes>        WRITE the PCM (one big chunk here)
7a 03 07  seek                       back to header region
7a 03 06  <hdr10> <512-byte RFWV>    WRITE the header
7a 03 03                             CLOSE
7a 03 0a  <path>.TMP                 finalize/rename
6a 03 0b  arg=N                      register: begin
6a 03 0c  arg=N   -> …0c… <u32 size> register: record size (= file size)
6a 03 21 + poll 03 22                flash commit
```
`03/06` write body = `<u32 offset LE> 40 00 <3 bytes> <data>`; the 3-byte
field is echoed in the ack (`03 0d 12` for the PCM write, `00 04 00` for
the 512-byte header write) — looks like a per-write tag/checksum, NOT a
plain length (PCM was 50834=0xC692 bytes, field said `03 0d 12`).
WHETHER the device validates that field (i.e. we must compute it) vs
accepts zeros is the main unknown to settle live. The `.TMP`/`.SMP`
interplay and the exact `03/18`/`03/19` roles are the other things to
pin during implementation, verifying each upload by reading it back with
`readwave` + confirming in `samples`.

### Upload CRACKED (2026-07-13, synthupload-1.log + live)
Controlled uploads (2 same-size/diff-content, 3 sizes) settled it:

**File write — WORKS, verified byte-for-byte.** The `03/06` write field
is the payload length in **3-byte big-endian base-128 (7-bit)**:
8192→`00 40 00`, 50834→`03 0d 12`. Same for A and B (diff content) ⇒
not a checksum. Feeding 0 before was the hang (device read the wrong
byte count). Also: SKIP the `03/19` free-space query (replay gets no
ack; write completes without it) and give file ops a ~3s timeout. Then
`WriteRemoteFile` → `readwave` round-trips exactly. The write does NOT
register or commit; the file is readable by path only.

**Register — fully decoded except one 32-bit field (2026-07-13, refined).**
After the file write the app does, in order (heartbeats `6a 03 15`/`16`
interleaved — ignore them):
```
7a 03 0a  <path>.TMP            finalize/cleanup temp   -> 7a 7a ack
6a 03 0b  arg=N                 register begin          -> 6a 7a ack
6a 03 0c  arg=N                 register size (reply = file size)
DT1 base record @ block+0x00                            -> 6a 7a ack
DT1 name record @ block+0x1b                            -> 6a 02 ack
6a 03 21 + poll 03 22           flash commit
```
There are **TWO** DT1 record writes into the sample's 256-byte directory
block at `0x2000000 + N·256`, addressed as 4×7-bit bytes (N=1586: `+0x00`
→ `10 18 64 00`, `+0x1b` → `10 18 64 1b`; verified in code as
`SampleRecordAddr`):
- **base record** (`+0x00`, 151 bytes): all constant EXCEPT byte `0x0c`
  = a size field (= frames/4096 across the four synth uploads: 4096→1,
  8192→2, 12288→3) and a 16-byte space field at `0x1f–0x2e`. **No hash
  here.** Byte `0x18`=0x7f and a trailing `04 0b 00` are constant.
- **name record** (`+0x1b`, 140 bytes): 4 zeros + wavename[16] (`0x04`) +
  filename[100] (`0x14`, space-padded) + `00 04 0b 00 "Z4T2393 "`
  (constant) + the **32-bit content hash at `0x84`** as 8 nibbles (one
  per byte, MSN first; A=`0x62459d09`).
- **The hash turned out to be irrelevant — UPLOAD IS DONE (live-verified
  2026-07-13).** It is NOT any standard algorithm (all CRC-32 variants,
  Adler-32, FNV-1/1a, djb2/sdbm/joaat, sum/fletcher/murmur3, POSIX cksum)
  over ANY region, NOT a name-string hash, and NOT device-provided — a
  proprietary value the app computes and jdf declined to disassemble for.
  **It didn't matter:** the live test (below) proved the device simply
  stores the field verbatim and otherwise IGNORES it. A sample uploaded
  with the hash field = 0 registers, gets its length measured by the
  device, and PLAYS perfectly on the unit (jdf confirmed). So we write 0
  and never compute it.

### UPLOAD SHIPPED + LIVE-VERIFIED (2026-07-13)
Upload is one atomic action: `SpdsxDevice::UploadWave` (= WriteRemoteFile
+ RegisterWave, both private) and one CLI command `spdutil sendwave <N>
--from f.smp [--name X.wav]` — there is no separate register command (a
written-but-unregistered file is invisible to every UI), and no hash/tail
option (the field is always written 0). Records are byte-exact vs this
capture, guarded by `spdutil selftest`.

Live test on hardware: uploaded a synth `.smp` to fresh indices 1590
(hash 0) and 1591 (real hash 0x62459d09). Both appear in `samples` with
the right name and read back byte-exact; **1590 (hash 0) plays perfectly
on the device.** Two findings from diffing the resulting bank-0x20
records against the app's own 1586 (identical content):
- The device does **not** compute/recompute or validate the hash — it
  stores whatever byte you send (1590 kept 0, 1591 kept the real value).
- The parsed record's frame-count field (record 0x94, LE32) reads 0
  immediately after upload but corrects itself later: it's a **bulk-dump
  image coherency lag**, not a field we write. The device measured the
  length correctly on its own (1590 showed 0.09 s once the image caught
  up). Base-record byte 0x0c (= frames/4096) is the only length hint we
  send; the exact count is device-derived.
Only caveat left: whether the OFFICIAL app flags a hash-0 sample when it
next loads state (jdf to eyeball); nothing on the device itself does.

### Earlier upload attempt (2026-07-13) — FAILED, hung the device
Implemented the sequence (WriteRemoteFile) and ran it with the `03/06`
tag = 0. Result: the readback found no file, and the device stopped
answering pings entirely (serial node still present) — needed a power
cycle. Conclusion: the 3-byte `03/06` field is load-bearing and must be
correct; a wrong value desyncs the device (it mis-parses the trailing
payload as commands and hangs). The field is NOT the payload length,
sum, or CRC (checked). Correlating the capture's acks:
```
seek 03/07 (req …04 00) -> ack 00 00 04 00      (echoes 04 00)
03/19    -> ack 0e 2a 10 20
PCM 03/06 (req tag 03 0d 12) -> ack 03 0d 12
hdr 03/06 (req tag 00 04 00) -> ack 00 04 00
```
The PCM tag `03 0d 12` doesn't come from any visible preceding ack, so
it looks like a device-side flash address / write handle the app
tracks — not reconstructable offline. NEXT (before any more live
writes, which risk hanging the unit): a dedicated capture that varies
sample SIZE across two uploads to see how the tag moves with length,
and/or watch for a command whose reply seeds it. Until then sendwave is
unsafe. Delete is unaffected and works.

Full capture: fileops-1.log.
