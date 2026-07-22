#include "device/spdsx_device.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <thread>

#include "device/sample_image.h"

namespace spdsx::device {

namespace {

constexpr uint8_t kFrameHead[] = {0x0d, 0x60, 0xe0};
constexpr uint8_t kFrameTail[] = {0x01, 0x00, 0x00, 0x00};
constexpr uint8_t kChFile = 0x06;
constexpr uint8_t kChDt1 = 0x07;
constexpr uint8_t kChBulk = 0x08;
constexpr uint8_t kChControl = 0x09;
constexpr size_t kFrameHeaderSize = 20;
constexpr uint32_t kMaxFrameLen = 4096;
// Bulk block frames carry ~64KB payloads; headroom over that.
constexpr uint32_t kMaxBulkFrameLen = 1 << 17;

// hdr[3] selects a channel by message family: 41 10 = DT1 params,
// 41 6a = control/status, 41 6c = bulk block transfer, 41 7a = remote
// file transfer (wave export).
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
    if (payload[2] == 0x7A) {
      return kChFile;
    }
  }
  return kChDt1;
}

uint32_t ReadLen(const Bytes& buf) {
  return static_cast<uint32_t>(buf[16]) | (static_cast<uint32_t>(buf[17]) << 8)
      | (static_cast<uint32_t>(buf[18]) << 16)
      | (static_cast<uint32_t>(buf[19]) << 24);
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

std::string FindDevicePort(PortBackend& ports) {
  const auto candidates = ports.ListCandidates();
  if (candidates.empty()) {
    throw std::runtime_error(
        "no candidate serial ports found "
        "(device plugged in?)");
  }
  for (const auto& path : candidates) {
    try {
      const std::unique_ptr<SerialPort> serial = ports.Open(path);
      SpdsxDevice dev(serial.get());
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

SpdsxDevice::SpdsxDevice(SerialPort* port)
    : port_(port) {}

Bytes SpdsxDevice::ReadFrame(double timeout_seconds) {
  const Bytes hdr = port_->ReadExact(kFrameHeaderSize, timeout_seconds);
  if (hdr.size() < kFrameHeaderSize) {
    return {};
  }
  const uint32_t len = ReadLen(hdr);
  if (len == 0 || len > kMaxFrameLen) {
    return {};
  }
  return port_->ReadExact(len, timeout_seconds);
}

Bytes SpdsxDevice::Command(const Bytes& payload, double timeout_seconds) {
  port_->Write(Wrap(payload));
  return ReadFrame(timeout_seconds);
}

void SpdsxDevice::Send(const Bytes& payload) {
  port_->Write(Wrap(payload));
}

Bytes SpdsxDevice::Ping() {
  Bytes p = {0xF0, 0x41, 0x6A, 0x03, 0x16};
  p.insert(p.end(), 11, 0x00);
  p.push_back(0xF7);
  return Command(p);
}

std::string SpdsxDevice::FirmwareField(uint8_t field) {
  // Exact 32-byte cat-0x17 request from the capture; byte 15 selects the
  // field. Reply: f0 41 6a 02 00 00 00 00 17 40 00 00 00 <len> <ascii> f7.
  Bytes p = {0xF0, 0x41, 0x6A, 0x03, 0x17,  0x00, 0x00, 0x00, 0x00, 0x00, 0x40,
             0x00, 0x00, 0x00, 0x00, field, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
             0x00, 0x00, 0x00, 0x00, 0x00,  0x00, 0x00, 0x00, 0x00, 0xF7};
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

namespace {

// A 32-byte control frame on the 0x09 family: f0 41 6a 03 <sub>, a 0x40
// marker at byte 10, and a u32 argument at byte 15 (the position the
// firmware-version query and the sample-delete both use).
Bytes ControlFrame(uint8_t sub, uint32_t arg) {
  Bytes p(32, 0x00);
  p[0] = 0xF0;
  p[1] = 0x41;
  p[2] = 0x6A;
  p[3] = 0x03;
  p[4] = sub;
  p[10] = 0x40;
  p[15] = static_cast<uint8_t>(arg);
  p[16] = static_cast<uint8_t>(arg >> 8);
  p[17] = static_cast<uint8_t>(arg >> 16);
  p[18] = static_cast<uint8_t>(arg >> 24);
  p[31] = 0xF7;
  return p;
}

// A short 17-byte control frame with no argument (commit begin/poll).
Bytes ShortControl(uint8_t sub) {
  Bytes p(17, 0x00);
  p[0] = 0xF0;
  p[1] = 0x41;
  p[2] = 0x6A;
  p[3] = 0x03;
  p[4] = sub;
  p[16] = 0xF7;
  return p;
}

}  // namespace

bool SpdsxDevice::Commit(absl::FunctionRef<bool()> should_abort) {
  Command(ShortControl(0x21));  // begin; device acks 6a 7a
  return PollCommitted(should_abort);
}

bool SpdsxDevice::CommitUploadBatch(absl::FunctionRef<bool()> should_abort) {
  // The import commit carries two extra control messages between begin and
  // the poll — 6a 0c arg 1 and 6a 02 — in every capture of the official
  // app's import (synthupload-1.log, import-multi-1.log). A plain WRITE or
  // a delete commits without them (writecmd-*, fileops-1.log).
  Command(ShortControl(0x21));
  Command(ControlFrame(0x0C, 1));
  Command(ShortControl(0x02));
  return PollCommitted(should_abort);
}

bool SpdsxDevice::PollCommitted(absl::FunctionRef<bool()> should_abort) {
  // Poll until the device reports the flash write done — no time limit. A
  // batch commit's duration scales with what was staged and can be many
  // seconds; a timeout that fired mid-commit both misreported success and
  // (when the port was then reopened) could interrupt the flash. The caller
  // may pass should_abort to stop waiting (e.g. a user Abort button); that
  // returns false without any claim the commit finished.
  for (;;) {
    // Reply: f0 41 6a 02 .. 22 40 00 00 00 04 <status LE32> f7.
    const Bytes r = Command(ShortControl(0x22));
    if (r.size() >= 18 && r[8] == 0x22 && r[14] == 0x01) {
      return true;
    }
    if (should_abort()) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }
}

void SpdsxDevice::DeleteWave(int sample_index) {
  // The official app's full delete (fileops-1.log): query the slot and the
  // last registered index, delete inside a session bracket, then two status
  // queries before the session close and the commit.
  Command(ControlFrame(0x0C, static_cast<uint32_t>(sample_index)));
  Command(ShortControl(0x0D));
  Command(ControlFrame(0x09, 1));  // begin session
  Command(ControlFrame(0x1D, static_cast<uint32_t>(sample_index)));
  // When the deleted wave was assigned to pads, the device follows the ack
  // with unsolicited DT1 notifications (a kit-select echo, the assignments
  // being cleared). Drain until quiet, or the next command reads one as its
  // own ack and the stream desyncs.
  while (!ReadFrame(0.3).empty()) {
  }
  Command(ShortControl(0x18));
  Command(ShortControl(0x23));
  Command(ControlFrame(0x09, 0));  // end session
  Commit();
}

void SpdsxDevice::SelectKit(int kit) {
  // A DT1 like any other: the device never acks it (no capture has a reply
  // on the DT1 channel), so waiting would just stall the caller.
  Send(Dt1(kKitSelectAddr, EncodeKit(kit)));
}

void SpdsxDevice::SelectObject(ObjectKind kind, int index) {
  Send(Dt1(kObjectSelectAddr, {SelectValue(kind, index)}));
}

void SpdsxDevice::SetPadLink(
    int kit, ObjectKind kind, int index, int group, double pace_seconds) {
  SelectObject(kind, index);  // focus; fire-and-forget like every DT1
  std::this_thread::sleep_for(std::chrono::duration<double>(pace_seconds));
  Send(
      Dt1(PadLinkAddr(kind, index, kit), {static_cast<uint8_t>(group & 0x7F)}));
  std::this_thread::sleep_for(std::chrono::duration<double>(pace_seconds));
}

void SpdsxDevice::SetKitName(int kit,
                             const std::string& name,
                             double pace_seconds) {
  // The kit is encoded in the write address (KitNameAddr), so no kit
  // select is needed to target it.
  for (int i = 0; i < kKitNameLength; ++i) {
    const uint8_t ch = i < static_cast<int>(name.size())
        ? static_cast<uint8_t>(name[i])
        : 0x20;  // space-pad
    Send(Dt1(KitNameAddr(kit, i), {ch}));
    std::this_thread::sleep_for(std::chrono::duration<double>(pace_seconds));
  }
}

void SpdsxDevice::SetPadWave(
    int kit, int pad, PadSlot slot, int sample, double pace_seconds) {
  // Kit and pad+layer are both encoded in the address, so no kit select
  // or pad focus is needed (the app assigns waves without either). Write
  // the wave number, then the companion "slot in use" flag.
  Send(Dt1(PadWaveAddr(kit, pad, slot), NibbleEncode(sample)));
  std::this_thread::sleep_for(std::chrono::duration<double>(pace_seconds));
  Send(Dt1(PadWaveEnableAddr(kit, pad, slot), {0x01}));
  std::this_thread::sleep_for(std::chrono::duration<double>(pace_seconds));
}

void SpdsxDevice::SetPadLayerParams(int kit,
                                    int pad,
                                    const PadDeviceParams& params,
                                    double pace_seconds) {
  // Kit and pad are both encoded in the write address, so no kit select
  // is needed; focus the pad object (the app does before param edits).
  SelectObject(ObjectKind::kPad, pad);  // focus; fire-and-forget
  std::this_thread::sleep_for(std::chrono::duration<double>(pace_seconds));
  auto write = [&](int param, uint8_t value) {
    Send(Dt1(PadParamAddr(kit, pad, param), {value}));
    std::this_thread::sleep_for(std::chrono::duration<double>(pace_seconds));
  };
  write(0x00, params.layer_mode);
  write(0x01, params.fade_point);
  write(0x02, params.fade_end);
  write(0x03, params.dynamics);
  write(0x04, params.dynamics_curve);
  write(0x05, params.fixed_velocity);
  write(0x07, params.hi_hat_volume);
  write(0x08, params.hi_hat_fade_in);
  write(0x09, params.hi_hat_decay);
  write(0x13, params.trigger_reserve);
}

Bytes SpdsxDevice::ReadBulkFrame(double idle_timeout, double body_timeout) {
  // Short wait for the frame header (its absence means the device has
  // gone idle), then a longer wait for a possibly ~64KB body.
  const Bytes hdr = port_->ReadExact(kFrameHeaderSize, idle_timeout);
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
  return port_->ReadExact(len, body_timeout);
}

namespace {

// A remote-file request: f0 41 7a 03 <sub> + <body> + f7.
Bytes FileRequest(uint8_t sub, const Bytes& body) {
  Bytes p = {0xF0, 0x41, 0x7A, 0x03, sub};
  p.insert(p.end(), body.begin(), body.end());
  p.push_back(0xF7);
  return p;
}

// Every f0 41 7a 02 data frame is a 14-byte header then payload bytes.
constexpr size_t kFileFrameHeader = 14;

void AppendStr(Bytes& b, const std::string& s) {
  b.insert(b.end(), s.begin(), s.end());
}

// A length-prefixed path body (03/0a, 03/0c, 03/19): 9 zero bytes, the
// path length INCLUDING its null, the path, then that null.
Bytes PathBody(const std::string& path) {
  Bytes b(9, 0x00);
  b.push_back(static_cast<uint8_t>(path.size() + 1));
  AppendStr(b, path);
  b.push_back(0x00);
  return b;
}

// Open-for-write body (03/00): like PathBody but with the write-mode
// flag 0c 02 at bytes 3-4.
Bytes OpenWriteBody(const std::string& path) {
  Bytes b = PathBody(path);
  b[3] = 0x0C;
  b[4] = 0x02;
  return b;
}

// Create-entry body (03/18): 8 zeros, the 03 10 marker, then TWO fixed
// 200-byte fields — the destination `.SMP` path and the working `.TMP`
// path (import-multi-1.log). An earlier one-400-byte-field version left the
// .TMP half zero and the file never persisted.
Bytes CreateBody(const std::string& smp_path, const std::string& tmp_path) {
  Bytes b(8, 0x00);
  b.push_back(0x03);
  b.push_back(0x10);
  Bytes smp_field(200, 0x00);
  std::copy(smp_path.begin(), smp_path.end(), smp_field.begin());
  Bytes tmp_field(200, 0x00);
  std::copy(tmp_path.begin(), tmp_path.end(), tmp_field.begin());
  b.insert(b.end(), smp_field.begin(), smp_field.end());
  b.insert(b.end(), tmp_field.begin(), tmp_field.end());
  return b;
}

// A seek/read body (03/07, 03/04): the offset or byte count as three
// big-endian base-128 septets at bytes 7..9 — 512 is 00 04 00, 524288 is
// 20 00 00 (waveexport-1/2.log). Zero is the all-zero body.
Bytes SeptetBody(uint32_t n) {
  Bytes b(11, 0x00);
  b[7] = static_cast<uint8_t>((n >> 14) & 0x7F);
  b[8] = static_cast<uint8_t>((n >> 7) & 0x7F);
  b[9] = static_cast<uint8_t>(n & 0x7F);
  return b;
}

// A write-data frame body (03/06): 5 zero bytes, the 40 00 marker, the
// data length as 3 big-endian base-128 (7-bit) septets, then the data.
// The length encoding was cracked with controlled uploads (2026-07-13):
// e.g. 8192 -> 00 40 00, 50834 -> 03 0d 12. A wrong length desyncs the
// device (it reads the wrong number of data bytes) and hangs it.
Bytes WriteBody(const Bytes& data) {
  const uint32_t n = static_cast<uint32_t>(data.size());
  Bytes b(5, 0x00);
  b.push_back(0x40);
  b.push_back(0x00);
  b.push_back(static_cast<uint8_t>((n >> 14) & 0x7F));
  b.push_back(static_cast<uint8_t>((n >> 7) & 0x7F));
  b.push_back(static_cast<uint8_t>(n & 0x7F));
  b.insert(b.end(), data.begin(), data.end());
  return b;
}

}  // namespace

Bytes SpdsxDevice::ReadRemoteWave(int sample_index,
                                  const ProgressCallback& on_progress,
                                  double idle_timeout) {
  const std::string path = RemoteWavePath(sample_index);
  // The remote-file session bracket: the official app opens a session
  // (6a 09 arg 1 + 6a 0a) before EVERY export's OPEN and closes it
  // (6a 09 arg 0) after the CLOSE (waveexport-1/2.log).
  Command(ControlFrame(0x09, 1));
  Command(ShortControl(0x0A));

  const Bytes ack = Command(FileRequest(0x00, PathBody(path)));
  if (ack.size() < 4 || ack[3] != 0x7A) {
    Command(ControlFrame(0x09, 0));
    throw std::runtime_error("device rejected OPEN of " + path
                             + " (preload wave, or not present?)");
  }

  Bytes smp;
  size_t total = 0;  // known once STAT answers
  // One READ request for exactly `want` bytes, collecting the data frames
  // it produces. Each data frame is a 14-byte header, the payload, then a
  // trailing 0xf7 (the SysEx terminator) — drop BOTH ends. Missing the f7
  // leaves an odd byte per frame that shifts every following frame's
  // sample alignment (audible as alternating audio/noise).
  auto read_chunk = [&](uint32_t want) {
    port_->Write(Wrap(FileRequest(0x04, SeptetBody(want))));
    size_t got = 0;
    while (got < want) {
      const Bytes frame = ReadBulkFrame(idle_timeout, 5.0);
      if (frame.size() <= kFileFrameHeader + 1 || frame[3] != 0x02) {
        break;  // drained (idle) or a non-data frame
      }
      got += frame.size() - kFileFrameHeader - 1;
      smp.insert(smp.end(), frame.begin() + kFileFrameHeader, frame.end() - 1);
      if (on_progress && total > 0) {
        on_progress(std::min(smp.size(), total), total);
      }
    }
    return got;
  };
  auto abandon = [&](const std::string& why) {
    Command(FileRequest(0x03, Bytes(11, 0x00)));  // CLOSE
    Command(ControlFrame(0x09, 0));
    throw std::runtime_error(why);
  };

  // The official app's order: read the 512-byte RFWV header first, THEN
  // stat, then fetch the remainder in exact-size requests (at most 512 KiB
  // each, the size the app asks for). Some factory preloads have no
  // exportable file: nothing answers the header read.
  Command(FileRequest(0x07, SeptetBody(0)));  // SEEK 0
  if (read_chunk(kRfwvHeaderSize) == 0) {
    abandon("wave " + std::to_string(sample_index)
            + " has no exportable file (preload?): " + path);
  }

  // STAT: reply is f0 41 7a 02 .. 08 <u32 attr> <u32 size> f7; the file
  // size is the second u32 (offset 18).
  const Bytes sr = Command(FileRequest(0x13, Bytes(11, 0x00)));
  if (std::getenv("SPDSX_TRACE") != nullptr) {
    std::string hex;
    char buf[4];
    for (uint8_t b : sr) {
      std::snprintf(buf, sizeof(buf), "%02x ", b);
      hex += buf;
    }
    std::fprintf(
        stderr, "  STAT reply (%zu bytes): %s\n", sr.size(), hex.c_str());
  }
  if (sr.size() < 22 || sr[3] != 0x02) {
    abandon("wave " + std::to_string(sample_index)
            + " has no exportable file (preload?): " + path);
  }
  total = static_cast<uint32_t>(sr[18]) | static_cast<uint32_t>(sr[19]) << 8
      | static_cast<uint32_t>(sr[20]) << 16
      | static_cast<uint32_t>(sr[21]) << 24;
  if (on_progress) {
    on_progress(std::min(smp.size(), total), total);
  }

  constexpr uint32_t kReadChunk = 512 * 1024;
  Command(FileRequest(0x07, SeptetBody(kRfwvHeaderSize)));  // SEEK past it
  while (smp.size() < total) {
    const uint32_t want = static_cast<uint32_t>(
        std::min<size_t>(kReadChunk, total - smp.size()));
    if (read_chunk(want) == 0) {
      break;  // device stopped sending; avoid an infinite loop
    }
  }

  Command(FileRequest(0x03, Bytes(11, 0x00)));  // CLOSE
  Command(ControlFrame(0x09, 0));  // session close
  if (smp.size() > total) {
    smp.resize(total);  // trim a batch that overshot
  }
  return smp;
}

void SpdsxDevice::WriteRemoteFile(int sample_index, const Bytes& smp) {
  if (smp.size() <= kRfwvHeaderSize) {
    throw std::runtime_error("smp too small to have header + PCM");
  }
  const std::string smp_path = RemoteWavePath(sample_index);
  const std::string tmp_path = smp_path.substr(0, smp_path.size() - 4) + ".TMP";
  const std::string dir = smp_path.substr(0, smp_path.rfind('/'));
  const Bytes header(smp.begin(), smp.begin() + kRfwvHeaderSize);
  const Bytes pcm(smp.begin() + kRfwvHeaderSize, smp.end());

  const bool trace = std::getenv("SPDSX_TRACE") != nullptr;
  auto trace_ack = [&](const char* label, const Bytes& ack) {
    if (!trace) {
      return;
    }
    std::string hex;
    char buf[4];
    for (size_t i = 0; i < ack.size() && i < 16; ++i) {
      std::snprintf(buf, sizeof(buf), "%02x ", ack[i]);
      hex += buf;
    }
    std::fprintf(stderr, "  %-14s -> %s\n", label, hex.c_str());
  };
  auto step = [&](const char* label, const Bytes& req) {
    // Flash-backed file ops can take a few hundred ms to answer.
    const Bytes ack = Command(req, 3.0);
    trace_ack(label, ack);
    return ack;
  };
  // Writes file data in 03/06 frames. The device caps a single write-data
  // frame at 64 KiB of payload, so a larger blob goes as a back-to-back
  // burst of 64 KiB frames answered by ONE ack — exactly how the official
  // app writes (import-large-1.log: a 243 KiB sample = three 64 KiB frames
  // plus the remainder, one write, one ack). A single oversized frame,
  // which is what we sent before, wedges the unit mid-write.
  constexpr size_t kMaxWriteChunk = 65536;
  auto write_data = [&](const char* label, const Bytes& data) {
    Bytes burst;
    for (size_t off = 0; off < data.size(); off += kMaxWriteChunk) {
      const size_t n = std::min(kMaxWriteChunk, data.size() - off);
      const Bytes frame = Wrap(FileRequest(
          0x06,
          WriteBody(Bytes(data.begin() + static_cast<long>(off),
                          data.begin() + static_cast<long>(off + n)))));
      burst.insert(burst.end(), frame.begin(), frame.end());
    }
    port_->Write(burst);
    trace_ack(label, ReadFrame(3.0));
  };
  // The directory of the .SMP (".../WAVE/DATA/D0NN") and its parent
  // (".../WAVE/DATA").
  const std::string data_dir = dir.substr(0, dir.rfind('/'));
  // The handle the device returns in a dir (0c) reply, at payload bytes
  // 4..8, that a following 0d must echo back. A listing of a directory
  // that doesn't exist answers with the handle bytes all zero.
  auto handle_of = [](const Bytes& ack) {
    Bytes h(11, 0x00);
    if (ack.size() >= 9) {
      std::copy(ack.begin() + 4, ack.begin() + 9, h.begin());
    }
    return h;
  };
  auto has_handle = [](const Bytes& h) {
    return std::any_of(
        h.begin(), h.begin() + 5, [](uint8_t b) { return b != 0; });
  };

  step("stat tmp", FileRequest(0x0A, PathBody(tmp_path)));
  step("create", FileRequest(0x18, CreateBody(smp_path, tmp_path)));
  // Ensure the D0NN directory exists, exactly as the app does: list it,
  // and when the answer carries a handle just echo it and open. Only a
  // missing directory (all-zero handle) takes the mkdir walk — list the
  // parent, echo THE PARENT's handle, mkdir, re-list (import-multi-1.log:
  // first file into a fresh D016 vs the rest). Running the walk
  // unconditionally, as this used to, echoed the missing directory's
  // all-zero handle where the official app echoes the parent's.
  Bytes h = handle_of(step("dir D", FileRequest(0x0C, PathBody(dir))));
  if (!has_handle(h)) {
    h = handle_of(step("dir DATA", FileRequest(0x0C, PathBody(data_dir))));
    step("dir 0d", FileRequest(0x0D, h));
    step("dir D", FileRequest(0x0C, PathBody(dir)));  // still missing
    step("mkdir D", FileRequest(0x09, PathBody(dir)));
    h = handle_of(step("dir D", FileRequest(0x0C, PathBody(dir))));
  }
  step("dir 0d", FileRequest(0x0D, h));
  step("open write", FileRequest(0x00, OpenWriteBody(smp_path)));
  step("seek", FileRequest(0x07, SeptetBody(kRfwvHeaderSize)));
  // Free-space query on the remote root, exactly as the official app issues
  // before the data write (import-multi-1.log). It appears to stage the file
  // so the batch's single flash commit captures it — without it, uploads
  // register metadata but their data never persists. Its reply is TWO
  // things: a 16-byte off-format fragment (0c-prefixed) our transport
  // doesn't model, then a normal ack frame — both must come off the wire,
  // or every later step reads the previous step's ack.
  port_->Write(Wrap(FileRequest(0x19, PathBody("/SPDSXREMOTE//"))));
  port_->ReadExact(16, 1.0);
  trace_ack("free space", ReadFrame(3.0));
  write_data("write pcm", pcm);
  step("seek 0", FileRequest(0x07, Bytes(11, 0x00)));
  write_data("write hdr", header);  // 512 bytes: always one frame
  step("close", FileRequest(0x03, Bytes(11, 0x00)));
}

void SpdsxDevice::RegisterWave(int sample_index,
                               int frames,
                               const std::string& wavename,
                               const std::string& filename) {
  // Replays the official app's post-write register sequence (synthupload-1
  // + import-multi-1.log): finalize the temp file, open a register slot for
  // N, write the base directory record, close the session (6a 09 arg 0),
  // then write the name record. Crucially it does NOT flash-commit here:
  // the batch commits ONCE at the end (CommitUploadBatch, in the caller).
  //
  // The two records are DT1s, which the device never acks — they go out
  // fire-and-forget exactly as the official app sends them; the session
  // close's ack doubles as the barrier after the base record. (An earlier
  // decode put a 15/16 pair here instead of the 09: those frames were the
  // official app's background status poll, interleaved by coincidence.)
  // The name record's 32-bit hash field is 0; the device ignores it.
  const std::string smp_path = RemoteWavePath(sample_index);
  const std::string tmp_path = smp_path.substr(0, smp_path.size() - 4) + ".TMP";

  auto trace = [&](const char* label, const Bytes& ack) {
    if (std::getenv("SPDSX_TRACE") == nullptr) {
      return;
    }
    std::string hex;
    char buf[4];
    for (size_t i = 0; i < ack.size() && i < 16; ++i) {
      std::snprintf(buf, sizeof(buf), "%02x ", ack[i]);
      hex += buf;
    }
    std::fprintf(stderr, "  %-14s -> %s\n", label, hex.c_str());
  };

  trace("finalize tmp", Command(FileRequest(0x0A, PathBody(tmp_path)), 3.0));
  trace("register 0b", Command(ControlFrame(0x0B, sample_index), 3.0));
  trace("register 0c", Command(ControlFrame(0x0C, sample_index), 3.0));
  Send(Dt1(SampleRecordAddr(sample_index, 0x00), SampleBaseRecord(frames)));
  trace("session 09/0", Command(ControlFrame(0x09, 0), 3.0));
  Send(Dt1(SampleRecordAddr(sample_index, 0x1B),
           SampleNameRecord(wavename, filename, /*content_hash=*/0)));
}

void SpdsxDevice::UploadWave(int sample_index,
                             const Bytes& smp,
                             const std::string& wavename,
                             const std::string& filename) {
  // Writes the wave file and registers it in the pool directory (writing
  // without registering leaves an orphan file no UI can see, so the two are
  // never done separately). Both land in WORKING state only — the caller
  // must flash-commit once after the batch (CommitUploadBatch). Committing
  // per file, as this used to, made the device wedge after a few uploads
  // (import-multi-1.log: the official app commits the whole batch once).
  // `frames` = the PER-CHANNEL frame count (the record's end point):
  // total PCM bytes / (2 bytes * channels). A mono assumption halved the
  // end point of stereo samples and cut their playback short.
  if (smp.size() <= kRfwvHeaderSize) {
    throw std::runtime_error("smp too small to have header + PCM");
  }
  const RfwvHeader header = ParseRfwvHeader(smp);
  const int channels =
      header.valid && header.channels > 0 ? header.channels : 1;
  const int frames = static_cast<int>((smp.size() - kRfwvHeaderSize)
                                      / (2u * static_cast<unsigned>(channels)));
  // The per-file session open (6a 09 arg 1 + 6a 0a), matched by the arg-0
  // close RegisterWave sends after the base record — the official app
  // brackets every file this way (import-multi-1.log).
  Command(ControlFrame(0x09, 1));
  Command(ShortControl(0x0A));
  WriteRemoteFile(sample_index, smp);
  RegisterWave(sample_index, frames, wavename, filename);
}

void SpdsxDevice::PrepareUploadBatch() {
  // The official app opens an import batch with one 6a 0d query — the last
  // registered wave index, which it uses to pick slots (ours come from the
  // bulk image, so the answer is drained and dropped). The per-file session
  // open/close lives in UploadWave/RegisterWave.
  Command(ShortControl(0x0D));
}

Bytes SpdsxDevice::DumpBank(uint8_t bank,
                            const BlockCallback& on_block,
                            double idle_timeout,
                            double block_timeout) {
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
    port_->Write(Wrap(BulkRequest(kBulkRead, bank, kBulkNextChunk)));
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
