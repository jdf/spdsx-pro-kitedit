// Parsing the bank 0x10 (kits/settings) bulk-read image into kit data.
//
// A bulk dump is block frames concatenated (what SpdsxDevice::DumpBank
// returns). CleanBulkImage strips the per-block transport framing to the
// true, contiguous device memory; the kit records live there as a flat
// array of 200 fixed-size records.
//
// Record layout (reverse-engineered 2026-07-12, verified against the
// official app: the "ZZZ" kit lands at kit 129, and edited pad values
// read back exactly):
//   - array base 0x262c in the clean image, stride 3528 bytes
//   - offset 0: 16-byte space-padded ASCII kit name
//   - offset 0x284: the per-pad parameter table, 9 pads x 28-byte blocks.
//     Within a pad block the byte offset equals the DT1 param index (the
//     same index the write address 06 00 <1F+pad> <param> uses):
//       +0x00 layer mode, +0x01 fade point, +0x02 fade end,
//       +0x03 dynamics on/off, +0x04 dynamics curve, +0x05 fixed velocity,
//       +0x13 trigger reserve.
//   - offset 0x49a: the per-layer table, 18 layers (9 pads x top/bottom)
//     x 60-byte blocks; the block starts with the layer's wave (sample
//     pool index) as u16 LE, 0 = no sample. Verified against the "ZZZ"
//     experiment kit (pad 7 = 127/203) and the factory kits.
//   Many other pad params (EQ, effects, ...) are elsewhere in the record
//   and still unmapped.
#ifndef SPDSX_PATCHEDIT_SOURCE_DEVICE_KIT_IMAGE_H_
#define SPDSX_PATCHEDIT_SOURCE_DEVICE_KIT_IMAGE_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "device/protocol.h"  // Bytes

namespace spdsx::device {

// The number of kit slots the device holds.
inline constexpr int kBankKitCount = 200;
inline constexpr int kPadsPerKit = 9;
inline constexpr size_t kKitRecordStride = 3528;
inline constexpr size_t kKitArrayBase = 0x262c;
inline constexpr size_t kKitNameOffset = 0;
inline constexpr size_t kKitNameLen = 16;

// Per-pad parameter table within a kit record.
inline constexpr size_t kPadTableBase = 0x284;
inline constexpr size_t kPadBlockStride = 28;  // 0x1c
// Byte offsets within a pad block == the DT1 param index.
inline constexpr size_t kPadLayerMode = 0x00;
inline constexpr size_t kPadFadePoint = 0x01;
inline constexpr size_t kPadFadeEnd = 0x02;
inline constexpr size_t kPadDynamics = 0x03;
inline constexpr size_t kPadDynCurve = 0x04;
inline constexpr size_t kPadFixedVel = 0x05;
inline constexpr size_t kPadTrigReserve = 0x13;

// Per-layer table within a kit record; each layer block starts with the
// wave index (u16 LE).
inline constexpr size_t kLayerTableBase = 0x49a;
inline constexpr size_t kLayerBlockStride = 60;  // 0x3c

// Strips the per-block framing (`f0 41 6c 02` + 10-byte header ... `f7`,
// one per ~64KB block) from a raw bulk dump, yielding the contiguous
// device memory image.
Bytes CleanBulkImage(const Bytes& raw);

// A pad's hit-response parameters as stored on the device (raw byte
// values; mode 0-7 and curve 0-3 match the app's enum order, which is
// also the app's LayerMode/DynamicsCurve order).
struct PadDeviceParams {
  uint8_t layer_mode = 0;
  uint8_t fade_point = 0;
  uint8_t fade_end = 0;
  uint8_t dynamics = 0;
  uint8_t dynamics_curve = 0;
  uint8_t fixed_velocity = 0;
  uint8_t trigger_reserve = 0;
  // Wave (sample pool index) per layer; 0 = no sample.
  uint16_t wave_top = 0;
  uint16_t wave_bottom = 0;
};

// One kit's parsed contents: name plus the nine pads' mapped params.
struct KitRecord {
  std::string name;
  std::array<PadDeviceParams, kPadsPerKit> pads;
};

// Parses the kit records out of a CLEAN (header-stripped) bank 0x10
// image. Returns up to kBankKitCount records; fewer if the image is
// short. Names are trimmed of trailing spaces.
std::vector<KitRecord> ParseKits(const Bytes& clean_image);

}  // namespace spdsx::device

#endif  // SPDSX_PATCHEDIT_SOURCE_DEVICE_KIT_IMAGE_H_
