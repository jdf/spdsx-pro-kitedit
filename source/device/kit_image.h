// Parsing the bank 0x10 (kits/settings) bulk-read image into kit data.
//
// A bulk dump is block frames concatenated (what SpdsxDevice::DumpBank
// returns). CleanBulkImage strips the per-block transport framing to the
// true, contiguous device memory; the kit records live there as a flat
// array of 200 fixed-size records.
//
// Record layout (reverse-engineered 2026-07-12, verified against the
// official app: the "ZZZ" kit lands at kit 129 as the app shows):
//   - array base 0x262c in the clean image, stride 3528 bytes
//   - offset 0: 16-byte space-padded ASCII kit name
//   - the remaining ~3512 bytes hold 9 pads' parameters — NOT yet mapped
//     (pad waves, layer mode, fades, dynamics, ...). To be filled in by
//     the dump/change-one-thing/re-dump diff method.
#ifndef SPDSX_PATCHEDIT_SOURCE_DEVICE_KIT_IMAGE_H_
#define SPDSX_PATCHEDIT_SOURCE_DEVICE_KIT_IMAGE_H_

#include <cstddef>
#include <string>
#include <vector>

#include "device/protocol.h"  // Bytes

namespace spdsx::device {

// The number of kit slots the device holds.
inline constexpr int kBankKitCount = 200;
inline constexpr size_t kKitRecordStride = 3528;
inline constexpr size_t kKitArrayBase = 0x262c;
inline constexpr size_t kKitNameOffset = 0;
inline constexpr size_t kKitNameLen = 16;

// Strips the per-block framing (`f0 41 6c 02` + 10-byte header ... `f7`,
// one per ~64KB block) from a raw bulk dump, yielding the contiguous
// device memory image.
Bytes CleanBulkImage(const Bytes& raw);

// One kit's parsed contents. Only the name is decoded so far.
struct KitRecord {
  std::string name;
};

// Parses the kit records out of a CLEAN (header-stripped) bank 0x10
// image. Returns up to kBankKitCount records; fewer if the image is
// short. Names are trimmed of trailing spaces.
std::vector<KitRecord> ParseKits(const Bytes& clean_image);

}  // namespace spdsx::device

#endif  // SPDSX_PATCHEDIT_SOURCE_DEVICE_KIT_IMAGE_H_
