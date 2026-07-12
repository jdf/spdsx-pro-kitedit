// Roland SPD-SX PRO DT1 protocol: message construction and address encoding.
// Pure byte manipulation, no I/O — ported from spdsx-py's spdsx.py and
// verified byte-for-byte against the same live captures.
#ifndef SPDSX_PATCHEDIT_SOURCE_DEVICE_PROTOCOL_H_
#define SPDSX_PATCHEDIT_SOURCE_DEVICE_PROTOCOL_H_

#include <cstddef>
#include <cstdint>
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

// Bulk-read (stream-a-bank) request, from capture:
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
