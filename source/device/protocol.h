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

}  // namespace spdsx::device

#endif  // SPDSX_PATCHEDIT_SOURCE_DEVICE_PROTOCOL_H_
