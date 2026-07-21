#include "device_sync.h"

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "device/protocol.h"
#include "fake_serial_port.h"

namespace spdsx {
namespace {

using device::Bytes;
using spdsx_testing::FakeSerialPort;

device::SampleRecord Record(int index) {
  device::SampleRecord record;
  record.index = index;
  return record;
}

// A three-kit cast: `base` is the agreed past, and tests derive `current`
// and `theirs` from it so every difference is deliberate.
KitData SyncBase() {
  KitData kit;
  kit.name = "BASE KIT";
  kit.pads[0].samples.first = LayerSample::DeviceWave(127);
  kit.pads[0].params.fade_point = 80;
  return kit;
}

std::array<SyncResolution, KitModel::kPadCount> AllMine() {
  std::array<SyncResolution, KitModel::kPadCount> r;
  r.fill(SyncResolution::kMine);
  return r;
}

// ---- KitDataFromDevice / DeviceParamsFromPad ----

TEST(DeviceSyncMapping, KitDataFromDeviceMapsEveryField) {
  device::KitRecord rec;
  rec.name = "ZZZ";
  auto& dp = rec.pads[8];
  dp.layer_mode = 7;  // HI-HAT
  dp.fade_point = 30;
  dp.fade_end = 90;
  dp.dynamics = 0;
  dp.dynamics_curve = 3;
  dp.fixed_velocity = 100;
  dp.trigger_reserve = 1;
  dp.hi_hat_volume = 80;
  dp.hi_hat_fade_in = 5;
  dp.hi_hat_decay = 25;
  dp.wave_top = 1590;
  dp.wave_bottom = 0;

  const KitData kit = KitDataFromDevice(rec);

  EXPECT_EQ(kit.name, juce::String("ZZZ"));
  const Pad& pad = kit.pads[8];
  EXPECT_EQ(pad.params.mode, LayerMode::kHiHat);
  EXPECT_EQ(pad.params.fade_point, 30);
  EXPECT_EQ(pad.params.fade_end, 90);
  EXPECT_FALSE(pad.params.dynamics);
  EXPECT_EQ(pad.params.curve, DynamicsCurve::kLoud3);
  EXPECT_EQ(pad.params.fixed_velocity, 100);
  EXPECT_TRUE(pad.params.trigger_reserve);
  EXPECT_EQ(pad.params.hi_hat_volume, 80);
  EXPECT_EQ(pad.params.hi_hat_fade_in, 5);
  EXPECT_EQ(pad.params.hi_hat_decay, 25);
  EXPECT_EQ(pad.samples.first, LayerSample::DeviceWave(1590));
  EXPECT_TRUE(pad.samples.second.empty());
}

// The device mapping has to survive a round trip, or every sync would see
// phantom changes on untouched pads.
TEST(DeviceSyncMapping, DeviceParamsRoundTrip) {
  device::KitRecord rec;
  auto& in = rec.pads[3];
  in.layer_mode = 3;  // XFADE
  in.fade_point = 40;
  in.fade_end = 100;
  in.dynamics = 1;
  in.dynamics_curve = 2;
  in.fixed_velocity = 90;
  in.hi_hat_volume = 70;
  in.hi_hat_fade_in = 2;
  in.hi_hat_decay = 33;
  in.trigger_reserve = 0;
  in.wave_top = 500;
  in.wave_bottom = 7;

  const device::PadDeviceParams out =
      DeviceParamsFromPad(KitDataFromDevice(rec).pads[3]);

  EXPECT_EQ(out.layer_mode, in.layer_mode);
  EXPECT_EQ(out.fade_point, in.fade_point);
  EXPECT_EQ(out.fade_end, in.fade_end);
  EXPECT_EQ(out.dynamics, in.dynamics);
  EXPECT_EQ(out.dynamics_curve, in.dynamics_curve);
  EXPECT_EQ(out.fixed_velocity, in.fixed_velocity);
  EXPECT_EQ(out.hi_hat_volume, in.hi_hat_volume);
  EXPECT_EQ(out.hi_hat_fade_in, in.hi_hat_fade_in);
  EXPECT_EQ(out.hi_hat_decay, in.hi_hat_decay);
  EXPECT_EQ(out.trigger_reserve, in.trigger_reserve);
  EXPECT_EQ(out.wave_top, in.wave_top);
  EXPECT_EQ(out.wave_bottom, in.wave_bottom);
}

// A layer still holding a local file has no wave number yet; uploads
// substitute indices before any write is built, so this maps to 0.
TEST(DeviceSyncMapping, FileLayerMapsToWaveZero) {
  Pad pad;
  pad.samples.first = LayerSample(juce::File("/tmp/kick.wav"));
  EXPECT_EQ(DeviceParamsFromPad(pad).wave_top, 0);
}

// ---- MergePad: the field-wise three-way rules ----

TEST(DeviceSyncMerge, TakesALocalEditTheDeviceDidNotTouch) {
  const Pad base = SyncBase().pads[0];
  Pad current = base;
  current.params.fade_point = 90;

  juce::StringArray conflicts;
  const Pad merged = MergePad(current, base, base, true, &conflicts);

  EXPECT_EQ(merged, current);
  EXPECT_TRUE(conflicts.isEmpty());
}

TEST(DeviceSyncMerge, TakesADeviceEditNotMadeLocally) {
  const Pad base = SyncBase().pads[0];
  Pad theirs = base;
  theirs.samples.second = LayerSample::DeviceWave(200);

  const Pad merged = MergePad(base, base, theirs, true);

  EXPECT_EQ(merged, theirs);
}

// Different fields edited on the two sides are not a conflict: both edits
// land.
TEST(DeviceSyncMerge, MergesIndependentFieldEditsFromBothSides) {
  const Pad base = SyncBase().pads[0];
  Pad current = base;
  current.params.fade_point = 90;
  Pad theirs = base;
  theirs.params.curve = DynamicsCurve::kLoud2;

  juce::StringArray conflicts;
  const Pad merged = MergePad(current, base, theirs, true, &conflicts);

  EXPECT_EQ(merged.params.fade_point, 90);
  EXPECT_EQ(merged.params.curve, DynamicsCurve::kLoud2);
  EXPECT_TRUE(conflicts.isEmpty());
}

TEST(DeviceSyncMerge, ConvergentEditsAreNotAConflict) {
  const Pad base = SyncBase().pads[0];
  Pad current = base;
  current.params.fade_point = 90;
  const Pad theirs = current;

  juce::StringArray conflicts;
  const Pad merged = MergePad(current, base, theirs, false, &conflicts);

  EXPECT_EQ(merged.params.fade_point, 90);
  EXPECT_TRUE(conflicts.isEmpty());
}

TEST(DeviceSyncMerge, AConflictResolvesTowardTheChosenSide) {
  const Pad base = SyncBase().pads[0];
  Pad current = base;
  current.params.fade_point = 90;
  Pad theirs = base;
  theirs.params.fade_point = 100;

  EXPECT_EQ(MergePad(current, base, theirs, true).params.fade_point, 90);
  EXPECT_EQ(MergePad(current, base, theirs, false).params.fade_point, 100);
}

// The conflict line carries both values — it IS the dialog text.
TEST(DeviceSyncMerge, DescribesAConflictWithBothValues) {
  const Pad base = SyncBase().pads[0];
  Pad current = base;
  current.params.fade_point = 90;
  Pad theirs = base;
  theirs.params.fade_point = 100;

  const juce::StringArray conflicts = PadConflicts(current, base, theirs);

  ASSERT_EQ(conflicts.size(), 1);
  EXPECT_EQ(conflicts[0], juce::String("fade point (yours 90, device 100)"));
}

// A local file against a device-side wave change is the sample conflict
// the upload path creates; both identities must read clearly.
TEST(DeviceSyncMerge, DescribesASampleConflictByFileAndWave) {
  const Pad base = SyncBase().pads[0];
  Pad current = base;
  current.samples.first = LayerSample(juce::File("/tmp/kick.wav"));
  Pad theirs = base;
  theirs.samples.first = LayerSample::DeviceWave(203);

  const juce::StringArray conflicts = PadConflicts(current, base, theirs);

  ASSERT_EQ(conflicts.size(), 1);
  EXPECT_EQ(conflicts[0],
            juce::String("top sample (yours kick.wav, device wave 203)"));
}

// ---- FindKitConflicts ----

TEST(DeviceSyncConflicts, ListsTheNameAndEachConflictedPad) {
  const KitData base = SyncBase();
  KitData current = base;
  current.name = "MINE";
  current.pads[4].params.dynamics = false;
  KitData theirs = base;
  theirs.name = "THEIRS";
  theirs.pads[4].params.dynamics = false;  // convergent: not a conflict
  theirs.pads[7].params.mode = LayerMode::kSwitch;
  current.pads[7].params.mode = LayerMode::kAlternate;

  const std::vector<SyncConflict> conflicts =
      FindKitConflicts(41, current, base, theirs);

  ASSERT_EQ(conflicts.size(), 2u);
  EXPECT_EQ(conflicts[0].kit, 41);
  EXPECT_EQ(conflicts[0].pad, -1);
  EXPECT_TRUE(conflicts[0].description.contains("Kit 42"));
  EXPECT_TRUE(conflicts[0].description.contains("kit name"));
  EXPECT_EQ(conflicts[1].pad, 7);
  EXPECT_TRUE(conflicts[1].description.contains("pad 8"));
  EXPECT_TRUE(conflicts[1].description.contains("layer mode"));
}

TEST(DeviceSyncConflicts, ACleanKitHasNone) {
  const KitData base = SyncBase();
  EXPECT_TRUE(FindKitConflicts(0, base, base, base).empty());
}

// ---- PlanKitSync ----

TEST(DeviceSyncPlan, NothingChangedPlansNothing) {
  const KitData base = SyncBase();
  const KitSyncPlan plan =
      PlanKitSync(base, base, base, SyncResolution::kMine, AllMine());

  EXPECT_FALSE(plan.WritesDevice());
  EXPECT_FALSE(plan.skipped);
  EXPECT_EQ(plan.new_current, base);
  EXPECT_EQ(plan.new_base, base);
}

TEST(DeviceSyncPlan, ALocalEditWritesThatPadAndAdvancesBase) {
  const KitData base = SyncBase();
  KitData current = base;
  current.pads[2].params.fade_end = 120;
  current.pads[2].samples.second = LayerSample::DeviceWave(300);

  const KitSyncPlan plan =
      PlanKitSync(current, base, base, SyncResolution::kMine, AllMine());

  EXPECT_TRUE(plan.write_params[2]);
  EXPECT_FALSE(plan.write_wave[2][0]);
  EXPECT_TRUE(plan.write_wave[2][1]);
  for (int pad = 0; pad < KitModel::kPadCount; ++pad) {
    if (pad != 2) {
      EXPECT_FALSE(plan.write_params[static_cast<size_t>(pad)]) << pad;
    }
  }
  EXPECT_EQ(plan.new_current, current);
  EXPECT_EQ(plan.new_base, current);  // pushed = the new agreed past
}

TEST(DeviceSyncPlan, ADeviceEditPullsWithoutWriting) {
  const KitData base = SyncBase();
  KitData theirs = base;
  theirs.name = "RENAMED ON UNIT";
  theirs.pads[5].params.trigger_reserve = true;

  const KitSyncPlan plan =
      PlanKitSync(base, base, theirs, SyncResolution::kMine, AllMine());

  EXPECT_FALSE(plan.WritesDevice());
  EXPECT_EQ(plan.new_current, theirs);
  EXPECT_EQ(plan.new_base, theirs);
}

TEST(DeviceSyncPlan, ALocalRenameWritesTheName) {
  const KitData base = SyncBase();
  KitData current = base;
  current.name = "MY NAME";

  const KitSyncPlan plan =
      PlanKitSync(current, base, base, SyncResolution::kMine, AllMine());

  EXPECT_TRUE(plan.write_name);
  EXPECT_EQ(plan.new_base.name, juce::String("MY NAME"));
}

// "Do nothing" is the resolution that must NOT advance base: the pad
// stays different and re-flags on the next sync.
TEST(DeviceSyncPlan, SkippingAConflictLeavesThePadAndItsBase) {
  const KitData base = SyncBase();
  KitData current = base;
  current.pads[0].params.fade_point = 90;
  current.pads[1].params.fade_end = 111;  // clean local edit elsewhere
  KitData theirs = base;
  theirs.pads[0].params.fade_point = 100;

  auto resolutions = AllMine();
  resolutions[0] = SyncResolution::kSkip;
  const KitSyncPlan plan =
      PlanKitSync(current, base, theirs, SyncResolution::kMine, resolutions);

  EXPECT_TRUE(plan.skipped);
  EXPECT_EQ(plan.new_current.pads[0], current.pads[0]);
  EXPECT_EQ(plan.new_base.pads[0], base.pads[0]);  // unadvanced
  EXPECT_FALSE(plan.write_params[0]);
  // The clean edit on pad 2 still goes out.
  EXPECT_TRUE(plan.write_params[1]);
  EXPECT_EQ(plan.new_base.pads[1], current.pads[1]);
}

// Resolving toward the device needs no write — the device already holds
// the winning value; the document pulls it instead.
TEST(DeviceSyncPlan, AConflictResolvedTheirsPullsInsteadOfWriting) {
  const KitData base = SyncBase();
  KitData current = base;
  current.pads[0].params.fade_point = 90;
  KitData theirs = base;
  theirs.pads[0].params.fade_point = 100;

  auto resolutions = AllMine();
  resolutions[0] = SyncResolution::kTheirs;
  const KitSyncPlan plan =
      PlanKitSync(current, base, theirs, SyncResolution::kMine, resolutions);

  EXPECT_FALSE(plan.WritesDevice());
  EXPECT_EQ(plan.new_current.pads[0].params.fade_point, 100);
  EXPECT_EQ(plan.new_base.pads[0].params.fade_point, 100);
}

TEST(DeviceSyncPlan, AConflictResolvedMineWritesTheDevice) {
  const KitData base = SyncBase();
  KitData current = base;
  current.name = "MINE";
  KitData theirs = base;
  theirs.name = "THEIRS";

  const KitSyncPlan plan =
      PlanKitSync(current, base, theirs, SyncResolution::kMine, AllMine());

  EXPECT_TRUE(plan.write_name);
  EXPECT_EQ(plan.new_current.name, juce::String("MINE"));
}

// ---- Free pool indices ----

TEST(DeviceSyncPool, AnEmptyPoolStartsAtOne) {
  EXPECT_EQ(NextFreeSampleIndex({}), 1);
}

TEST(DeviceSyncPool, FillsTheFirstGap) {
  const std::vector<device::SampleRecord> pool = {
      Record(1), Record(2), Record(4)};
  EXPECT_EQ(NextFreeSampleIndex(pool), 3);
  EXPECT_EQ(NextFreeSampleIndex(pool, 3), 5);
}

TEST(DeviceSyncPool, SkipsPastAContiguousRun) {
  const std::vector<device::SampleRecord> pool = {
      Record(1), Record(2), Record(3)};
  EXPECT_EQ(NextFreeSampleIndex(pool), 4);
}

TEST(DeviceSyncPool, AFullPoolHasNoFreeIndex) {
  std::vector<device::SampleRecord> pool;
  for (int i = 1; i < device::kSampleSlots; ++i) {
    pool.push_back(Record(i));
  }
  EXPECT_EQ(NextFreeSampleIndex(pool), 0);
}

// ---- Upload planning + substitution ----

TEST(DeviceSyncUploads, CollectsDistinctWrittenFilesAndAssignsFreeIndices) {
  const KitData base = SyncBase();
  KitData current = base;
  const juce::File kick("/tmp/kick.wav");
  const juce::File snare("/tmp/snare.wav");
  current.pads[0].samples.first = LayerSample(kick);
  current.pads[1].samples.first = LayerSample(kick);  // same file twice
  current.pads[1].samples.second = LayerSample(snare);

  std::vector<std::pair<int, KitSyncPlan>> plans;
  plans.emplace_back(
      0, PlanKitSync(current, base, base, SyncResolution::kMine, AllMine()));
  const std::vector<device::SampleRecord> pool = {Record(1), Record(3)};

  const std::vector<UploadPlan> uploads = PlanUploads(plans, pool);

  ASSERT_EQ(uploads.size(), 2u);
  EXPECT_EQ(uploads[0].file, kick);
  EXPECT_EQ(uploads[0].index, 2);
  EXPECT_EQ(uploads[0].wavename, "kick");
  EXPECT_EQ(uploads[0].filename, "kick.wav");
  EXPECT_EQ(uploads[1].file, snare);
  EXPECT_EQ(uploads[1].index, 4);
}

TEST(DeviceSyncUploads, IgnoresFileLayersTheSyncWillNotWrite) {
  // A skipped conflict leaves its file layer alone — it must not upload.
  const KitData base = SyncBase();
  KitData current = base;
  current.pads[0].samples.first = LayerSample(juce::File("/tmp/kick.wav"));
  KitData theirs = base;
  theirs.pads[0].samples.first = LayerSample::DeviceWave(9);

  auto resolutions = AllMine();
  resolutions[0] = SyncResolution::kSkip;
  std::vector<std::pair<int, KitSyncPlan>> plans;
  plans.emplace_back(
      0,
      PlanKitSync(current, base, theirs, SyncResolution::kMine, resolutions));

  EXPECT_TRUE(PlanUploads(plans, {}).empty());
}

TEST(DeviceSyncUploads, SubstitutionTurnsFilesIntoWavesInBothSnapshots) {
  const KitData base = SyncBase();
  KitData current = base;
  const juce::File kick("/tmp/kick.wav");
  current.pads[0].samples.first = LayerSample(kick);

  std::vector<std::pair<int, KitSyncPlan>> plans;
  plans.emplace_back(
      0, PlanKitSync(current, base, base, SyncResolution::kMine, AllMine()));
  const std::vector<UploadPlan> uploads = PlanUploads(plans, {});
  ASSERT_EQ(uploads.size(), 1u);

  SubstituteUploads(plans, uploads);

  const LayerSample expected = LayerSample::DeviceWave(uploads[0].index);
  EXPECT_EQ(plans[0].second.new_current.pads[0].samples.first, expected);
  EXPECT_EQ(plans[0].second.new_base.pads[0].samples.first, expected);
}

// ---- BuildKitWrite ----

TEST(DeviceSyncWrite, CarriesOnlyThePadsThatNeedWrites) {
  const KitData base = SyncBase();
  KitData current = base;
  current.name = "MINE";
  current.pads[6].params.fade_point = 50;
  current.pads[8].samples.first = LayerSample::DeviceWave(42);

  const KitSyncPlan plan =
      PlanKitSync(current, base, base, SyncResolution::kMine, AllMine());
  const KitWrite write = BuildKitWrite(198, plan);

  EXPECT_EQ(write.kit, 199);  // device kits are 1-based
  EXPECT_TRUE(write.name);
  EXPECT_EQ(write.kit_name, "MINE");
  ASSERT_EQ(write.pads.size(), 2u);
  EXPECT_EQ(write.pads[0].pad, 7);
  EXPECT_TRUE(write.pads[0].params);
  EXPECT_FALSE(write.pads[0].wave[0]);
  EXPECT_EQ(write.pads[1].pad, 9);
  EXPECT_FALSE(write.pads[1].params);
  EXPECT_TRUE(write.pads[1].wave[0]);
  EXPECT_EQ(write.pads[1].dp.wave_top, 42);
}

// ---- ExecutePush, against the fake port ----

// The commit-poll reply the device sends when the flash write is done.
Bytes SyncCommitDone() {
  Bytes r(19, 0x00);
  r[0] = 0xF0;
  r[1] = 0x41;
  r[2] = 0x6A;
  r[3] = 0x02;
  r[8] = 0x22;
  r[14] = 0x01;
  r[18] = 0xF7;
  return r;
}

TEST(DeviceSyncPush, WritesNameWaveAndParamsThenCommitsOnce) {
  FakeSerialPort port;
  device::SpdsxDevice dev(&port);

  KitWrite kw;
  kw.kit = 199;
  kw.name = true;
  kw.kit_name = "MINE";
  PadWrite pw;
  pw.pad = 7;
  pw.params = true;
  pw.wave = {true, false};
  pw.dp.wave_top = 42;
  pw.dp.fade_point = 50;
  kw.pads.push_back(pw);

  port.QueueReply({0x7a});  // the pad focus before the param writes
  port.QueueReply({0x7a});  // commit begin ack
  port.QueueReply(SyncCommitDone());

  EXPECT_TRUE(ExecutePush(dev, {}, {kw}, {}, 0.0));

  const std::vector<Bytes> sent = port.payloads();
  // 16 name chars + wave + enable + focus + 10 params + begin + poll.
  ASSERT_EQ(sent.size(), 31u);
  using namespace device;
  EXPECT_EQ(sent[0], Dt1(KitNameAddr(199, 0), {'M'}));
  EXPECT_EQ(sent[15], Dt1(KitNameAddr(199, 15), {' '}));  // space-padded
  EXPECT_EQ(sent[16],
            Dt1(PadWaveAddr(199, 7, PadSlot::kTop), NibbleEncode(42)));
  EXPECT_EQ(sent[17], Dt1(PadWaveEnableAddr(199, 7, PadSlot::kTop), {0x01}));
  EXPECT_EQ(sent[18],
            Dt1(kObjectSelectAddr, {SelectValue(ObjectKind::kPad, 7)}));
  EXPECT_EQ(sent[19], Dt1(PadParamAddr(199, 7, 0x00), {0x00}));  // layer mode
  EXPECT_EQ(sent[20], Dt1(PadParamAddr(199, 7, 0x01), {50}));  // fade point
  EXPECT_EQ(sent[29][4], 0x21);  // one commit at the very end
  EXPECT_EQ(sent[30][4], 0x22);
}

TEST(DeviceSyncPush, NothingToWriteMeansNoTrafficAndSuccess) {
  FakeSerialPort port;
  device::SpdsxDevice dev(&port);

  EXPECT_TRUE(ExecutePush(dev, {}, {}, {}, 0.0));
  EXPECT_TRUE(port.writes().empty());
}

TEST(DeviceSyncPush, ReportsACommitTheDeviceNeverConfirms) {
  FakeSerialPort port;
  device::SpdsxDevice dev(&port);

  KitWrite kw;
  kw.kit = 1;
  kw.name = true;
  kw.kit_name = "X";

  EXPECT_FALSE(ExecutePush(dev, {}, {kw}, {}, 0.0, /*commit_timeout=*/0.0));
}

TEST(DeviceSyncPush, UploadsGoOutFirstAndReportAsTheyLand) {
  FakeSerialPort port;
  device::SpdsxDevice dev(&port);

  SmpUpload upload;
  upload.index = 1587;
  upload.smp = device::PcmToRfwv(Bytes(8192, 0), 48000, 1, 16);
  upload.wavename = "kick";
  upload.filename = "kick.wav";

  // The upload's 18 commands (see the UploadWave suite), only the commit
  // poll of which is inspected here.
  for (int i = 0; i < 17; ++i) {
    port.QueueReply({0x7a});
  }
  port.QueueReply(SyncCommitDone());

  std::vector<int> reported;
  EXPECT_TRUE(ExecutePush(
      dev,
      {upload},
      {},
      [&reported](const SmpUpload& u) { reported.push_back(u.index); },
      0.0));

  EXPECT_EQ(reported, std::vector<int>({1587}));
  ASSERT_EQ(port.payloads().size(), 18u);
  EXPECT_EQ(port.payloads()[0][4], 0x09);  // the upload session begin
}

// The whole pipeline: a local file layer plans an upload, substitution
// gives it a pool index, and the push writes that index to the device.
TEST(DeviceSyncPush, EndToEndFromPlanToWire) {
  const KitData base = SyncBase();
  KitData current = base;
  current.pads[0].samples.second = LayerSample(juce::File("/tmp/clap.wav"));

  std::vector<std::pair<int, KitSyncPlan>> plans;
  plans.emplace_back(
      4, PlanKitSync(current, base, base, SyncResolution::kMine, AllMine()));
  const std::vector<device::SampleRecord> pool = {Record(1), Record(2)};
  const std::vector<UploadPlan> uploads = PlanUploads(plans, pool);
  ASSERT_EQ(uploads.size(), 1u);
  EXPECT_EQ(uploads[0].index, 3);
  SubstituteUploads(plans, uploads);
  const KitWrite write = BuildKitWrite(plans[0].first, plans[0].second);

  FakeSerialPort port;
  device::SpdsxDevice dev(&port);
  port.QueueReply({0x7a});  // commit begin ack
  port.QueueReply(SyncCommitDone());

  EXPECT_TRUE(ExecutePush(dev, {}, {write}, {}, 0.0));

  using namespace device;
  const std::vector<Bytes> sent = port.payloads();
  ASSERT_EQ(sent.size(), 4u);  // wave + enable + commit begin + poll
  EXPECT_EQ(sent[0], Dt1(PadWaveAddr(5, 1, PadSlot::kBottom), NibbleEncode(3)));
  EXPECT_EQ(sent[1], Dt1(PadWaveEnableAddr(5, 1, PadSlot::kBottom), {0x01}));
}

}  // namespace
}  // namespace spdsx
