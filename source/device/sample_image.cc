#include "device/sample_image.h"

#include <algorithm>
#include <array>
#include <cstdio>

namespace spdsx::device {

namespace {

// Offsets within a record — the binary metadata LEADS the record (see the
// header's layout note; the boundary was corrected 2026-07-22).
constexpr size_t kFramesOffset = 0x08;
constexpr size_t kCategoryOffset = 0x14;
constexpr size_t kWavenameOffset = 0x18;
constexpr size_t kWavenameLen = 16;
constexpr size_t kFilenameOffset = 0x28;
constexpr size_t kFilenameLen = 84;

// The record whose filename anchors the directory: sample 1 is always
// the first preload.
constexpr std::string_view kAnchor = "PRELOAD 00001";

constexpr std::array<std::string_view, kSampleCategoryCount> kCategories = {
    "OFF",
    "Kick",
    "Kick Proc/Elec",
    "Snare",
    "Snare Proc/Elec",
    "Cross Stick",
    "Clap",
    "Tom",
    "Tom Proc/Elec",
    "HiHat",
    "HiHat Proc/Elec",
    "Crash",
    "Ride",
    "Splash/China",
    "Cymbal Proc/Elec",
    "Percussion",
    "Percussion Elec",
    "FX",
    "Synth Hit",
    "Sub Element",
    "Loop",
    "808"};

std::string Trimmed(const Bytes& image, size_t offset, size_t len) {
  std::string s(image.begin() + static_cast<long>(offset),
                image.begin() + static_cast<long>(offset + len));
  while (!s.empty() && (s.back() == ' ' || s.back() == '\0')) {
    s.pop_back();
  }
  return s;
}

uint32_t ReadLe32(const Bytes& image, size_t offset) {
  return static_cast<uint32_t>(image[offset])
      | static_cast<uint32_t>(image[offset + 1]) << 8
      | static_cast<uint32_t>(image[offset + 2]) << 16
      | static_cast<uint32_t>(image[offset + 3]) << 24;
}

}  // namespace

std::string_view SampleCategoryName(int category) {
  if (category < 0 || category >= kSampleCategoryCount) {
    return "?";
  }
  return kCategories.at(static_cast<size_t>(category));
}

std::vector<SampleRecord> ParseSampleDir(const Bytes& clean_image) {
  std::vector<SampleRecord> records;
  const auto anchor = std::search(
      clean_image.begin(), clean_image.end(), kAnchor.begin(), kAnchor.end());
  if (anchor == clean_image.end()) {
    return records;
  }
  // The anchor is record 1's filename field. Offsets run from record 1
  // directly: with the metadata-first boundary, record 1 starts only a
  // preamble's worth of bytes into the directory, so there may be no
  // room for a whole record 0 (the sentinel) before it.
  const size_t anchor_off = static_cast<size_t>(anchor - clean_image.begin());
  if (anchor_off < kFilenameOffset) {
    return records;
  }
  const size_t rec1 = anchor_off - kFilenameOffset;
  for (int i = 1; i < kSampleSlots; ++i) {
    const size_t off =
        rec1 + static_cast<size_t>(i - 1) * kSampleRecordStride;
    if (off + kSampleRecordStride > clean_image.size()) {
      break;
    }
    SampleRecord rec;
    rec.wavename = Trimmed(clean_image, off + kWavenameOffset, kWavenameLen);
    if (rec.wavename.empty()) {
      continue;  // unoccupied slot (deleted or never used)
    }
    rec.index = i;
    rec.filename = Trimmed(clean_image, off + kFilenameOffset, kFilenameLen);
    rec.frames = ReadLe32(clean_image, off + kFramesOffset);
    rec.category =
        static_cast<int>(ReadLe32(clean_image, off + kCategoryOffset));
    records.push_back(std::move(rec));
  }
  return records;
}

namespace {

void PushLe32(Bytes& b, uint32_t v) {
  b.push_back(static_cast<uint8_t>(v));
  b.push_back(static_cast<uint8_t>(v >> 8));
  b.push_back(static_cast<uint8_t>(v >> 16));
  b.push_back(static_cast<uint8_t>(v >> 24));
}

void PushLe16(Bytes& b, uint16_t v) {
  b.push_back(static_cast<uint8_t>(v));
  b.push_back(static_cast<uint8_t>(v >> 8));
}

void PushStr(Bytes& b, const char* s) {
  for (; *s; ++s) {
    b.push_back(static_cast<uint8_t>(*s));
  }
}

// A self-contained MD5 (RFC 1321), so the JUCE-free device library can
// compute the RFWV header checksum without pulling in JUCE. Returns the
// 16-byte digest of `data`.
std::array<uint8_t, 16> Md5(const uint8_t* data, size_t len) {
  auto rotl = [](uint32_t x, int c) { return (x << c) | (x >> (32 - c)); };
  static const uint32_t K[64] = {
      0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a,
      0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
      0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340,
      0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
      0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8,
      0x676f02d9, 0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
      0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa,
      0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
      0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92,
      0xffeff47d, 0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
      0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
  static const int S[64] = {7,  12, 17, 22, 7,  12, 17, 22, 7,  12, 17, 22, 7,
                            12, 17, 22, 5,  9,  14, 20, 5,  9,  14, 20, 5,  9,
                            14, 20, 5,  9,  14, 20, 4,  11, 16, 23, 4,  11, 16,
                            23, 4,  11, 16, 23, 4,  11, 16, 23, 6,  10, 15, 21,
                            6,  10, 15, 21, 6,  10, 15, 21, 6,  10, 15, 21};
  uint32_t a0 = 0x67452301, b0 = 0xefcdab89, c0 = 0x98badcfe, d0 = 0x10325476;
  // Pad: 0x80, then zeros to 56 mod 64, then the 64-bit bit length.
  std::vector<uint8_t> msg(data, data + len);
  const uint64_t bit_len = static_cast<uint64_t>(len) * 8;
  msg.push_back(0x80);
  while (msg.size() % 64 != 56) {
    msg.push_back(0);
  }
  for (int i = 0; i < 8; ++i) {
    msg.push_back(static_cast<uint8_t>(bit_len >> (8 * i)));
  }
  for (size_t off = 0; off < msg.size(); off += 64) {
    uint32_t M[16];
    for (int i = 0; i < 16; ++i) {
      M[i] = static_cast<uint32_t>(msg[off + 4 * i])
          | static_cast<uint32_t>(msg[off + 4 * i + 1]) << 8
          | static_cast<uint32_t>(msg[off + 4 * i + 2]) << 16
          | static_cast<uint32_t>(msg[off + 4 * i + 3]) << 24;
    }
    uint32_t A = a0, B = b0, C = c0, D = d0;
    for (int i = 0; i < 64; ++i) {
      uint32_t F;
      int g;
      if (i < 16) {
        F = (B & C) | (~B & D);
        g = i;
      } else if (i < 32) {
        F = (D & B) | (~D & C);
        g = (5 * i + 1) % 16;
      } else if (i < 48) {
        F = B ^ C ^ D;
        g = (3 * i + 5) % 16;
      } else {
        F = C ^ (B | ~D);
        g = (7 * i) % 16;
      }
      F = F + A + K[i] + M[g];
      A = D;
      D = C;
      C = B;
      B = B + rotl(F, S[i]);
    }
    a0 += A;
    b0 += B;
    c0 += C;
    d0 += D;
  }
  std::array<uint8_t, 16> out;
  const uint32_t parts[4] = {a0, b0, c0, d0};
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      out[static_cast<size_t>(i * 4 + j)] =
          static_cast<uint8_t>(parts[i] >> (8 * j));
    }
  }
  return out;
}

}  // namespace

Bytes RfwvToWav(const Bytes& smp) {
  const RfwvHeader h = ParseRfwvHeader(smp);
  if (!h.valid || h.channels == 0 || h.bits_per_sample == 0
      || smp.size() <= kRfwvHeaderSize) {
    return {};
  }
  const uint32_t pcm_bytes =
      static_cast<uint32_t>(smp.size() - kRfwvHeaderSize);
  const uint16_t block_align =
      static_cast<uint16_t>(h.channels * (h.bits_per_sample / 8));
  const uint32_t byte_rate = h.sample_rate * block_align;
  Bytes wav;
  wav.reserve(44 + pcm_bytes);
  PushStr(wav, "RIFF");
  PushLe32(wav, 36 + pcm_bytes);  // file size - 8
  PushStr(wav, "WAVE");
  PushStr(wav, "fmt ");
  PushLe32(wav, 16);  // PCM fmt chunk size
  PushLe16(wav, 1);  // PCM
  PushLe16(wav, h.channels);
  PushLe32(wav, h.sample_rate);
  PushLe32(wav, byte_rate);
  PushLe16(wav, block_align);
  PushLe16(wav, h.bits_per_sample);
  PushStr(wav, "data");
  PushLe32(wav, pcm_bytes);
  wav.insert(wav.end(), smp.begin() + kRfwvHeaderSize, smp.end());
  return wav;
}

Bytes PcmToRfwv(const Bytes& pcm,
                uint32_t sample_rate,
                uint16_t channels,
                uint16_t bits_per_sample) {
  Bytes smp(kRfwvHeaderSize, 0);  // header zero-filled; overview stays 0
  smp[0] = 'R';
  smp[1] = 'F';
  smp[2] = 'W';
  smp[3] = 'V';
  auto put_le32 = [&](size_t at, uint32_t v) {
    smp[at] = static_cast<uint8_t>(v);
    smp[at + 1] = static_cast<uint8_t>(v >> 8);
    smp[at + 2] = static_cast<uint8_t>(v >> 16);
    smp[at + 3] = static_cast<uint8_t>(v >> 24);
  };
  // data_bytes = whole-file size minus 8 (the magic + this field).
  put_le32(0x04, static_cast<uint32_t>(kRfwvHeaderSize + pcm.size()) - 8);
  put_le32(0x08, sample_rate);
  smp[0x0C] = static_cast<uint8_t>(channels);
  smp[0x0D] = static_cast<uint8_t>(channels >> 8);
  put_le32(0x10, bits_per_sample);
  // The device validates this: MD5 of the 16 format-descriptor bytes.
  const std::array<uint8_t, 16> digest = Md5(smp.data() + 0x04, 0x10);
  std::copy(digest.begin(), digest.end(), smp.begin() + 0x20);
  smp.insert(smp.end(), pcm.begin(), pcm.end());
  return smp;
}

std::string RemoteWavePath(int index) {
  char buf[80];
  std::snprintf(buf,
                sizeof(buf),
                "/SPDSXREMOTE//Roland/SPD-SXPRO/WAVE/DATA/D%03d/W%05d.SMP",
                index / 100,
                index);
  return buf;
}

RfwvHeader ParseRfwvHeader(const Bytes& smp) {
  RfwvHeader h;
  // RFWV magic, then u32 data length, u32 sample rate, u16 channels, and
  // bits/sample at 0x10 — so the fields run to 0x13 and anything shorter than
  // 20 bytes is not a header. (Reading them needs the whole 20: the buffer
  // comes off the device, so a truncated one must be refused, not parsed.)
  if (smp.size() < 20 || smp[0] != 'R' || smp[1] != 'F' || smp[2] != 'W'
      || smp[3] != 'V') {
    return h;
  }
  h.valid = true;
  h.data_bytes = ReadLe32(smp, 4);
  h.sample_rate = ReadLe32(smp, 8);
  h.channels =
      static_cast<uint16_t>(smp[12] | static_cast<uint16_t>(smp[13]) << 8);
  h.bits_per_sample = static_cast<uint16_t>(ReadLe32(smp, 16));
  return h;
}

}  // namespace spdsx::device
