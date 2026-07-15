#include "device/kit_image.h"

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

// One reply block: the 14-byte frame header, `data_len` bytes of payload, and
// the terminating f7.
Bytes Block(uint8_t fill, size_t data_len) {
  Bytes blk = FromHex("f0 41 6c 02 00 00 00 00 10 00 00 04 00 00");
  blk.insert(blk.end(), data_len, fill);
  blk.push_back(0xf7);
  return blk;
}

// A clean image big enough to hold records 0..128 (device kits 1..129).
constexpr int kRecords = 129;

Bytes CleanImage() {
  return Bytes(kKitArrayBase + kRecords * kKitRecordStride, 0x00);
}

size_t RecordAt(int record) {
  return kKitArrayBase + static_cast<size_t>(record) * kKitRecordStride;
}

size_t PadAt(int record, int pad) {
  return RecordAt(record) + kPadTableBase
      + static_cast<size_t>(pad) * kPadBlockStride;
}

void PlantName(Bytes& image, int record, const std::string& name) {
  const size_t rec = RecordAt(record) + kKitNameOffset;
  for (size_t i = 0; i < kKitNameLen; ++i) {
    image[rec + i] = i < name.size() ? static_cast<uint8_t>(name[i]) : 0x20;
  }
}

// ---- The record layout is reverse-engineered ground truth ----

// These came from diffing real device images and were verified against the
// hardware (the ZZZ kit landing at 129, edited pad values reading back). They
// are measurements, not choices: pinned so a change has to be deliberate.
TEST(KitImageLayout, IsTheMeasuredRecordLayout) {
  EXPECT_EQ(kBankKitCount, 200);
  EXPECT_EQ(kKitArrayBase, 0x262cu);
  EXPECT_EQ(kKitRecordStride, 3528u);
  EXPECT_EQ(kKitNameOffset, 0u);
  EXPECT_EQ(kKitNameLen, 16u);
  EXPECT_EQ(kPadsPerKit, 9);
  EXPECT_EQ(kPadTableBase, 0x284u);
  EXPECT_EQ(kPadBlockStride, 28u);
  EXPECT_EQ(kLayerTableBase, 0x49au);
  EXPECT_EQ(kLayerBlockStride, 60u);
}

// Within a pad block the byte offset IS the DT1 param index, which is why a
// stored value and a written one use the same number (see PadParamAddr).
TEST(KitImageLayout, PadParamOffsetsAreTheDt1ParamIndices) {
  EXPECT_EQ(kPadLayerMode, 0x00u);
  EXPECT_EQ(kPadFadePoint, 0x01u);
  EXPECT_EQ(kPadFadeEnd, 0x02u);
  EXPECT_EQ(kPadDynamics, 0x03u);
  EXPECT_EQ(kPadDynCurve, 0x04u);
  EXPECT_EQ(kPadFixedVel, 0x05u);
  EXPECT_EQ(kPadTrigReserve, 0x13u);
}

// Mapped live 2026-07-13 by capturing the app's writes on a hi-hat pad. This
// supersedes an earlier +0x06/07/08 guess that was off by one and mislabelled,
// so the exact offsets are worth holding.
TEST(KitImageLayout, HiHatClosedPedalOffsetsAreTheMappedOnes) {
  EXPECT_EQ(kPadHiHatVolume, 0x07u);
  EXPECT_EQ(kPadHiHatFadeIn, 0x08u);
  EXPECT_EQ(kPadHiHatDecay, 0x09u);
}

// ---- CleanBulkImage ----

TEST(CleanBulkImage, StripsTheFramingAndConcatenatesTheData) {
  Bytes raw = Block(0xaa, 100);
  const Bytes second = Block(0xbb, 50);
  raw.insert(raw.end(), second.begin(), second.end());

  const Bytes clean = CleanBulkImage(raw);

  ASSERT_EQ(clean.size(), 150u);
  EXPECT_EQ(clean[0], 0xaa);
  EXPECT_EQ(clean[99], 0xaa);
  EXPECT_EQ(clean[100], 0xbb);
  EXPECT_EQ(clean[149], 0xbb);
}

TEST(CleanBulkImage, HasNothingToStripFromNothing) {
  EXPECT_TRUE(CleanBulkImage({}).empty());
  EXPECT_TRUE(CleanBulkImage(FromHex("de ad be ef")).empty());
}

// A block with no room for data past its header contributes nothing rather
// than reading off the end of it.
TEST(CleanBulkImage, SkipsABlockWithNoPayload) {
  const Bytes empty_block =
      FromHex("f0 41 6c 02 00 00 00 00 10 00 00 04 00 f7");
  EXPECT_TRUE(CleanBulkImage(empty_block).empty());

  Bytes mixed = empty_block;
  const Bytes real = Block(0xcc, 8);
  mixed.insert(mixed.end(), real.begin(), real.end());
  EXPECT_EQ(CleanBulkImage(mixed), Bytes(8, 0xcc));
}

// ---- ParseKits ----

// Record i is device kit i+1: the ZZZ experiment kit sat at 129, which is
// record 128, and that is how the layout was confirmed against the hardware.
TEST(ParseKits, ReadsKitNamesByRecordIndex) {
  Bytes image = CleanImage();
  PlantName(image, 0, "Dance");
  PlantName(image, 128, "ZZZZZZZZZZZZZZZZ");

  const std::vector<KitRecord> kits = ParseKits(image);

  ASSERT_EQ(kits.size(), static_cast<size_t>(kRecords));
  EXPECT_EQ(kits[0].name, "Dance");
  EXPECT_EQ(kits[128].name, "ZZZZZZZZZZZZZZZZ");
}

// The device space-pads the fixed-width name field.
TEST(ParseKits, TrimsThePaddingOffNames) {
  Bytes image = CleanImage();
  PlantName(image, 0, "Dance");  // space-padded to 16
  PlantName(image, 1, "");  // all spaces
  PlantName(image, 2, "ZZZZZZZZZZZZZZZZ");  // fills the field exactly

  const std::vector<KitRecord> kits = ParseKits(image);

  EXPECT_EQ(kits[0].name, "Dance");
  EXPECT_EQ(kits[1].name, "");
  EXPECT_EQ(kits[2].name, "ZZZZZZZZZZZZZZZZ");
}

// A record left as zeroes reads as an empty name, not sixteen NULs.
TEST(ParseKits, TrimsNulPaddingToo) {
  const Bytes image = CleanImage();
  const std::vector<KitRecord> kits = ParseKits(image);
  ASSERT_FALSE(kits.empty());
  EXPECT_EQ(kits[0].name, "");
}

TEST(ParseKits, ReadsThePadParamsFromTheMappedOffsets) {
  Bytes image = CleanImage();
  const size_t pad1 = PadAt(128, 0);
  image[pad1 + kPadLayerMode] = 3;  // XFADE
  image[pad1 + kPadFadePoint] = 100;
  image[pad1 + kPadFadeEnd] = 120;
  image[pad1 + kPadDynamics] = 1;
  image[pad1 + kPadDynCurve] = 3;  // LOUD3
  image[pad1 + kPadFixedVel] = 50;
  image[pad1 + kPadHiHatVolume] = 80;
  image[pad1 + kPadHiHatFadeIn] = 5;
  image[pad1 + kPadHiHatDecay] = 25;
  image[pad1 + kPadTrigReserve] = 1;
  image[PadAt(128, 2) + kPadLayerMode] = 4;  // pad 3: SWITCH

  const std::vector<KitRecord> kits = ParseKits(image);

  ASSERT_EQ(kits.size(), static_cast<size_t>(kRecords));
  const PadDeviceParams& pad = kits[128].pads[0];
  EXPECT_EQ(pad.layer_mode, 3);
  EXPECT_EQ(pad.fade_point, 100);
  EXPECT_EQ(pad.fade_end, 120);
  EXPECT_EQ(pad.dynamics, 1);
  EXPECT_EQ(pad.dynamics_curve, 3);
  EXPECT_EQ(pad.fixed_velocity, 50);
  EXPECT_EQ(pad.hi_hat_volume, 80);
  EXPECT_EQ(pad.hi_hat_fade_in, 5);
  EXPECT_EQ(pad.hi_hat_decay, 25);
  EXPECT_EQ(pad.trigger_reserve, 1);

  // The pads are separate blocks a stride apart.
  EXPECT_EQ(kits[128].pads[2].layer_mode, 4);
  EXPECT_EQ(kits[128].pads[1].layer_mode, 0);
}

// The layer table is 18 blocks: top = pad*2, bottom = pad*2 + 1, each
// starting with the wave as a u16 LE.
TEST(ParseKits, ReadsEachLayersWaveAsALittleEndianPoolIndex) {
  Bytes image = CleanImage();
  const size_t layer = RecordAt(128) + kLayerTableBase;
  image[layer] = 127;  // pad 1 top
  image[layer + kLayerBlockStride] = 203;  // pad 1 bottom
  // A pool index past 255 has to come back out of both bytes.
  image[layer + 2 * kLayerBlockStride] = 0x2e;  // pad 2 top = 1582
  image[layer + 2 * kLayerBlockStride + 1] = 0x06;

  const std::vector<KitRecord> kits = ParseKits(image);

  ASSERT_EQ(kits.size(), static_cast<size_t>(kRecords));
  EXPECT_EQ(kits[128].pads[0].wave_top, 127);
  EXPECT_EQ(kits[128].pads[0].wave_bottom, 203);
  EXPECT_EQ(kits[128].pads[1].wave_top, 1582);
  EXPECT_EQ(kits[128].pads[1].wave_bottom, 0);  // 0 = no sample
}

// Cut between a record's name and its pad table: take the name and leave the
// pads at their defaults, rather than reading past the end of the image.
TEST(ParseKits, StopsMidRecordWhenTheImageEndsAfterTheName) {
  Bytes image(kKitArrayBase + kKitNameLen + 4, 0x00);
  PlantName(image, 0, "Dance");

  const std::vector<KitRecord> kits = ParseKits(image);

  ASSERT_EQ(kits.size(), 1u);
  EXPECT_EQ(kits[0].name, "Dance");
  EXPECT_EQ(kits[0].pads[0].layer_mode, 0);
  EXPECT_EQ(kits[0].pads[0].fade_point, 0);
  EXPECT_EQ(kits[0].pads[0].wave_top, 0);
}

TEST(ParseKits, StopsAtTheDevicesKitCount) {
  const Bytes image(kKitArrayBase + 250 * kKitRecordStride, 0x00);
  EXPECT_EQ(ParseKits(image).size(), static_cast<size_t>(kBankKitCount));
}

// A short image yields the records it actually holds instead of reading past
// the end of it.
TEST(ParseKits, StopsWhenTheImageRunsOut) {
  EXPECT_TRUE(ParseKits({}).empty());
  EXPECT_TRUE(ParseKits(Bytes(kKitArrayBase, 0x00)).empty());

  const Bytes three(kKitArrayBase + 3 * kKitRecordStride, 0x00);
  EXPECT_EQ(ParseKits(three).size(), 3u);
}

}  // namespace
}  // namespace spdsx::device
