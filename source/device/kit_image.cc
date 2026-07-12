#include "device/kit_image.h"

namespace spdsx::device {

namespace {

// Each block frame is `f0 41 6c 02` + a 10-byte header (14 bytes total)
// then the data, then a trailing `f7`.
constexpr size_t kBlockHeaderLen = 14;
constexpr size_t kBlockTrailerLen = 1;

// Trims trailing spaces (the device space-pads fixed-width names).
std::string TrimName(const Bytes& b, size_t off, size_t len) {
  size_t end = off + len;
  while (end > off && (b[end - 1] == 0x20 || b[end - 1] == 0x00)) {
    --end;
  }
  return std::string(b.begin() + off, b.begin() + end);
}

}  // namespace

Bytes CleanBulkImage(const Bytes& raw) {
  Bytes clean;
  clean.reserve(raw.size());
  for (const BulkBlock& blk : SplitBulkImage(raw)) {
    if (blk.length <= kBlockHeaderLen + kBlockTrailerLen) {
      continue;
    }
    const size_t start = blk.offset + kBlockHeaderLen;
    const size_t stop = blk.offset + blk.length - kBlockTrailerLen;
    clean.insert(clean.end(), raw.begin() + start, raw.begin() + stop);
  }
  return clean;
}

std::vector<KitRecord> ParseKits(const Bytes& clean_image) {
  std::vector<KitRecord> kits;
  for (int i = 0; i < kBankKitCount; ++i) {
    const size_t rec = kKitArrayBase + static_cast<size_t>(i) * kKitRecordStride;
    if (rec + kKitNameOffset + kKitNameLen > clean_image.size()) {
      break;
    }
    KitRecord k;
    k.name = TrimName(clean_image, rec + kKitNameOffset, kKitNameLen);
    for (int pad = 0; pad < kPadsPerKit; ++pad) {
      const size_t p = rec + kPadTableBase
          + static_cast<size_t>(pad) * kPadBlockStride;
      if (p + kPadTrigReserve >= clean_image.size()) {
        break;
      }
      PadDeviceParams& pp = k.pads[static_cast<size_t>(pad)];
      pp.layer_mode = clean_image[p + kPadLayerMode];
      pp.fade_point = clean_image[p + kPadFadePoint];
      pp.fade_end = clean_image[p + kPadFadeEnd];
      pp.dynamics = clean_image[p + kPadDynamics];
      pp.dynamics_curve = clean_image[p + kPadDynCurve];
      pp.fixed_velocity = clean_image[p + kPadFixedVel];
      pp.trigger_reserve = clean_image[p + kPadTrigReserve];
      // The layer table: top = layer pad*2, bottom = pad*2 + 1.
      const size_t top = rec + kLayerTableBase
          + static_cast<size_t>(pad) * 2 * kLayerBlockStride;
      if (top + kLayerBlockStride + 1 < clean_image.size()) {
        pp.wave_top = static_cast<uint16_t>(clean_image[top]
            | clean_image[top + 1] << 8);
        pp.wave_bottom = static_cast<uint16_t>(
            clean_image[top + kLayerBlockStride]
            | clean_image[top + kLayerBlockStride + 1] << 8);
      }
    }
    kits.push_back(std::move(k));
  }
  return kits;
}

}  // namespace spdsx::device
