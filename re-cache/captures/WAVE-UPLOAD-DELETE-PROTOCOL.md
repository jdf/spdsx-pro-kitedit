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

## DELETE — simple, fully decoded
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

Full capture: fileops-1.log.
