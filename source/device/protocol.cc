#include "device/protocol.h"

#include <algorithm>
#include <stdexcept>

namespace spdsx::device {

const Bytes kKitSelectAddr = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const Bytes kObjectSelectAddr = {0x28, 0x00, 0x00, 0x00};

uint8_t Checksum(const Bytes& body) {
  int sum = 0;
  for (uint8_t b : body) {
    sum += b;
  }
  return static_cast<uint8_t>((128 - (sum % 128)) & 0x7F);
}

Bytes Dt1(const Bytes& address, const Bytes& data) {
  Bytes body;
  body.reserve(address.size() + data.size());
  body.insert(body.end(), address.begin(), address.end());
  body.insert(body.end(), data.begin(), data.end());

  Bytes msg = {0xF0, 0x41, kDeviceId};
  for (uint8_t b : kModelId) {
    msg.push_back(b);
  }
  msg.push_back(kDt1);
  msg.insert(msg.end(), body.begin(), body.end());
  msg.push_back(Checksum(body));
  msg.push_back(0xF7);
  return msg;
}

Bytes EncodeKit(int kit) {
  if (kit < 1 || kit > 200) {
    throw std::out_of_range("kit must be 1-200");
  }
  const int v = kit - 1;
  return {static_cast<uint8_t>((v >> 4) & 0x7F), static_cast<uint8_t>(v & 0x0F)};
}

uint8_t SelectValue(ObjectKind kind, int index) {
  if (kind == ObjectKind::kPad) {
    if (index < 1 || index > 9) {
      throw std::out_of_range("pad 1-9");
    }
    return static_cast<uint8_t>(index - 1);
  }
  if (index < 1 || index > 8) {
    throw std::out_of_range("trigger 1-8");
  }
  return static_cast<uint8_t>(8 + index);
}

Bytes PadLinkPrefix(int kit) {
  if (kit < 1 || kit > 200) {
    throw std::out_of_range("kit 1-200");
  }
  const int flat = 512 + 2 * (kit - 1);
  return {static_cast<uint8_t>((flat >> 7) & 0x7F),
      static_cast<uint8_t>(flat & 0x7F)};
}

Bytes PadLinkAddr(ObjectKind kind, int index, int kit) {
  Bytes addr = PadLinkPrefix(kit);
  if (kind == ObjectKind::kPad) {
    if (index < 1 || index > 9) {
      throw std::out_of_range("pad 1-9");
    }
    addr.push_back(static_cast<uint8_t>(0x1F + index));
    addr.push_back(0x0D);
  } else {
    if (index < 1 || index > 8) {
      throw std::out_of_range("trigger 1-8");
    }
    addr.push_back(static_cast<uint8_t>(0x28 + index));
    addr.push_back(0x0C);
  }
  return addr;
}

Bytes NibbleEncode(int value) {
  if (value < 0 || value > 0xFFFF) {
    throw std::out_of_range("nibble-encoded value must be 0-65535");
  }
  return {static_cast<uint8_t>((value >> 12) & 0x0F),
      static_cast<uint8_t>((value >> 8) & 0x0F),
      static_cast<uint8_t>((value >> 4) & 0x0F),
      static_cast<uint8_t>(value & 0x0F)};
}

Bytes KitNameAddr(int i) {
  if (i < 0 || i >= kKitNameLength) {
    throw std::out_of_range("kit-name index 0-15");
  }
  return {0x06, 0x00, 0x00, static_cast<uint8_t>(i)};
}

Bytes PadWaveAddr(PadSlot slot) {
  const uint8_t param = slot == PadSlot::kTop ? 0x4C : 0x4D;
  return {0x06, 0x00, param, 0x01};
}

Bytes BulkRequest(uint8_t sub, uint8_t bank, uint32_t arg) {
  return {0xF0, 0x41, 0x6C, 0x03, sub, 0x00, 0x00, 0x00, 0x00, bank, 0x00,
      0x00, static_cast<uint8_t>(arg & 0xFF),
      static_cast<uint8_t>((arg >> 8) & 0xFF),
      static_cast<uint8_t>((arg >> 16) & 0xFF),
      static_cast<uint8_t>((arg >> 24) & 0xFF), 0xF7};
}

Bytes BulkReadRequest(uint8_t bank) {
  return BulkRequest(kBulkPrepare, bank, 0);
}

std::vector<BulkBlock> SplitBulkImage(const Bytes& image) {
  static const Bytes kMarker = {0xF0, 0x41, 0x6C, 0x02};
  std::vector<BulkBlock> blocks;
  size_t i = 0;
  while (i + kMarker.size() <= image.size()) {
    if (!std::equal(kMarker.begin(), kMarker.end(), image.begin() + i)) {
      ++i;
      continue;
    }
    // Bank id is payload byte 8 (past `f0 41 6c 02 00 00 00 00`).
    const uint8_t bank = i + 8 < image.size() ? image[i + 8] : 0;
    // The block runs to the next marker, or the end of the image.
    size_t next = i + kMarker.size();
    while (next + kMarker.size() <= image.size()
        && !std::equal(kMarker.begin(), kMarker.end(), image.begin() + next))
    {
      ++next;
    }
    if (next + kMarker.size() > image.size()) {
      next = image.size();
    }
    blocks.push_back({bank, i, next - i});
    i = next;
  }
  return blocks;
}

Bytes SampleRecordAddr(int index, int offset) {
  const uint32_t v = 0x2000000u
      + static_cast<uint32_t>(index) * 256u
      + static_cast<uint32_t>(offset);
  return {static_cast<uint8_t>((v >> 21) & 0x7F),
      static_cast<uint8_t>((v >> 14) & 0x7F),
      static_cast<uint8_t>((v >> 7) & 0x7F),
      static_cast<uint8_t>(v & 0x7F)};
}

namespace {

// Copies an ASCII string into a fixed-width field, space-padded and
// truncated — the form the directory records store names in.
void PutField(Bytes& b, size_t at, size_t width, const std::string& s) {
  for (size_t i = 0; i < width; ++i) {
    b[at + i] = i < s.size() ? static_cast<uint8_t>(s[i]) : 0x20;
  }
}

}  // namespace

Bytes SampleBaseRecord(int frames) {
  static const uint8_t kTemplate[151] = {
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x7f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x20, 0x20, 0x20, 0x20,
      0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x04, 0x0b, 0x00};
  Bytes b(kTemplate, kTemplate + sizeof(kTemplate));
  b[0x0c] = static_cast<uint8_t>(frames / 4096);
  return b;
}

Bytes SampleNameRecord(const std::string& wavename,
    const std::string& filename, uint32_t content_hash) {
  Bytes b(140, 0x00);
  PutField(b, 0x04, 16, wavename);
  PutField(b, 0x14, 100, filename);
  const uint8_t kConst[12] = {0x00, 0x04, 0x0b, 0x00, 0x5a, 0x34, 0x54, 0x32,
      0x33, 0x39, 0x33, 0x20};  // "\0\4\v\0Z4T2393 "
  std::copy(kConst, kConst + 12, b.begin() + 0x78);
  for (int i = 0; i < 8; ++i) {
    b[0x84 + i] = static_cast<uint8_t>((content_hash >> (28 - 4 * i)) & 0x0F);
  }
  return b;
}

}  // namespace spdsx::device
