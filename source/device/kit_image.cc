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
    kits.push_back(std::move(k));
  }
  return kits;
}

}  // namespace spdsx::device
