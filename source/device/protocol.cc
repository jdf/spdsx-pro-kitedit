#include "device/protocol.h"

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

}  // namespace spdsx::device
