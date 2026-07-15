#include "device/sample_image.h"

#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace spdsx::device {
namespace {

Bytes FromHex(const std::string& hex) {
  Bytes out;
  std::istringstream in(hex);
  int byte = 0;
  while (in >> std::hex >> byte) {
    out.push_back(static_cast<uint8_t>(byte));
  }
  return out;
}

// A 24-byte RFWV header prefix: 48 kHz, mono, 16-bit, 0x4d14 data bytes.
const Bytes kRfwvPrefix = FromHex(
    "52 46 57 56 14 4d 00 00 80 bb 00 00 01 00 00 00 "
    "10 00 00 00 00 00 00 00");

// The directory the parser anchors on: junk, then records a stride apart,
// with record 1's filename holding the "PRELOAD 00001" anchor.
constexpr size_t kJunk = 64;

void PutRecord(Bytes& image,
               int index,
               const std::string& wavename,
               const std::string& filename,
               uint32_t frames,
               uint32_t category) {
  const size_t rec = kJunk + static_cast<size_t>(index) * kSampleRecordStride;
  std::memcpy(&image[rec], wavename.data(), wavename.size());
  std::memcpy(&image[rec + 0x10], filename.data(), filename.size());
  for (int b = 0; b < 4; ++b) {
    image[rec + 0x94 + static_cast<size_t>(b)] =
        static_cast<uint8_t>(frames >> (8 * b));
    image[rec + 0xa0 + static_cast<size_t>(b)] =
        static_cast<uint8_t>(category >> (8 * b));
  }
}

// Space-filled, the way the device pads its fixed-width fields.
Bytes Directory(size_t records = 6) {
  return Bytes(kJunk + kSampleRecordStride * records, 0x20);
}

// ---- Layout and categories ----

TEST(SampleImageLayout, IsTheMeasuredDirectoryLayout) {
  EXPECT_EQ(kSampleSlots, 20001);  // 20,000 waves plus the index-0 sentinel
  EXPECT_EQ(kSampleRecordStride, 164u);  // 0xa4
  EXPECT_EQ(kRfwvHeaderSize, 512u);
  EXPECT_EQ(kSampleCategoryCount, 22);
}

TEST(SampleCategoryName, NamesEveryCategoryTheDeviceHas) {
  EXPECT_EQ(SampleCategoryName(0), "OFF");
  EXPECT_EQ(SampleCategoryName(1), "Kick");
  EXPECT_EQ(SampleCategoryName(15), "Percussion");
  EXPECT_EQ(SampleCategoryName(kSampleCategoryCount - 1), "808");
}

// A record could hold anything; an unknown category is not worth a throw.
TEST(SampleCategoryName, ShrugsAtACategoryThatIsNotOne) {
  EXPECT_EQ(SampleCategoryName(-1), "?");
  EXPECT_EQ(SampleCategoryName(kSampleCategoryCount), "?");
  EXPECT_EQ(SampleCategoryName(9999), "?");
}

// ---- ParseSampleDir ----

TEST(ParseSampleDir, ReadsTheNamedRecordsInIndexOrder) {
  Bytes image = Directory();
  PutRecord(image, 1, "Solid K", "PRELOAD 00001", 56773, 1);
  PutRecord(image, 2, "Warm K", "PRELOAD 00002", 76712, 1);
  PutRecord(image, 4, "Bongo_Hi_CR78", "Bongo_Hi_CR78.wav", 11111, 15);

  const std::vector<SampleRecord> dir = ParseSampleDir(image);

  ASSERT_EQ(dir.size(), 3u);
  EXPECT_EQ(dir[0].index, 1);
  EXPECT_EQ(dir[0].wavename, "Solid K");
  EXPECT_EQ(dir[0].filename, "PRELOAD 00001");
  EXPECT_EQ(dir[0].frames, 56773u);
  EXPECT_EQ(dir[0].category, 1);

  EXPECT_EQ(dir[1].index, 2);
  EXPECT_EQ(dir[2].index, 4);
  EXPECT_EQ(dir[2].filename, "Bongo_Hi_CR78.wav");
  EXPECT_EQ(dir[2].category, 15);
}

// The pool is sparse: an unnamed slot was deleted or never used, and is
// skipped rather than listed as a blank wave. Index 3 is absent above.
TEST(ParseSampleDir, SkipsTheSlotsWithNoWaveInThem) {
  Bytes image = Directory();
  PutRecord(image, 1, "Solid K", "PRELOAD 00001", 1, 1);
  PutRecord(image, 4, "Bongo", "Bongo.wav", 1, 1);

  const std::vector<SampleRecord> dir = ParseSampleDir(image);

  ASSERT_EQ(dir.size(), 2u);
  EXPECT_EQ(dir[0].index, 1);
  EXPECT_EQ(dir[1].index, 4);
}

// The directory is located by finding record 1's filename, so any clean image
// containing bank 0x20 works regardless of where it starts.
TEST(ParseSampleDir, FindsNothingWithoutTheAnchor) {
  EXPECT_TRUE(ParseSampleDir({}).empty());
  EXPECT_TRUE(ParseSampleDir(Directory()).empty());  // all spaces, no anchor
  EXPECT_TRUE(ParseSampleDir(FromHex("de ad be ef")).empty());
}

// The anchor is record 1's filename field, so record 0 must sit a full stride
// before it. An anchor too near the start is not a directory.
TEST(ParseSampleDir, RejectsAnAnchorWithNoRoomForRecordZero) {
  Bytes image(kSampleRecordStride * 4, 0x20);
  const std::string anchor = "PRELOAD 00001";
  std::memcpy(&image[0x10], anchor.data(), anchor.size());  // record 1 at 0

  EXPECT_TRUE(ParseSampleDir(image).empty());
}

// Only a whole record is a record: one the image cuts in half is left out
// rather than read past the end of the buffer.
TEST(ParseSampleDir, StopsWhenTheImageRunsOutMidRecord) {
  Bytes image = Directory(4);
  PutRecord(image, 1, "Solid K", "PRELOAD 00001", 1, 1);
  PutRecord(image, 2, "Warm K", "PRELOAD 00002", 1, 1);
  PutRecord(image, 3, "Half A Wave", "half.wav", 1, 1);
  // Cut record 3 partway through.
  image.resize(kJunk + kSampleRecordStride * 3 + 80);

  const std::vector<SampleRecord> dir = ParseSampleDir(image);

  ASSERT_EQ(dir.size(), 2u);
  EXPECT_EQ(dir[0].index, 1);
  EXPECT_EQ(dir[1].index, 2);
}

// ---- SampleRecord::is_preload ----

// Factory preloads can never be exported off the device, so they will never
// have local audio; user imports eventually will.
TEST(SampleRecord, TellsAPreloadFromAUserImport) {
  SampleRecord preload;
  preload.filename = "PRELOAD 00001";
  EXPECT_TRUE(preload.is_preload());

  SampleRecord imported;
  imported.filename = "Bongo_Hi_CR78.wav";
  EXPECT_FALSE(imported.is_preload());

  SampleRecord empty;
  EXPECT_FALSE(empty.is_preload());

  // The prefix has to be at the front, not merely present.
  SampleRecord tricky;
  tricky.filename = "my PRELOAD 00001.wav";
  EXPECT_FALSE(tricky.is_preload());
}

// ---- RemoteWavePath ----

// Sample N lives at .../D<N/100>/W<N>.SMP, both zero-padded.
TEST(RemoteWavePath, IsTheDeviceSidePathForAPoolIndex) {
  EXPECT_EQ(RemoteWavePath(1554),
            "/SPDSXREMOTE//Roland/SPD-SXPRO/WAVE/DATA/D015/W01554.SMP");
  EXPECT_EQ(RemoteWavePath(1),
            "/SPDSXREMOTE//Roland/SPD-SXPRO/WAVE/DATA/D000/W00001.SMP");
  EXPECT_EQ(RemoteWavePath(20000),
            "/SPDSXREMOTE//Roland/SPD-SXPRO/WAVE/DATA/D200/W20000.SMP");
}

// ---- ParseRfwvHeader ----

TEST(ParseRfwvHeader, ReadsTheFormatOffTheFrontOfASmp) {
  const RfwvHeader h = ParseRfwvHeader(kRfwvPrefix);

  EXPECT_TRUE(h.valid);
  EXPECT_EQ(h.data_bytes, 0x4d14u);
  EXPECT_EQ(h.sample_rate, 48000u);
  EXPECT_EQ(h.channels, 1);
  EXPECT_EQ(h.bits_per_sample, 16);
}

TEST(ParseRfwvHeader, RejectsWhatIsNotAnRfwv) {
  EXPECT_FALSE(ParseRfwvHeader({}).valid);
  EXPECT_FALSE(ParseRfwvHeader(FromHex("de ad be ef")).valid);

  Bytes wrong_magic = kRfwvPrefix;
  wrong_magic[3] = 'X';  // RFWX
  EXPECT_FALSE(ParseRfwvHeader(wrong_magic).valid);
}

// The header's last field ends at byte 0x13, so anything shorter is not a
// header to read -- reporting it invalid is what keeps the read in bounds.
TEST(ParseRfwvHeader, RejectsATruncatedHeader) {
  for (size_t size = 0; size < 20; ++size) {
    const Bytes truncated(kRfwvPrefix.begin(),
                          kRfwvPrefix.begin() + static_cast<long>(size));
    EXPECT_FALSE(ParseRfwvHeader(truncated).valid) << "size " << size;
  }
  EXPECT_TRUE(
      ParseRfwvHeader(Bytes(kRfwvPrefix.begin(), kRfwvPrefix.begin() + 20))
          .valid);
}

// ---- RfwvToWav ----

TEST(RfwvToWav, WrapsThePcmInAStandardWavHeader) {
  Bytes smp = kRfwvPrefix;
  smp.resize(kRfwvHeaderSize, 0);
  const Bytes pcm = {1, 0, 2, 0, 3, 0, 4, 0};
  smp.insert(smp.end(), pcm.begin(), pcm.end());

  const Bytes wav = RfwvToWav(smp);

  ASSERT_EQ(wav.size(), 44u + pcm.size());
  EXPECT_EQ(std::string(wav.begin(), wav.begin() + 4), "RIFF");
  EXPECT_EQ(std::string(wav.begin() + 8, wav.begin() + 12), "WAVE");
  EXPECT_EQ(std::string(wav.begin() + 12, wav.begin() + 16), "fmt ");
  EXPECT_EQ(std::string(wav.begin() + 36, wav.begin() + 40), "data");
  EXPECT_EQ(wav[22], 1);  // channels
  // The PCM is carried across untouched.
  EXPECT_EQ(Bytes(wav.end() - static_cast<long>(pcm.size()), wav.end()), pcm);
}

// The PCM starts at 0x200, not at 0x20: reading from 0x20 would play the
// header's overview block as a burst of noise.
TEST(RfwvToWav, TakesThePcmFromPastTheWholeHeader) {
  Bytes smp = kRfwvPrefix;
  smp.resize(kRfwvHeaderSize, 0xee);  // noise, if it were ever read as audio
  const Bytes pcm = {0x11, 0x22};
  smp.insert(smp.end(), pcm.begin(), pcm.end());

  const Bytes wav = RfwvToWav(smp);

  ASSERT_EQ(wav.size(), 44u + pcm.size());
  EXPECT_EQ(Bytes(wav.begin() + 44, wav.end()), pcm);
}

TEST(RfwvToWav, RefusesWhatItCannotConvert) {
  EXPECT_TRUE(RfwvToWav({}).empty());
  EXPECT_TRUE(RfwvToWav(FromHex("de ad be ef")).empty());

  // A valid header with no PCM after it is not a wave.
  Bytes header_only = kRfwvPrefix;
  header_only.resize(kRfwvHeaderSize, 0);
  EXPECT_TRUE(RfwvToWav(header_only).empty());

  // Nor is one claiming no channels or no bit depth.
  Bytes no_channels = kRfwvPrefix;
  no_channels[12] = 0;
  no_channels.resize(kRfwvHeaderSize + 8, 0);
  EXPECT_TRUE(RfwvToWav(no_channels).empty());

  Bytes no_bits = kRfwvPrefix;
  no_bits[16] = 0;
  no_bits.resize(kRfwvHeaderSize + 8, 0);
  EXPECT_TRUE(RfwvToWav(no_bits).empty());
}

// ---- PcmToRfwv ----

// Byte-exact against the device's own header for the same wave (synth1586,
// live-verified to play). The MD5 at 0x20 is the part that matters: the
// device validates it, and a wrong or zero checksum registers and reads back
// fine but plays SILENT.
TEST(PcmToRfwv, MatchesTheDevicesOwnHeaderIncludingTheChecksum) {
  const Bytes smp = PcmToRfwv(Bytes(8192, 0), 48000, 1, 16);

  const Bytes expected = FromHex(
      "52 46 57 56 f8 21 00 00 80 bb 00 00 01 00 00 00 "
      "10 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 "
      "70 c7 26 87 b3 5a 08 cf 7f a5 e8 ce d1 e3 51 33");

  ASSERT_EQ(smp.size(), kRfwvHeaderSize + 8192);
  EXPECT_EQ(
      Bytes(smp.begin(), smp.begin() + static_cast<long>(expected.size())),
      expected);
}

// The checksum covers the format descriptor, so a wave that differs in rate,
// channels, or depth gets a different one.
TEST(PcmToRfwv, ChecksumsTheFormatDescriptor) {
  const auto digest = [](const Bytes& smp) {
    return Bytes(smp.begin() + 0x20, smp.begin() + 0x30);
  };
  const Bytes mono = PcmToRfwv(Bytes(64, 0), 48000, 1, 16);
  const Bytes stereo = PcmToRfwv(Bytes(64, 0), 48000, 2, 16);
  const Bytes resampled = PcmToRfwv(Bytes(64, 0), 44100, 1, 16);

  EXPECT_NE(digest(mono), digest(stereo));
  EXPECT_NE(digest(mono), digest(resampled));
  // Same format, same checksum: it does not depend on the audio.
  EXPECT_EQ(digest(mono), digest(PcmToRfwv(Bytes(64, 0xff), 48000, 1, 16)));
}

TEST(PcmToRfwv, RoundTripsBackThroughParseRfwvHeader) {
  const Bytes smp = PcmToRfwv(Bytes(64, 0x7f), 48000, 2, 16);
  const RfwvHeader h = ParseRfwvHeader(smp);

  EXPECT_TRUE(h.valid);
  EXPECT_EQ(h.sample_rate, 48000u);
  EXPECT_EQ(h.channels, 2);
  EXPECT_EQ(h.bits_per_sample, 16);
  // data_bytes is the whole file less the magic and this field.
  EXPECT_EQ(h.data_bytes, kRfwvHeaderSize + 64 - 8);
}

// What goes up comes back down: the PCM survives a build-then-convert.
TEST(PcmToRfwv, KeepsThePcmThroughAConversionBackToWav) {
  Bytes pcm;
  for (int i = 0; i < 64; ++i) {
    pcm.push_back(static_cast<uint8_t>(i));
  }
  const Bytes wav = RfwvToWav(PcmToRfwv(pcm, 48000, 1, 16));

  ASSERT_EQ(wav.size(), 44u + pcm.size());
  EXPECT_EQ(Bytes(wav.begin() + 44, wav.end()), pcm);
}

}  // namespace
}  // namespace spdsx::device
