// Roland SPD-SX PRO DT1 protocol: message construction and address encoding.
// Pure byte manipulation, no I/O — ported from spdsx-py's spdsx.py and
// verified byte-for-byte against the same live captures.
#ifndef SPDSX_PATCHEDIT_SOURCE_DEVICE_PROTOCOL_H_
#define SPDSX_PATCHEDIT_SOURCE_DEVICE_PROTOCOL_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace spdsx::device {

using Bytes = std::vector<uint8_t>;

// Confirmed from live captures.
inline constexpr uint8_t kDeviceId = 0x10;
inline constexpr uint8_t kModelId[] = {0x00, 0x00, 0x00, 0x00, 0x16};
inline constexpr uint8_t kDt1 = 0x12;  // write data to address
inline constexpr uint8_t kRq1 = 0x11;  // request data from address

// The two kinds of triggerable object on the instrument.
enum class ObjectKind { kPad, kTrig };

// Kit names are a fixed 16-character space-padded ASCII field.
inline constexpr int kKitNameLength = 16;

// A pad slot: the top or bottom layer.
enum class PadSlot { kTop, kBottom };

// Roland checksum over the address+data bytes: (128 - sum % 128) & 0x7F.
uint8_t Checksum(const Bytes& body);

// Builds a DT1 write message:
//   F0 41 <dev> <model...> 12 <addr...> <data...> <cksum> F7
Bytes Dt1(const Bytes& address, const Bytes& data);

// Kit number (1..200) -> two data bytes. value = kit - 1; hi = value >> 4,
// lo = value & 0x0F. Throws std::out_of_range outside 1..200.
Bytes EncodeKit(int kit);

// Address of the current-kit select parameter.
extern const Bytes kKitSelectAddr;
// Address of the object-focus parameter (required before a pad-link write).
extern const Bytes kObjectSelectAddr;

// Data byte for the object-focus write: pad N -> N-1, trig N -> 8 + N.
uint8_t SelectValue(ObjectKind kind, int index);

// Kit-encoded pad-link address prefix: flat = 512 + 2*(kit-1), split into
// two 7-bit bytes (hi = flat >> 7, lo = flat & 0x7F).
Bytes PadLinkPrefix(int kit);

// Full pad-link parameter address for (kind, index) within a specific kit.
Bytes PadLinkAddr(ObjectKind kind, int index, int kit);

// Nibble-encodes a value into 4 bytes, one nibble each (big-endian). Roland
// uses this for values that exceed 0x7F, since SysEx data bytes must stay
// 7-bit; e.g. 127 -> 00 00 07 0f, 203 -> 00 00 0c 0b.
Bytes NibbleEncode(int value);

// Address of kit-name character i (0..15), current-kit-relative (select the
// kit first).
Bytes KitNameAddr(int i);

// Wave-assignment address for a pad slot, current-kit + focused-pad relative
// (select the kit and focus the pad first). Top slot uses param 0x4c, bottom
// 0x4d; the trailing 0x01 selects the wave-number field.
Bytes PadWaveAddr(PadSlot slot);

// Address of a pad's layer parameter, current-kit + focused-pad relative
// (select the kit and focus the pad first). pad is 1-based (pad 1 ->
// 0x20 .. pad 9 -> 0x28); param is the parameter index, which equals the
// byte offset within the pad's kit-record block: 0x00 layer mode, 0x01
// fade point, 0x02 fade end, 0x03 dynamics, 0x04 dynamics curve, 0x05
// fixed velocity, 0x13 trigger reserve.
Bytes PadParamAddr(int pad, int param);

// ---- Sample-pool registration (upload directory records) ----
//
// Registering an uploaded wave writes two DT1 records into the sample's
// 256-byte directory block, whose base DT1 address is
//   0x2000000 + index*256 + offset
// encoded as four big-endian 7-bit bytes (index 1586, offset 0x1b ->
// 10 18 64 1b). Decoded from synthupload-1.log (2026-07-13); see
// re-cache/captures/WAVE-UPLOAD-DELETE-PROTOCOL.md.
Bytes SampleRecordAddr(int index, int offset);

// The 151-byte "base" record (block offset 0x00). Byte 0x0c is a size
// field (= frames / 4096 across the captured uploads); the rest is
// constant. Content-independent apart from that size.
Bytes SampleBaseRecord(int frames);

// The 140-byte "name" record (block offset 0x1b): wavename[16] +
// filename[100] (both space-padded) + a constant block, then a 32-bit
// content hash at offset 0x84 as eight nibbles (one per byte, MSN
// first). The hash's algorithm is unknown; pass 0 to test whether the
// device recomputes it on flash-commit.
Bytes SampleNameRecord(const std::string& wavename,
    const std::string& filename, uint32_t content_hash);

// ---- Bulk block transfer (device-state read) ----
//
// The whole ~8MB device image streams on the 41 6c family (channel 0x08),
// banked: 0x10 = kit/settings, 0x20 = sample audio, 0x30/0x40 = small
// metadata/config. The app streams a bank with this request, and the device
// replies with a series of `f0 41 6c 02 ...~64KB... f7` block frames.
inline constexpr uint8_t kBankKits = 0x10;
inline constexpr uint8_t kBankSamples = 0x20;
inline constexpr uint8_t kBankMeta = 0x30;
inline constexpr uint8_t kBankConfig = 0x40;

// A bulk-transfer request on the 41 6c family:
//   f0 41 6c 03 <sub> 00 00 00 00 <bank> 00 00 <arg LE32> f7
// Sub-commands (decoded from the official app's load, 2026-07-12):
//   0x05 PREPARE a bank — the 6c 7a ack carries the bank's size
//   0x00 BEGIN reading the bank
//   0x02 READ a chunk — device streams a batch of 6c 02 data blocks;
//        arg 0x24 requests the next standard chunk
//   0x01 END reading the bank
inline constexpr uint8_t kBulkPrepare = 0x05;
inline constexpr uint8_t kBulkBegin = 0x00;
inline constexpr uint8_t kBulkRead = 0x02;
inline constexpr uint8_t kBulkEnd = 0x01;
// The arg the app sends to pull the next standard chunk.
inline constexpr uint32_t kBulkNextChunk = 0x24;

Bytes BulkRequest(uint8_t sub, uint8_t bank, uint32_t arg);

// PREPARE request for a bank (kept for the selftest's byte-exact check):
//   f0 41 6c 03 05 00 00 00 00 <bank> 00 00 00 00 00 00 f7
Bytes BulkReadRequest(uint8_t bank);

// One reply block located within a reassembled image (the concatenation of
// the block frames' payloads, headers included — same layout the RE
// parse_capture.py produces and the re-cache image stores).
struct BulkBlock {
  uint8_t bank;    // payload byte 8
  size_t offset;   // start of the `f0 41 6c 02` marker in the image
  size_t length;   // full payload length through its terminating f7
};

// Splits a reassembled image into its blocks by scanning for the
// `f0 41 6c 02` markers. Each block runs to the next marker (or end).
std::vector<BulkBlock> SplitBulkImage(const Bytes& image);

}  // namespace spdsx::device

#endif  // SPDSX_PATCHEDIT_SOURCE_DEVICE_PROTOCOL_H_
