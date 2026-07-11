// Roland SPD-SX PRO DT1 protocol: message construction and address encoding.
// Pure byte manipulation, no I/O — ported from spdsx-py's spdsx.py and
// verified byte-for-byte against the same live captures.
#ifndef SPDSX_PATCHEDIT_SOURCE_DEVICE_PROTOCOL_H_
#define SPDSX_PATCHEDIT_SOURCE_DEVICE_PROTOCOL_H_

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

}  // namespace spdsx::device

#endif  // SPDSX_PATCHEDIT_SOURCE_DEVICE_PROTOCOL_H_
