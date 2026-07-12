# User-wave export = remote-filesystem READ (`f0 41 7a`, channel 0x06)

Captured 2026-07-12 (`waveexport-1.log`) exporting user sample 1554
(`Bongo_Hi_CR78`) from the official app. Exporting a wave is the app
reading a file off the device over a **remote-filesystem protocol** that
is distinct from everything mapped before:

- channel **0x06** in the transport frame (`0d 60 e0 06` write /
  `0d e0 60 06` read), vs 0x07 DT1, 0x08 bulk, 0x09 control.
- family **`f0 41 7a`**: `03` = request, `02` = data reply, `7a` = ack
  (mirrors the `6a`/`6c` families). Sub-command is the byte after `03`.

## The file lives at a derived path
The OPEN request carries a length-prefixed ASCII path:

```
/SPDSXREMOTE//Roland/SPD-SXPRO/WAVE/DATA/D015/W01554.SMP
```

Sample index N maps to `.../WAVE/DATA/D{N/100:03d}/W{N:05d}.SMP`
(1554 -> `D015/W01554.SMP`). `/SPDSXREMOTE/` is a virtual root the device
exposes for remote file access. The files are **`.SMP`, not `.wav`** —
Roland's own container (magic `RFWV`), so the host converts on export.

## `.SMP` / RFWV header (from the first data read)
```
52 46 57 56  14 4d 00 00  80 bb 00 00  01 00 ...
R  F  W  V   len=0x4d14    rate=0xbb80  ch=0x0001
             =19732 bytes  =48000 Hz    =mono
```
Audio body is signed 16-bit LE PCM (`92 00 42 01 26 01 ...` = a rising
waveform). STAT (below) reported file size 0x4d1c = 19740 = 19732 + an
8-byte prefix/chunk header before the data.

## The exchange (one export)
All on channel 0x06; bodies shown are the bytes after `f0 41 7a 03 <sub>`.
```
OPEN   03/00  <9x00> 39 <"/SPDSXREMOTE//.../W01554.SMP"> 00   -> 7a ack (…16)
SEEK?  03/07  <zeros>                                        -> 7a ack
READ   03/04  … 04 00                                        -> 02/7d <handle> 40 00 …  RFWV header+data
STAT   03/13  <zeros>                                        -> 02/00 … 08 <u32 ffff81> <u32 size=0x4d1c>
SEEK   03/07  … 04 00     (position := 4)                    -> 7a ack (body echoes 00 00 04 00)
READ   03/04  … 01 16 1c 00                                  -> 02/7e <handle> 40 00 …  PCM
CLOSE  03/03  <zeros>                                        -> 7a ack
```
Bracketed on channel 0x09 by `6a 03 09` / `6a 03 0a` control frames
(enter/leave remote-file mode, likely).

Reply framing: `f0 41 7a 02 <sub> <u32 handle> 40 00 <...> <payload>`.
The `02` sub byte differs per reply (`7d`, `7e`, `00`); looks like a
rolling tag, not load-bearing. `7a` acks end `... 16 f7`.

## What is SOLID vs still fuzzy
Solid: the channel, the `03/00/07/04/13/03` command set, the path
derivation, the RFWV header layout, PCM-16 mono @ 48k.

Fuzzy (needs one more capture): the exact **offset/length field
arithmetic** in `03/04`/`03/07`, and the **multi-block read loop** for
files bigger than one reply — this sample was small (~19.7 KB) and the
`bulkread.js` logger truncates reads to 48 bytes (`READ_HEAD`), so the
data replies aren't reconstructable from this log. NEXT CAPTURE: export
a LARGER user wave with `wavexport.js` (no read truncation, channel-6
focus) to watch the read loop and confirm the length encoding. Then the
transfer is mechanical to port into `spdsx_device` (channel-6 ops) +
RFWV->PCM, feeding the local sample cache so device waves become
playable/thumbnailed. PRELOADs stay unreadable (their `.SMP` files are
presumably not exposed under `/SPDSXREMOTE/`, or export is blocked).
```
```
