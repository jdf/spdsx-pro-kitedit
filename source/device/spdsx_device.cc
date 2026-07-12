#include "device/spdsx_device.h"

#include <chrono>
#include <thread>

namespace spdsx::device {

namespace {

constexpr uint8_t kFrameHead[] = {0x0d, 0x60, 0xe0};
constexpr uint8_t kFrameTail[] = {0x01, 0x00, 0x00, 0x00};
constexpr uint8_t kChDt1 = 0x07;
constexpr uint8_t kChControl = 0x09;
constexpr size_t kFrameHeaderSize = 20;
constexpr uint32_t kMaxFrameLen = 4096;

// hdr[3] selects a channel by message family: 41 10 = DT1 params,
// 41 6a = control/status.
uint8_t ChannelFor(const Bytes& payload) {
  if (payload.size() >= 3 && payload[1] == 0x41) {
    if (payload[2] == 0x10) {
      return kChDt1;
    }
    if (payload[2] == 0x6A) {
      return kChControl;
    }
  }
  return kChDt1;
}

uint32_t ReadLen(const Bytes& buf) {
  return static_cast<uint32_t>(buf[16]) |
      (static_cast<uint32_t>(buf[17]) << 8) |
      (static_cast<uint32_t>(buf[18]) << 16) |
      (static_cast<uint32_t>(buf[19]) << 24);
}

}  // namespace

Bytes Wrap(const Bytes& payload) {
  Bytes out;
  out.insert(out.end(), kFrameHead, kFrameHead + sizeof(kFrameHead));
  out.push_back(ChannelFor(payload));
  out.insert(out.end(), 8, 0x00);  // app-side junk the device ignores
  out.insert(out.end(), kFrameTail, kFrameTail + sizeof(kFrameTail));
  const uint32_t len = static_cast<uint32_t>(payload.size());
  out.push_back(static_cast<uint8_t>(len & 0xFF));
  out.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
  out.push_back(static_cast<uint8_t>((len >> 16) & 0xFF));
  out.push_back(static_cast<uint8_t>((len >> 24) & 0xFF));
  out.insert(out.end(), payload.begin(), payload.end());
  return out;
}

Bytes Unwrap(const Bytes& frame) {
  if (frame.size() < kFrameHeaderSize) {
    return {};
  }
  const uint32_t len = ReadLen(frame);
  const size_t avail = frame.size() - kFrameHeaderSize;
  const size_t take = len < avail ? len : avail;
  return Bytes(frame.begin() + kFrameHeaderSize,
      frame.begin() + kFrameHeaderSize + take);
}

SpdsxDevice::SpdsxDevice(const std::string& port) : port_(port) {}

Bytes SpdsxDevice::ReadFrame(double timeout_seconds) {
  const Bytes hdr = port_.ReadExact(kFrameHeaderSize, timeout_seconds);
  if (hdr.size() < kFrameHeaderSize) {
    return {};
  }
  const uint32_t len = ReadLen(hdr);
  if (len == 0 || len > kMaxFrameLen) {
    return {};
  }
  return port_.ReadExact(len, timeout_seconds);
}

Bytes SpdsxDevice::Command(const Bytes& payload, double timeout_seconds) {
  port_.Write(Wrap(payload));
  return ReadFrame(timeout_seconds);
}

void SpdsxDevice::Send(const Bytes& payload) { port_.Write(Wrap(payload)); }

Bytes SpdsxDevice::Ping() {
  Bytes p = {0xF0, 0x41, 0x6A, 0x03, 0x16};
  p.insert(p.end(), 11, 0x00);
  p.push_back(0xF7);
  return Command(p);
}

Bytes SpdsxDevice::StatusRequest(uint8_t category) {
  // UNSOLVED: both guessed layouts failed live. A category at offset 5
  // (ping-shaped) is ignored — the device answers as if pinged; this
  // reply-shaped attempt (four zeros, model id, category) gets no reply
  // at all. The real layout needs a frida capture of the official app's
  // startup handshake, which polls categories 0x15-0x17 continuously.
  Bytes p = {0xF0, 0x41, 0x6A, 0x03, 0x00, 0x00, 0x00, 0x00, 0x16,
      category};
  p.insert(p.end(), 6, 0x00);
  p.push_back(0xF7);
  return Command(p);
}

Bytes SpdsxDevice::SelectKit(int kit) {
  return Command(Dt1(kKitSelectAddr, EncodeKit(kit)));
}

Bytes SpdsxDevice::SelectObject(ObjectKind kind, int index) {
  return Command(Dt1(kObjectSelectAddr, {SelectValue(kind, index)}));
}

void SpdsxDevice::SetPadLink(int kit, ObjectKind kind, int index, int group,
    double pace_seconds) {
  SelectObject(kind, index);  // replies; drains
  Send(Dt1(PadLinkAddr(kind, index, kit),
      {static_cast<uint8_t>(group & 0x7F)}));
  std::this_thread::sleep_for(std::chrono::duration<double>(pace_seconds));
}

void SpdsxDevice::SetKitName(int kit, const std::string& name,
    double pace_seconds) {
  SelectKit(kit);  // current-kit-relative; replies, drains
  for (int i = 0; i < kKitNameLength; ++i) {
    const uint8_t ch = i < static_cast<int>(name.size())
        ? static_cast<uint8_t>(name[i])
        : 0x20;  // space-pad
    Send(Dt1(KitNameAddr(i), {ch}));
    std::this_thread::sleep_for(std::chrono::duration<double>(pace_seconds));
  }
}

void SpdsxDevice::SetPadWave(int kit, int pad, PadSlot slot, int sample,
    double pace_seconds) {
  SelectKit(kit);
  SelectObject(ObjectKind::kPad, pad);  // focus; replies, drains
  Send(Dt1(PadWaveAddr(slot), NibbleEncode(sample)));
  std::this_thread::sleep_for(std::chrono::duration<double>(pace_seconds));
}

}  // namespace spdsx::device
