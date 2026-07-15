#include "device/protocol.h"

#include <set>
#include <sstream>
#include <stdexcept>
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

// ---- Checksum ----

// The defining property of a Roland checksum: appended to the body, the whole
// thing sums to a multiple of 128. That is what the device validates, so it
// is worth holding directly rather than restating the arithmetic.
TEST(Checksum, MakesTheBodySumToAMultipleOf128) {
  const Bytes bodies[] = {{},
                          {0x00},
                          {0x02},
                          {0x7f},
                          {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00},
                          {0x7f, 0x7f, 0x7f, 0x7f}};
  for (const Bytes& body : bodies) {
    int sum = 0;
    for (const uint8_t b : body) {
      sum += b;
    }
    sum += Checksum(body);
    EXPECT_EQ(sum % 128, 0);
  }
}

TEST(Checksum, IsSevenBit) {
  for (int b = 0; b < 256; ++b) {
    EXPECT_LE(Checksum({static_cast<uint8_t>(b)}), 0x7F) << "body byte " << b;
  }
}

// ---- Dt1 ----

TEST(Dt1, WrapsTheBodyInTheRolandFrame) {
  const Bytes msg = Dt1({0x01, 0x02}, {0x03});

  // f0 41 <dev> + 5 model + 12 + 2 addr + 1 data + checksum + f7.
  ASSERT_EQ(msg.size(), 14u);
  EXPECT_EQ(msg[0], 0xF0);  // sysex start
  EXPECT_EQ(msg[1], 0x41);  // Roland
  EXPECT_EQ(msg[2], kDeviceId);
  EXPECT_EQ(Bytes(msg.begin() + 3, msg.begin() + 8),
            Bytes(kModelId, kModelId + sizeof(kModelId)));
  EXPECT_EQ(msg[8], kDt1);
  EXPECT_EQ(Bytes(msg.begin() + 9, msg.begin() + 12),
            Bytes({0x01, 0x02, 0x03}));
  EXPECT_EQ(msg[msg.size() - 2], Checksum({0x01, 0x02, 0x03}));
  EXPECT_EQ(msg.back(), 0xF7);  // sysex end
}

// Byte-exact against the captures the port was verified from: the whole
// kit-select message for kits either side of the 7-bit boundary at 128.
TEST(Dt1, MatchesTheCapturedKitSelectMessages) {
  const struct {
    int kit;
    const char* hex;
  } cases[] = {
      {3, "f0 41 10 00 00 00 00 16 12 00 00 00 00 00 00 00 02 7e f7"},
      {128, "f0 41 10 00 00 00 00 16 12 00 00 00 00 00 00 07 0f 6a f7"},
      {129, "f0 41 10 00 00 00 00 16 12 00 00 00 00 00 00 08 00 78 f7"},
      {130, "f0 41 10 00 00 00 00 16 12 00 00 00 00 00 00 08 01 77 f7"},
      {131, "f0 41 10 00 00 00 00 16 12 00 00 00 00 00 00 08 02 76 f7"},
      {200, "f0 41 10 00 00 00 00 16 12 00 00 00 00 00 00 0c 07 6d f7"},
  };

  for (const auto& c : cases) {
    EXPECT_EQ(Dt1(kKitSelectAddr, EncodeKit(c.kit)), FromHex(c.hex))
        << "kit " << c.kit;
  }
}

// ---- EncodeKit ----

TEST(EncodeKit, SplitsTheZeroBasedKitAcrossTwoBytes) {
  EXPECT_EQ(EncodeKit(1), Bytes({0x00, 0x00}));
  EXPECT_EQ(EncodeKit(3), Bytes({0x00, 0x02}));
  EXPECT_EQ(EncodeKit(128), Bytes({0x07, 0x0f}));
  EXPECT_EQ(EncodeKit(129), Bytes({0x08, 0x00}));
  EXPECT_EQ(EncodeKit(200), Bytes({0x0c, 0x07}));
}

TEST(EncodeKit, RefusesAKitTheDeviceDoesNotHave) {
  EXPECT_THROW((void)EncodeKit(0), std::out_of_range);
  EXPECT_THROW((void)EncodeKit(201), std::out_of_range);
  EXPECT_THROW((void)EncodeKit(-1), std::out_of_range);
}

// ---- SelectValue ----

TEST(SelectValue, NumbersPadsFromZeroAndTriggersAboveThem) {
  EXPECT_EQ(SelectValue(ObjectKind::kPad, 1), 0);
  EXPECT_EQ(SelectValue(ObjectKind::kPad, 9), 8);
  EXPECT_EQ(SelectValue(ObjectKind::kTrig, 1), 9);
  EXPECT_EQ(SelectValue(ObjectKind::kTrig, 8), 16);
}

// The two kinds share one value space, so they must not collide.
TEST(SelectValue, GivesEveryObjectItsOwnValue) {
  std::vector<uint8_t> seen;
  for (int pad = 1; pad <= 9; ++pad) {
    seen.push_back(SelectValue(ObjectKind::kPad, pad));
  }
  for (int trig = 1; trig <= 8; ++trig) {
    seen.push_back(SelectValue(ObjectKind::kTrig, trig));
  }
  const std::set<uint8_t> unique(seen.begin(), seen.end());
  EXPECT_EQ(unique.size(), seen.size());
}

TEST(SelectValue, RefusesAnObjectTheDeviceDoesNotHave) {
  EXPECT_THROW((void)SelectValue(ObjectKind::kPad, 0), std::out_of_range);
  EXPECT_THROW((void)SelectValue(ObjectKind::kPad, 10), std::out_of_range);
  EXPECT_THROW((void)SelectValue(ObjectKind::kTrig, 0), std::out_of_range);
  EXPECT_THROW((void)SelectValue(ObjectKind::kTrig, 9), std::out_of_range);
}

// ---- PadLinkPrefix: the kit is in the address ----

// This prefix is what makes a write land on the kit you meant. Every capture
// was taken on kit 129, whose prefix is 06 00 -- which is exactly why a
// hardcoded 06 00 looked right for a while and silently sent every write to
// kit 129 regardless of the target.
TEST(PadLinkPrefix, EncodesTheKitAndKit129IsTheFamiliar0600) {
  EXPECT_EQ(PadLinkPrefix(129), Bytes({0x06, 0x00}));

  EXPECT_EQ(PadLinkPrefix(1), Bytes({0x04, 0x00}));
  EXPECT_EQ(PadLinkPrefix(5), Bytes({0x04, 0x08}));
  EXPECT_EQ(PadLinkPrefix(10), Bytes({0x04, 0x12}));
  EXPECT_EQ(PadLinkPrefix(20), Bytes({0x04, 0x26}));
  EXPECT_EQ(PadLinkPrefix(200), Bytes({0x07, 0x0e}));
}

// Two bytes of a SysEx address: neither may set the high bit.
TEST(PadLinkPrefix, StaysSevenBitAcrossEveryKit) {
  for (int kit = 1; kit <= 200; ++kit) {
    const Bytes prefix = PadLinkPrefix(kit);
    ASSERT_EQ(prefix.size(), 2u);
    EXPECT_LE(prefix[0], 0x7F) << "kit " << kit;
    EXPECT_LE(prefix[1], 0x7F) << "kit " << kit;
  }
}

// Adjacent kits are two apart in the flat address space, and no two kits
// share a prefix -- a collision would silently write to the wrong kit.
TEST(PadLinkPrefix, IsDistinctForEveryKit) {
  std::set<Bytes> seen;
  for (int kit = 1; kit <= 200; ++kit) {
    EXPECT_TRUE(seen.insert(PadLinkPrefix(kit)).second) << "kit " << kit;
  }
}

TEST(PadLinkPrefix, RefusesAKitTheDeviceDoesNotHave) {
  EXPECT_THROW((void)PadLinkPrefix(0), std::out_of_range);
  EXPECT_THROW((void)PadLinkPrefix(201), std::out_of_range);
}

// ---- PadLinkAddr ----

TEST(PadLinkAddr, MatchesTheCapturedPadLinkMessages) {
  const struct {
    int kit;
    ObjectKind kind;
    int index;
    int group;
    const char* hex;
  } cases[] = {
      {5,
       ObjectKind::kTrig,
       7,
       3,
       "f0 41 10 00 00 00 00 16 12 04 08 2f 0c 03 36 f7"},
      {10,
       ObjectKind::kTrig,
       7,
       5,
       "f0 41 10 00 00 00 00 16 12 04 12 2f 0c 05 2a f7"},
      {20,
       ObjectKind::kTrig,
       7,
       5,
       "f0 41 10 00 00 00 00 16 12 04 26 2f 0c 05 16 f7"},
      {200,
       ObjectKind::kTrig,
       7,
       1,
       "f0 41 10 00 00 00 00 16 12 07 0e 2f 0c 01 2f f7"},
      {200,
       ObjectKind::kPad,
       7,
       11,
       "f0 41 10 00 00 00 00 16 12 07 0e 26 0d 0b 2d f7"},
  };

  for (const auto& c : cases) {
    const Bytes built = Dt1(PadLinkAddr(c.kind, c.index, c.kit),
                            {static_cast<uint8_t>(c.group)});
    EXPECT_EQ(built, FromHex(c.hex)) << "kit " << c.kit;
  }
}

TEST(PadLinkAddr, PutsTheKitPrefixAheadOfTheObject) {
  const Bytes pad = PadLinkAddr(ObjectKind::kPad, 1, 129);
  EXPECT_EQ(pad, Bytes({0x06, 0x00, 0x20, 0x0D}));  // 0x1F + 1

  const Bytes trig = PadLinkAddr(ObjectKind::kTrig, 1, 129);
  EXPECT_EQ(trig, Bytes({0x06, 0x00, 0x29, 0x0C}));  // 0x28 + 1
}

TEST(PadLinkAddr, RefusesAnObjectTheDeviceDoesNotHave) {
  EXPECT_THROW((void)PadLinkAddr(ObjectKind::kPad, 10, 1), std::out_of_range);
  EXPECT_THROW((void)PadLinkAddr(ObjectKind::kTrig, 9, 1), std::out_of_range);
}

// ---- NibbleEncode ----

TEST(NibbleEncode, SpreadsAValueOverFourNibbles) {
  EXPECT_EQ(NibbleEncode(0), Bytes({0x00, 0x00, 0x00, 0x00}));
  EXPECT_EQ(NibbleEncode(127), Bytes({0x00, 0x00, 0x07, 0x0f}));
  EXPECT_EQ(NibbleEncode(203), Bytes({0x00, 0x00, 0x0c, 0x0b}));
  EXPECT_EQ(NibbleEncode(0xFFFF), Bytes({0x0f, 0x0f, 0x0f, 0x0f}));
}

// The whole point: SysEx data bytes are 7-bit, so a value over 0x7F has to
// travel as nibbles. Every byte out must be one.
TEST(NibbleEncode, NeverEmitsMoreThanANibble) {
  for (int value = 0; value <= 0xFFFF; value += 7) {
    for (const uint8_t b : NibbleEncode(value)) {
      ASSERT_LE(b, 0x0F) << "value " << value;
    }
  }
}

TEST(NibbleEncode, RefusesWhatWillNotFit) {
  EXPECT_THROW((void)NibbleEncode(-1), std::out_of_range);
  EXPECT_THROW((void)NibbleEncode(0x10000), std::out_of_range);
}

// ---- KitNameAddr ----

TEST(KitNameAddr, IsTheKitPrefixThenTheCharacterIndex) {
  EXPECT_EQ(KitNameAddr(129, 0), Bytes({0x06, 0x00, 0x00, 0x00}));
  EXPECT_EQ(KitNameAddr(129, 15), Bytes({0x06, 0x00, 0x00, 0x0f}));
  EXPECT_EQ(KitNameAddr(200, 0), Bytes({0x07, 0x0e, 0x00, 0x00}));
}

// Byte-exact: writing 'Z' into the first and last character of kit 129's name.
TEST(KitNameAddr, MatchesTheCapturedKitNameWrites) {
  EXPECT_EQ(Dt1(KitNameAddr(129, 0), {0x5a}),
            FromHex("f0 41 10 00 00 00 00 16 12 06 00 00 00 5a 20 f7"));
  EXPECT_EQ(Dt1(KitNameAddr(129, 15), {0x5a}),
            FromHex("f0 41 10 00 00 00 00 16 12 06 00 00 0f 5a 11 f7"));
}

TEST(KitNameAddr, RefusesACharacterOutsideTheNameField) {
  EXPECT_THROW((void)KitNameAddr(1, -1), std::out_of_range);
  EXPECT_THROW((void)KitNameAddr(1, kKitNameLength), std::out_of_range);
}

// ---- Wave slots ----

TEST(PadWaveAddr, MatchesTheCapturedSlotAddresses) {
  EXPECT_EQ(PadWaveAddr(129, 1, PadSlot::kTop), FromHex("06 00 40 01"));
  EXPECT_EQ(PadWaveEnableAddr(129, 1, PadSlot::kTop), FromHex("06 00 40 00"));
  // Pad 9's bottom slot is the far end of the range, verified live.
  EXPECT_EQ(PadWaveAddr(129, 9, PadSlot::kBottom), FromHex("06 00 51 01"));
}

// The whole assignment message for pad 7, both slots: the wave travels
// nibble-encoded because 127 and 203 will not fit in a 7-bit data byte.
TEST(PadWaveAddr, MatchesTheCapturedWaveAssignments) {
  EXPECT_EQ(
      Dt1(PadWaveAddr(129, 7, PadSlot::kTop), NibbleEncode(127)),
      FromHex("f0 41 10 00 00 00 00 16 12 06 00 4c 01 00 00 07 0f 17 f7"));
  EXPECT_EQ(
      Dt1(PadWaveAddr(129, 7, PadSlot::kBottom), NibbleEncode(203)),
      FromHex("f0 41 10 00 00 00 00 16 12 06 00 4d 01 00 00 0c 0b 15 f7"));
}

// Focusing an object is what a pad-link write needs first.
TEST(ObjectSelect, MatchesTheCapturedFocusMessage) {
  EXPECT_EQ(kObjectSelectAddr, FromHex("28 00 00 00"));
  EXPECT_EQ(Dt1(kObjectSelectAddr, {SelectValue(ObjectKind::kPad, 7)}),
            FromHex("f0 41 10 00 00 00 00 16 12 28 00 00 00 06 52 f7"));
}

// The 18 slots run 0x40 (pad 1 top) to 0x51 (pad 9 bottom), contiguous and
// one per slot -- mapped live across all 18. The old code used a fixed
// 0x4c/0x4d, which was pad 7 alone.
TEST(PadWaveAddr, GivesTheEighteenSlotsContiguousAddresses) {
  std::vector<uint8_t> slot_bytes;
  for (int pad = 1; pad <= 9; ++pad) {
    for (const PadSlot slot : {PadSlot::kTop, PadSlot::kBottom}) {
      const Bytes addr = PadWaveAddr(129, pad, slot);
      ASSERT_EQ(addr.size(), 4u);
      slot_bytes.push_back(addr[2]);
    }
  }
  ASSERT_EQ(slot_bytes.size(), 18u);
  EXPECT_EQ(slot_bytes.front(), 0x40);
  EXPECT_EQ(slot_bytes.back(), 0x51);
  for (size_t i = 0; i < slot_bytes.size(); ++i) {
    EXPECT_EQ(slot_bytes[i], 0x40 + i);
  }
}

// The enable flag is the same slot, a different field.
TEST(PadWaveEnableAddr, DiffersFromTheWaveAddressOnlyInTheField) {
  for (int pad = 1; pad <= 9; ++pad) {
    for (const PadSlot slot : {PadSlot::kTop, PadSlot::kBottom}) {
      Bytes wave = PadWaveAddr(129, pad, slot);
      const Bytes enable = PadWaveEnableAddr(129, pad, slot);
      EXPECT_EQ(wave.back(), 0x01);
      EXPECT_EQ(enable.back(), 0x00);
      wave.back() = 0x00;
      EXPECT_EQ(wave, enable) << "pad " << pad;
    }
  }
}

// ---- PadParamAddr ----

// The param index doubles as the byte offset in the kit record's pad block,
// which is why a stored value and a written one use the same number.
TEST(PadParamAddr, IsTheKitPrefixThenThePadThenTheParamIndex) {
  EXPECT_EQ(PadParamAddr(129, 1, 0x00), Bytes({0x06, 0x00, 0x20, 0x00}));
  EXPECT_EQ(PadParamAddr(129, 9, 0x13), Bytes({0x06, 0x00, 0x28, 0x13}));
  EXPECT_EQ(PadParamAddr(200, 1, 0x05), Bytes({0x07, 0x0e, 0x20, 0x05}));
}

// Byte-exact against the paramlog-padparams capture: the writes the sync push
// sends. The hi-hat trio is the one that was mapped live after an earlier
// guess put it a byte out.
TEST(PadParamAddr, MatchesTheCapturedPadParamWrites) {
  const struct {
    int pad;
    int param;
    uint8_t value;
    const char* hex;
  } cases[] = {
      // Pad 1: layer mode = XFADE, then fade point = 0x51.
      {1, 0x00, 0x03, "f0 41 10 00 00 00 00 16 12 06 00 20 00 03 57 f7"},
      {1, 0x01, 0x51, "f0 41 10 00 00 00 00 16 12 06 00 20 01 51 08 f7"},
      // Pad 9 (the hi-hat): volume, fade-in, decay.
      {9, 0x07, 0x64, "f0 41 10 00 00 00 00 16 12 06 00 28 07 64 67 f7"},
      {9, 0x08, 0x32, "f0 41 10 00 00 00 00 16 12 06 00 28 08 32 18 f7"},
      {9, 0x09, 0x4d, "f0 41 10 00 00 00 00 16 12 06 00 28 09 4d 7c f7"},
  };

  for (const auto& c : cases) {
    EXPECT_EQ(Dt1(PadParamAddr(129, c.pad, c.param), {c.value}), FromHex(c.hex))
        << "pad " << c.pad << " param " << c.param;
  }
}

// ---- Bulk transfer ----

TEST(BulkRequest, HasTheBulkFrameWithTheBankAndArg) {
  const Bytes req = BulkRequest(kBulkRead, kBankKits, kBulkNextChunk);

  ASSERT_EQ(req.size(), 17u);
  EXPECT_EQ(Bytes(req.begin(), req.begin() + 5),
            Bytes({0xF0, 0x41, 0x6C, 0x03, kBulkRead}));
  EXPECT_EQ(req[9], kBankKits);
  // The arg is little-endian, unlike the 7-bit addresses elsewhere.
  EXPECT_EQ(Bytes(req.begin() + 12, req.begin() + 16),
            Bytes({0x24, 0x00, 0x00, 0x00}));
  EXPECT_EQ(req.back(), 0xF7);
}

TEST(BulkRequest, WritesTheArgLittleEndian) {
  const Bytes req = BulkRequest(kBulkRead, kBankSamples, 0x12345678);
  EXPECT_EQ(Bytes(req.begin() + 12, req.begin() + 16),
            Bytes({0x78, 0x56, 0x34, 0x12}));
}

TEST(BulkReadRequest, IsThePrepareForABank) {
  for (const uint8_t bank : {kBankKits, kBankSamples, kBankMeta, kBankConfig}) {
    EXPECT_EQ(BulkReadRequest(bank), BulkRequest(kBulkPrepare, bank, 0));
  }
}

// Byte-exact: the stream-a-bank request the official app sends.
TEST(BulkReadRequest, MatchesTheCapturedRequests) {
  EXPECT_EQ(BulkReadRequest(kBankKits),
            FromHex("f0 41 6c 03 05 00 00 00 00 10 00 00 00 00 00 00 f7"));
  EXPECT_EQ(BulkReadRequest(kBankSamples),
            FromHex("f0 41 6c 03 05 00 00 00 00 20 00 00 00 00 00 00 f7"));
}

// ---- SplitBulkImage ----

TEST(SplitBulkImage, FindsEachBlockAndItsBank) {
  Bytes image = FromHex("f0 41 6c 02 00 00 00 00 10 aa bb cc f7");
  const Bytes second = FromHex("f0 41 6c 02 00 00 00 00 20 de ad f7");
  image.insert(image.end(), second.begin(), second.end());

  const std::vector<BulkBlock> blocks = SplitBulkImage(image);

  ASSERT_EQ(blocks.size(), 2u);
  EXPECT_EQ(blocks[0].bank, 0x10);
  EXPECT_EQ(blocks[0].offset, 0u);
  EXPECT_EQ(blocks[0].length, 13u);
  EXPECT_EQ(blocks[1].bank, 0x20);
  EXPECT_EQ(blocks[1].offset, 13u);
  EXPECT_EQ(blocks[1].length, 12u);
}

// A block runs to the next marker, so the last one runs to the end.
TEST(SplitBulkImage, RunsTheLastBlockToTheEndOfTheImage) {
  const Bytes image = FromHex("f0 41 6c 02 00 00 00 00 10 aa bb cc dd ee f7");
  const std::vector<BulkBlock> blocks = SplitBulkImage(image);

  ASSERT_EQ(blocks.size(), 1u);
  EXPECT_EQ(blocks[0].offset, 0u);
  EXPECT_EQ(blocks[0].length, image.size());
}

TEST(SplitBulkImage, SkipsWhateverIsNotABlock) {
  EXPECT_TRUE(SplitBulkImage({}).empty());
  EXPECT_TRUE(SplitBulkImage(FromHex("de ad be ef")).empty());
  EXPECT_TRUE(SplitBulkImage(FromHex("f0 41 6c")).empty());  // truncated marker

  // Leading junk before the first marker is not part of any block.
  const Bytes image = FromHex("ff ff f0 41 6c 02 00 00 00 00 10 aa f7");
  const std::vector<BulkBlock> blocks = SplitBulkImage(image);
  ASSERT_EQ(blocks.size(), 1u);
  EXPECT_EQ(blocks[0].offset, 2u);
}

// ---- Sample-pool registration records ----

// Byte-exact against synthupload-1.log, sample 1587.
TEST(SampleRecordAddr, MatchesTheCapturedUploadAddresses) {
  EXPECT_EQ(SampleRecordAddr(1587, 0x00), Bytes({0x10, 0x18, 0x66, 0x00}));
  EXPECT_EQ(SampleRecordAddr(1587, 0x1b), Bytes({0x10, 0x18, 0x66, 0x1b}));
  // The worked example in the header.
  EXPECT_EQ(SampleRecordAddr(1586, 0x1b), Bytes({0x10, 0x18, 0x64, 0x1b}));
}

// Four bytes of a SysEx address, across the device's whole 20,000-wave pool.
TEST(SampleRecordAddr, StaysSevenBitAcrossThePool) {
  for (int index = 0; index <= 20000; index += 97) {
    for (const uint8_t b : SampleRecordAddr(index, 0x1b)) {
      ASSERT_LE(b, 0x7F) << "index " << index;
    }
  }
}

TEST(SampleBaseRecord, MatchesTheCapturedBaseRecord) {
  const Bytes expected = FromHex(
      "00 00 00 00 00 00 00 00 00 00 00 00 01 00 00 00 00 00 00 00 "
      "00 00 00 00 7f 00 00 00 00 00 00 20 20 20 20 20 20 20 20 20 "
      "20 20 20 20 20 20 20 00 00 00 00 00 00 00 00 00 00 00 00 00 "
      "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 "
      "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 "
      "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 "
      "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 "
      "00 00 00 00 00 00 00 00 04 0b 00");
  EXPECT_EQ(SampleBaseRecord(4096), expected);
  EXPECT_EQ(SampleBaseRecord(4096).size(), 151u);
}

// The record is otherwise constant: only the size field carries the wave.
TEST(SampleBaseRecord, VariesOnlyInTheSizeField) {
  const Bytes a = SampleBaseRecord(4096);
  const Bytes b = SampleBaseRecord(8192 * 10);
  ASSERT_EQ(a.size(), b.size());
  for (size_t i = 0; i < a.size(); ++i) {
    if (i != 0x0c) {
      EXPECT_EQ(a[i], b[i]) << "byte " << i;
    }
  }
}

TEST(SampleBaseRecord, CountsTheSizeInBlocksOf4096Frames) {
  EXPECT_EQ(SampleBaseRecord(0)[0x0c], 0);
  EXPECT_EQ(SampleBaseRecord(4095)[0x0c], 0);
  EXPECT_EQ(SampleBaseRecord(4096)[0x0c], 1);
  EXPECT_EQ(SampleBaseRecord(4096 * 5)[0x0c], 5);
}

TEST(SampleNameRecord, MatchesTheCapturedNameRecord) {
  const Bytes expected = FromHex(
      "00 00 00 00 42 5f 6e 6f 69 73 65 5f 34 30 39 36 20 20 20 20 "
      "42 5f 6e 6f 69 73 65 5f 34 30 39 36 2e 77 61 76 20 20 20 20 "
      "20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 "
      "20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 "
      "20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 "
      "20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 "
      "00 04 0b 00 5a 34 54 32 33 39 33 20 0e 0e 08 0a 0b 05 03 0f");
  EXPECT_EQ(SampleNameRecord("B_noise_4096", "B_noise_4096.wav", 0xee8ab53f),
            expected);
  EXPECT_EQ(expected.size(), 140u);
}

// Both names are fixed-width fields: short ones pad with spaces, and a long
// one is cut rather than running into the next field.
TEST(SampleNameRecord, PadsAndTruncatesTheNameFields) {
  const Bytes padded = SampleNameRecord("ab", "c.wav", 0);
  EXPECT_EQ(padded[0x04], 'a');
  EXPECT_EQ(padded[0x05], 'b');
  EXPECT_EQ(padded[0x06], 0x20);
  EXPECT_EQ(padded[0x13], 0x20);  // last byte of the 16-wide wavename
  EXPECT_EQ(padded[0x14], 'c');

  const Bytes cut =
      SampleNameRecord(std::string(40, 'x'), std::string(200, 'y'), 0);
  EXPECT_EQ(cut.size(), 140u);
  EXPECT_EQ(cut[0x13], 'x');  // wavename fills its 16
  EXPECT_EQ(cut[0x14], 'y');  // and stops: the filename starts here
  EXPECT_EQ(cut[0x77], 'y');  // filename fills its 100
  EXPECT_EQ(cut[0x78], 0x00);  // and stops before the constant block
}

// The device stores this field verbatim and ignores it, so we send 0 (see
// re-cache/captures/SAMPLE-RECORD-HASH.md) -- but it still has to land where
// the capture puts it, as eight nibbles, most significant first.
TEST(SampleNameRecord, WritesTheHashFieldAsEightNibbles) {
  const Bytes b = SampleNameRecord("n", "n.wav", 0x12345678);
  EXPECT_EQ(Bytes(b.begin() + 0x84, b.begin() + 0x8c),
            Bytes({0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08}));

  const Bytes zero = SampleNameRecord("n", "n.wav", 0);
  EXPECT_EQ(Bytes(zero.begin() + 0x84, zero.begin() + 0x8c), Bytes(8, 0x00));
}

}  // namespace
}  // namespace spdsx::device
