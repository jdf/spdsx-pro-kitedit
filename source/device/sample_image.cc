#include "device/sample_image.h"

#include <algorithm>
#include <array>
#include <cstdio>

namespace spdsx::device {

namespace {

constexpr size_t kWavenameOffset = 0x00;
constexpr size_t kWavenameLen = 16;
constexpr size_t kFilenameOffset = 0x10;
constexpr size_t kFilenameLen = 84;
constexpr size_t kFramesOffset = 0x94;
constexpr size_t kCategoryOffset = 0xa0;

// The record whose filename anchors the directory: sample 1 is always
// the first preload.
constexpr std::string_view kAnchor = "PRELOAD 00001";

constexpr std::array<std::string_view, kSampleCategoryCount> kCategories =
    {"OFF", "Kick", "Kick Proc/Elec", "Snare", "Snare Proc/Elec",
        "Cross Stick", "Clap", "Tom", "Tom Proc/Elec", "HiHat",
        "HiHat Proc/Elec", "Crash", "Ride", "Splash/China",
        "Cymbal Proc/Elec", "Percussion", "Percussion Elec", "FX",
        "Synth Hit", "Sub Element", "Loop", "808"};

std::string Trimmed(const Bytes& image, size_t offset, size_t len)
{
  std::string s(image.begin() + static_cast<long>(offset),
      image.begin() + static_cast<long>(offset + len));
  while (!s.empty() && (s.back() == ' ' || s.back() == '\0')) {
    s.pop_back();
  }
  return s;
}

uint32_t ReadLe32(const Bytes& image, size_t offset)
{
  return static_cast<uint32_t>(image[offset])
      | static_cast<uint32_t>(image[offset + 1]) << 8
      | static_cast<uint32_t>(image[offset + 2]) << 16
      | static_cast<uint32_t>(image[offset + 3]) << 24;
}

}  // namespace

std::string_view SampleCategoryName(int category)
{
  if (category < 0 || category >= kSampleCategoryCount) {
    return "?";
  }
  return kCategories.at(static_cast<size_t>(category));
}

std::vector<SampleRecord> ParseSampleDir(const Bytes& clean_image)
{
  std::vector<SampleRecord> records;
  const auto anchor = std::search(clean_image.begin(), clean_image.end(),
      kAnchor.begin(), kAnchor.end());
  if (anchor == clean_image.end()) {
    return records;
  }
  // The anchor is record 1's filename field.
  const size_t rec1 =
      static_cast<size_t>(anchor - clean_image.begin()) - kFilenameOffset;
  if (rec1 < kSampleRecordStride) {
    return records;
  }
  const size_t base = rec1 - kSampleRecordStride;
  for (int i = 1; i < kSampleSlots; ++i) {
    const size_t off =
        base + static_cast<size_t>(i) * kSampleRecordStride;
    if (off + kSampleRecordStride > clean_image.size()) {
      break;
    }
    SampleRecord rec;
    rec.wavename = Trimmed(clean_image, off + kWavenameOffset,
        kWavenameLen);
    if (rec.wavename.empty()) {
      continue;  // unoccupied slot (deleted or never used)
    }
    rec.index = i;
    rec.filename = Trimmed(clean_image, off + kFilenameOffset,
        kFilenameLen);
    rec.frames = ReadLe32(clean_image, off + kFramesOffset);
    rec.category = static_cast<int>(
        ReadLe32(clean_image, off + kCategoryOffset));
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
  PushLe32(wav, 16);                 // PCM fmt chunk size
  PushLe16(wav, 1);                  // PCM
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

std::string RemoteWavePath(int index)
{
  char buf[80];
  std::snprintf(buf, sizeof(buf),
      "/SPDSXREMOTE//Roland/SPD-SXPRO/WAVE/DATA/D%03d/W%05d.SMP",
      index / 100, index);
  return buf;
}

RfwvHeader ParseRfwvHeader(const Bytes& smp)
{
  RfwvHeader h;
  // RFWV magic, then u32 data length, u32 sample rate, u16 channels.
  if (smp.size() < 14 || smp[0] != 'R' || smp[1] != 'F' || smp[2] != 'W'
      || smp[3] != 'V')
  {
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
