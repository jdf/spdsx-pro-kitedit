#include "device/spdsx_device.h"

#include <chrono>
#include <stdexcept>
#include <thread>

namespace spdsx::device {

namespace {

constexpr uint8_t kFrameHead[] = {0x0d, 0x60, 0xe0};
constexpr uint8_t kFrameTail[] = {0x01, 0x00, 0x00, 0x00};
constexpr uint8_t kChDt1 = 0x07;
constexpr uint8_t kChBulk = 0x08;
constexpr uint8_t kChControl = 0x09;
constexpr size_t kFrameHeaderSize = 20;
constexpr uint32_t kMaxFrameLen = 4096;
// Bulk block frames carry ~64KB payloads; headroom over that.
constexpr uint32_t kMaxBulkFrameLen = 1 << 17;

// hdr[3] selects a channel by message family: 41 10 = DT1 params,
// 41 6a = control/status, 41 6c = bulk block transfer.
uint8_t ChannelFor(const Bytes& payload) {
  if (payload.size() >= 3 && payload[1] == 0x41) {
    if (payload[2] == 0x10) {
      return kChDt1;
    }
    if (payload[2] == 0x6A) {
      return kChControl;
    }
    if (payload[2] == 0x6C) {
      return kChBulk;
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

std::string FindDevicePort()
{
  const auto candidates = ListUsbModemPorts();
  if (candidates.empty()) {
    throw std::runtime_error(
        "no /dev/cu.usbmodem* ports found (device plugged in?)");
  }
  for (const auto& path : candidates) {
    try {
      SpdsxDevice dev(path);
      if (!dev.Ping().empty()) {
        return path;
      }
    } catch (const std::exception&) {
      // Busy or vanished; try the next node.
    }
  }
  throw std::runtime_error(
      "no SPD-SX PRO answered (is the official app still open?)");
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

std::string SpdsxDevice::FirmwareField(uint8_t field) {
  // Exact 32-byte cat-0x17 request from the capture; byte 15 selects the
  // field. Reply: f0 41 6a 02 00 00 00 00 17 40 00 00 00 <len> <ascii> f7.
  Bytes p = {0xF0, 0x41, 0x6A, 0x03, 0x17, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x40, 0x00, 0x00, 0x00, 0x00, field, 0x00, 0x00, 0x00, 0x01, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF7};
  const Bytes r = Command(p);
  if (r.size() < 15 || r[8] != 0x17) {
    return {};
  }
  const size_t len = r[13];
  std::string s;
  for (size_t i = 14; i < 14 + len && i + 1 < r.size(); ++i) {
    if (r[i] >= 0x20 && r[i] < 0x7F) {
      s += static_cast<char>(r[i]);
    }
  }
  return s;
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

Bytes SpdsxDevice::ReadBulkFrame(double idle_timeout, double body_timeout) {
  // Short wait for the frame header (its absence means the device has
  // gone idle), then a longer wait for a possibly ~64KB body.
  const Bytes hdr = port_.ReadExact(kFrameHeaderSize, idle_timeout);
  if (hdr.size() < kFrameHeaderSize) {
    return {};
  }
  const uint32_t len = static_cast<uint32_t>(hdr[16])
      | (static_cast<uint32_t>(hdr[17]) << 8)
      | (static_cast<uint32_t>(hdr[18]) << 16)
      | (static_cast<uint32_t>(hdr[19]) << 24);
  if (len == 0 || len > kMaxBulkFrameLen) {
    return {};
  }
  return port_.ReadExact(len, body_timeout);
}

Bytes SpdsxDevice::DumpBank(uint8_t bank, const BlockCallback& on_block,
    double idle_timeout, double block_timeout) {
  // The official app's load handshake (decoded 2026-07-12): PREPARE every
  // bank, then BEGIN -> repeated READ (each yields a batch of 6c 02 data
  // blocks, the device advancing its own cursor) -> END. We loop READ
  // until a request yields no blocks rather than pre-computing offsets.
  for (uint8_t b : {kBankKits, kBankSamples, kBankMeta, kBankConfig}) {
    Command(BulkRequest(kBulkPrepare, b, 0));  // reply: 6c 7a size ack
  }
  Command(BulkRequest(kBulkBegin, bank, 0));  // reply: 6c 7a

  Bytes image;
  constexpr int kMaxChunks = 4096;  // safety cap against a stuck loop
  for (int chunk = 0; chunk < kMaxChunks; ++chunk) {
    port_.Write(Wrap(BulkRequest(kBulkRead, bank, kBulkNextChunk)));
    int blocks_this_chunk = 0;
    for (;;) {
      const Bytes payload = ReadBulkFrame(idle_timeout, block_timeout);
      if (payload.size() < 4 || payload[1] != 0x41 || payload[2] != 0x6C) {
        break;  // idle or a non-bulk frame ends this batch
      }
      if (payload[3] != 0x02) {
        break;  // 6c 7a (or other) — end of this batch's data
      }
      image.insert(image.end(), payload.begin(), payload.end());
      if (on_block) {
        on_block(payload);
      }
      ++blocks_this_chunk;
    }
    if (blocks_this_chunk == 0) {
      break;  // bank exhausted
    }
  }

  Command(BulkRequest(kBulkEnd, bank, 0));  // reply: 6c 7a
  return image;
}

}  // namespace spdsx::device
