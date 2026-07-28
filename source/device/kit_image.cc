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
    const size_t rec =
        kKitArrayBase + static_cast<size_t>(i) * kKitRecordStride;
    if (rec + kKitNameOffset + kKitNameLen > clean_image.size()) {
      break;
    }
    KitRecord k;
    k.name = TrimName(clean_image, rec + kKitNameOffset, kKitNameLen);
    // Pads and triggers share one block layout; only the table base,
    // the link offset, and the layer-table block differ.
    auto parse_object = [&](size_t block, size_t link_offset, int layer_block) {
      PadDeviceParams pp;
      if (block + kPadTrigReserve >= clean_image.size()) {
        return pp;
      }
      pp.layer_mode = clean_image[block + kPadLayerMode];
      pp.fade_point = clean_image[block + kPadFadePoint];
      pp.fade_end = clean_image[block + kPadFadeEnd];
      pp.dynamics = clean_image[block + kPadDynamics];
      pp.dynamics_curve = clean_image[block + kPadDynCurve];
      pp.fixed_velocity = clean_image[block + kPadFixedVel];
      pp.hi_hat_volume = clean_image[block + kPadHiHatVolume];
      pp.hi_hat_fade_in = clean_image[block + kPadHiHatFadeIn];
      pp.hi_hat_decay = clean_image[block + kPadHiHatDecay];
      pp.trigger_reserve = clean_image[block + kPadTrigReserve];
      pp.pad_link = clean_image[block + link_offset];
      const size_t top = rec + kLayerTableBase
          + static_cast<size_t>(layer_block) * kLayerBlockStride;
      if (top + 2 * kLayerBlockStride <= clean_image.size()) {
        auto layer_wave = [&](size_t at) {
          return static_cast<uint16_t>(clean_image[at]
                                       | clean_image[at + 1] << 8);
        };
        auto layer_mix = [&](size_t at) {
          PadDeviceParams::LayerMix m;
          m.volume_db10 =
              static_cast<int16_t>(clean_image[at + kLayerVolumeLo]
                                   | clean_image[at + kLayerVolumeLo + 1] << 8);
          m.fade_in = clean_image[at + kLayerFadeIn];
          m.decay = clean_image[at + kLayerDecay];
          return m;
        };
        pp.wave_top = layer_wave(top);
        pp.wave_bottom = layer_wave(top + kLayerBlockStride);
        pp.mix_top = layer_mix(top);
        pp.mix_bottom = layer_mix(top + kLayerBlockStride);
      }
      return pp;
    };
    for (int pad = 0; pad < kPadsPerKit; ++pad) {
      k.pads[static_cast<size_t>(pad)] = parse_object(
          rec + kPadTableBase + static_cast<size_t>(pad) * kPadBlockStride,
          kPadLinkGroup,
          pad * 2);
    }
    for (int trig = 0; trig < kTriggersPerKit; ++trig) {
      k.triggers[static_cast<size_t>(trig)] = parse_object(
          rec + kTrigTableOffset + static_cast<size_t>(trig) * kTrigBlockStride,
          kTrigPadLink,
          kTrigLayerBlockBase + trig * 2);
    }
    kits.push_back(std::move(k));
  }
  return kits;
}

}  // namespace spdsx::device
