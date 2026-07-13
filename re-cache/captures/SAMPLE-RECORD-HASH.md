# The sample-record content hash — data & rejected hypotheses

Status: **unsolved, but not blocking.** The SPD-SX PRO sample-pool
directory record contains a 32-bit field that the official app fills
with a content-derived value we have not been able to reproduce. The
device stores it verbatim and otherwise **ignores** it: a sample
uploaded with this field set to `0` registers, is measured, plays on the
unit, and is assignable to a pad in the official app (all live-verified
2026-07-13). So `UploadWave` writes `0` there and it costs us nothing
functionally. This memo exists because we dislike shipping a field we
can't explain, and to arm a future attempt.

Read alongside `WAVE-UPLOAD-DELETE-PROTOCOL.md` (the upload protocol) and
`WAVE-EXPORT-PROTOCOL.md` (the `.SMP`/RFWV layout).

---

## 1. Where the field lives

Registering wave N writes two DT1 records into the pool directory block
at `0x2000000 + N*256` (see the upload memo). The 32-bit value sits in
the **name record** (block offset `+0x1b`), at record offset **`0x84`**,
encoded as **8 nibbles — one nibble per byte, most-significant first**
(the SysEx-safe encoding, since data bytes must stay ≤ 0x7F).

In the bulk-dump image (`spdutil dump --bank 0x20`, then `CleanBulkImage`
+ `ParseSampleDir`) the same value appears **packed** as a little-endian
`uint32` at parsed-record offset **`0x8c`** (`SampleRecord` does not read
it today).

Example, sample A (`A_sine_4096`):

```
name-record 0x84 nibbles : 06 02 04 05 09 0d 00 09   (MSN-first -> 0x62459d09)
image record 0x8c (LE32)  : 09 9d 45 62              (= 0x62459d09)
```

The device translates our 8-nibble write into the packed 4-byte image
value faithfully — we verified this by writing `0` (→ image `00 00 00
00`) and the real value (→ image `09 9d 45 62`).

---

## 2. The ground-truth data

Four controlled uploads through the official app under frida
(`synthupload-1.log`, 2026-07-13). We generated the WAVs, so their PCM is
fully known. Readback `.SMP`s are at `/tmp/synth{1586..1589}.smp`; source
WAVs at `~/Desktop/spdsx-upload-test/*.wav`. All are **48 kHz, 16-bit,
mono**.

| idx  | wavename       | filename           | content | frames | PCM bytes | `.SMP` bytes | hash (packed) | hash nibbles (record 0x84) |
|------|----------------|--------------------|---------|--------|-----------|--------------|---------------|-----------------------------|
| 1586 | `A_sine_4096`  | `A_sine_4096.wav`  | sine    | 4096   | 8192      | 8704         | `0x62459d09`  | `06 02 04 05 09 0d 00 09`   |
| 1587 | `B_noise_4096` | `B_noise_4096.wav` | noise   | 4096   | 8192      | 8704         | `0xee8ab53f`  | `0e 0e 08 0a 0b 05 03 0f`   |
| 1588 | `C_sine_8192`  | `C_sine_8192.wav`  | sine    | 8192   | 16384     | 16896        | `0xf1c1c77d`  | `0f 01 0c 01 0c 07 07 0d`   |
| 1589 | `D_sine_12288` | `D_sine_12288.wav` | sine    | 12288  | 24576     | 25088        | `0x5519d9bf`  | `05 05 01 09 0d 09 0b 0f`   |

The `.SMP` = a 512-byte RFWV header + signed-LE PCM. Verified: readback
`.SMP` PCM is **byte-identical** to the source WAV PCM.

### What the four points already tell us

- **It is content-dependent.** A and B are the *same size* (4096 frames)
  and *same-shaped* names but different audio (sine vs noise) → different
  hashes. So it is not purely a size or index function.
- **It is not purely name-derived.** Same reasoning cuts the other way:
  any two rows differ in both name and content, but a value driven only
  by the name string doesn't reproduce (§3).
- **The values look diffuse** (no low-bit structure, no obvious linear
  relationship between rows), consistent with a real hash/CRC rather than
  a small packed field.
- **32 bits exactly**, and the app clearly *computes* it locally: it is
  not present in any device reply (§3).

### Adjacent structure in the RFWV header (context, possibly relevant)

The 512-byte header is not the "constant padding" an earlier note
assumed. Diffing the four headers:

- **`0x00–0x1f`**: the documented RFWV fields (magic, data length, rate,
  channels, bits). `0x04` (data length) tracks size.
- **`0x20–0x2f`**: **size-correlated** — identical for A and B (same
  size), differs for C (different size).
- **`0x30–0x1ff`**: **content-derived** — differs even for same-size A vs
  B. This ~464-byte block is almost certainly an app-generated waveform
  overview / peak summary (the app draws sample thumbnails). It is a
  plausible *input* to, or sibling of, the hash — but hashing it did not
  reproduce the value (§3).

---

## 3. Rejected hypotheses

Every test below required a **simultaneous match on all four samples**
(both the value and its byteswap accepted, to absorb endianness). Scripts
are in the session scratchpad (`hashhunt.py`, `namehash.py`,
`finalsweep.py`, `hdrhunt.py`, `recordhash.py`). Nothing matched.

**(a) Standard checksum/hash over the audio.**
Algorithms: CRC-32 with polynomials CRC-32 (`0x04C11DB7`), CRC-32C
(`0x1EDC6F41`), CRC-32K (`0x741B8CD7`), CRC-32Q (`0x814141AB`), across
**all** combinations of init ∈ {0, 0xFFFFFFFF}, xorout ∈ {0, 0xFFFFFFFF},
and reflected/non-reflected in+out; plus Adler-32, Fletcher-32, FNV-1,
FNV-1a, djb2, djb2-xor, sdbm, Jenkins one-at-a-time, MurmurHash3 (seeds 0
and 1), POSIX `cksum`, byte-sum, 32-bit XOR of words, and sums of u16/u32
words.
Regions: PCM only; whole `.SMP`; RFWV header `0x00–0x1ff`, `0x00–0x1f`,
`0x00–0x2f`, `0x30–0x1ff`; and from offset `0x20`.
→ **No match.**

**(b) Standard hash over the source `.wav`.** Full file and PCM-only
(44-byte header stripped), same algorithm set. (Sanity-checked that
`.SMP` PCM == `.wav` PCM.)
→ **No match.**

**(c) Hash of the name string.** wavename and filename, in variants: raw,
space-padded to 16/100, NUL-padded, uppercased, and `wavename‖filename`
concatenated — same algorithm set.
→ **No match.** (Motivated because A/B differ in name too; ruled out.)

**(d) Combined / seeded hashes.** `name‖PCM`, `PCM‖name`,
`name16‖PCM`; FNV-1a seeded by the name then run over PCM; CRC-32 chained
name-then-PCM; CRC-32 seeded by the sample index.
→ **No match.**

**(e) Hash of the app-generated overview block.** CRC-32/CRC-32C variants
over header `0x30–0x1ff` and `0x20–0x1ff`.
→ **No match.** (Still the most interesting untested *variant space* —
see §4.)

**(f) The value copied from somewhere, not computed fresh.** Searched
each `.SMP` (header + PCM) for the 32-bit value in every byte order
(BE, LE, nibble-reversed, per-byte-swapped) and as its 8-nibble
expansion.
→ **Absent.** It is nowhere in the file.

**(g) Device-provided.** Inspected the register replies. `6a 03 0b` →
generic ack; `6a 03 0c` → reply carrying only the **file size**
(`...0c 40 00 00 00 0c 01 00 00 00 00 02 00 00 00 22...`). The hash value
does not appear in any reply. The app computes it before it writes the
record.
→ **Not device-sourced.**

**(h) Hash of the directory record's own bytes.** CRC/sum/FNV/djb2/Adler
over the pre-hash span of the name record (with/without the constant
`Z4T2393` block, names trimmed/padded).
→ **No match.**

---

## 4. Promising avenues, if we revisit

Ordered by appeal, cheapest first. None require disassembly (explicitly
off the table).

1. **More synthetic triangulation.** The four points can't separate
   "content only" from "content + name" or expose linearity. Upload a
   controlled matrix: (i) identical audio under *different names* → is
   the hash name-sensitive at all? (ii) identical name, *one PCM sample
   flipped* → does the hash change by a CRC-like avalanche or an
   additive delta? (iii) all-zero PCM of each length → a baseline that
   often exposes the init/seed. This is the same synthetic-WAV method
   that cracked the length field; it just needs a few more uploads.
2. **Hash of the overview block, harder.** The `0x30–0x1ff` block is
   content-derived and app-generated; the hash may be a checksum *of it*
   (or they share an accumulator). Test CRC/FNV over every sub-range and
   with seeds = size, frames, or the `0x20–0x2f` field; also try the
   block reinterpreted as int16/int32 arrays.
3. **Roland-specific / non-CRC constructions.** Sum-of-squares or
   peak/RMS-derived integers, ADPCM-style running checksums, or a
   truncated wider hash (e.g. low 32 bits of a 64-bit FNV/xxHash, or a
   CRC over down-sampled/decimated PCM matching the overview's
   decimation).
4. **Confirm the "ignored" conclusion stays true.** We proved the device
   ignores the field for register/measure/play/pad-assign. If a *future*
   firmware or an app-side "verify/repair pool" feature ever validates
   it, revisit — but today writing `0` is correct and safe.

---

## 5. Why we ship `0`

The whole point of the hash question was whether upload could work
without it. It can. `SpdsxDevice::UploadWave` writes `0` into this field;
`spdutil sendwave` drives it; `spdutil selftest` guards the record bytes.
Live-verified end to end. This memo is the paper trail for the one part
of the format we replicate without understanding.
