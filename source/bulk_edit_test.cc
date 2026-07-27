#include "bulk_edit.h"

#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "temp_dir.h"

namespace spdsx::bulk {
namespace {

// All-MIX kits. A default KitData is the factory kit, whose pad 9 is
// HI-HAT; these tests want a uniform starting point.
std::vector<KitData> Kits(size_t count) {
  std::vector<KitData> kits(count);
  for (KitData& kit : kits) {
    for (Pad& pad : kit.pads) {
      pad.params.mode = LayerMode::kMix;
    }
  }
  return kits;
}

ops::SetModeRequest Request(std::vector<spdutil::KitRange> kits,
                            LayerMode target) {
  ops::SetModeRequest request;
  request.kits = std::move(kits);
  request.target = target;
  return request;
}

TEST(BulkSetMode, AppliesToTheNamedKitsOnly) {
  auto kits = Kits(10);
  const auto plan = ApplySetMode(kits, Request({{2, 3}}, LayerMode::kHiHat));
  EXPECT_EQ(plan.size(), 18u);
  EXPECT_EQ(kits[0].pads[0].params.mode, LayerMode::kMix);
  EXPECT_EQ(kits[1].pads[0].params.mode, LayerMode::kHiHat);
  EXPECT_EQ(kits[2].pads[8].params.mode, LayerMode::kHiHat);
  EXPECT_EQ(kits[3].pads[0].params.mode, LayerMode::kMix);
}

TEST(BulkSetMode, PadFilterTouchesOnlyThosePads) {
  auto kits = Kits(5);
  ops::SetModeRequest request = Request({{1, 5}}, LayerMode::kHiHat);
  request.pads = {9};
  ApplySetMode(kits, request);
  for (const KitData& kit : kits) {
    for (size_t pad = 0; pad < 8; ++pad) {
      EXPECT_EQ(kit.pads[pad].params.mode, LayerMode::kMix);
    }
    EXPECT_EQ(kit.pads[8].params.mode, LayerMode::kHiHat);
  }
}

TEST(BulkSetMode, ChangesTheModeAndNothingElse) {
  auto kits = Kits(1);
  kits[0].pads[0].params.fade_point = 77;
  kits[0].pads[0].params.fixed_velocity = 42;
  kits[0].pads[0].samples.first = LayerSample::DeviceWave(1234);
  ApplySetMode(kits, Request({{1, 1}}, LayerMode::kXfade));
  const Pad& pad = kits[0].pads[0];
  EXPECT_EQ(pad.params.mode, LayerMode::kXfade);
  EXPECT_EQ(pad.params.fade_point, 77);
  EXPECT_EQ(pad.params.fixed_velocity, 42);
  EXPECT_EQ(pad.samples.first.device_index, 1234);
}

TEST(BulkSetMode, IfModeConvertsOnlyMatchingPads) {
  auto kits = Kits(1);
  kits[0].pads[0].params.mode = LayerMode::kXfade;
  kits[0].pads[1].params.mode = LayerMode::kSwitch;
  ops::SetModeRequest request = Request({{1, 1}}, LayerMode::kHiHat);
  request.has_if_mode = true;
  request.if_mode = LayerMode::kXfade;
  const auto plan = ApplySetMode(kits, request);
  EXPECT_EQ(plan.size(), 1u);
  EXPECT_EQ(kits[0].pads[0].params.mode, LayerMode::kHiHat);
  EXPECT_EQ(kits[0].pads[1].params.mode, LayerMode::kSwitch);
}

TEST(BulkSetMode, PlanReportsWithoutChanging) {
  auto kits = Kits(3);
  const auto plan = PlanSetMode(kits, Request({{1, 3}}, LayerMode::kHiHat));
  EXPECT_EQ(plan.size(), 27u);
  EXPECT_EQ(kits[0].pads[0].params.mode, LayerMode::kMix);
}

TEST(BulkSetMode, AlreadyThereMeansNoChange) {
  auto kits = Kits(2);
  const auto plan = ApplySetMode(kits, Request({{1, 2}}, LayerMode::kMix));
  EXPECT_TRUE(plan.empty());
}

TEST(BulkPadLink, PlansOnlyPadsNotAlreadyInTheGroup) {
  auto kits = Kits(3);
  kits[0].pads[6].params.pad_link = 11;  // kit 1 pad 7 already there
  ops::PadLinkRequest request;
  request.kits = {{1, 3}};
  request.pads = {7};
  request.group = 11;
  const auto plan = PlanPadLink(kits, request);
  ASSERT_EQ(plan.size(), 2u);
  EXPECT_EQ(plan[0].kit, 2);
  EXPECT_EQ(plan[1].kit, 3);
}

TEST(BulkPadLink, GroupZeroUnlinks) {
  auto kits = Kits(2);
  kits[0].pads[0].params.pad_link = 5;
  ops::PadLinkRequest request;
  request.kits = {{1, 2}};
  request.pads = {1};
  request.group = 0;
  const auto plan = PlanPadLink(kits, request);
  ASSERT_EQ(plan.size(), 1u);  // only the linked pad changes
  EXPECT_EQ(plan[0].from, 5);
}

// The action against a real (untitled) document: perform lands the modes
// and dirties the kits; undo restores every prior mode exactly.
class SetModeActionTest : public ::testing::Test {
protected:
  void SetUp() override {
    juce::PropertiesFile::Options options;
    options.applicationName = "spdsx-patchedit-test";
    options.filenameSuffix = ".settings";
    options.osxLibrarySubFolder = "Application Support";
    options.folderName = temp.dir().getFullPathName();
    settings.setStorageParameters(options);
    ASSERT_TRUE(settings.getUserSettings()->getFile().isAChildOf(temp.dir()));
    document = std::make_unique<DeviceDocument>(device, model, settings);
    document->ResetToUntitled();
  }

  spdsx_testing::TempDir temp;
  DeviceModel device;
  KitModel model;
  juce::ApplicationProperties settings;
  std::unique_ptr<DeviceDocument> document;
};

TEST_F(SetModeActionTest, PerformAppliesAndUndoRestoresExactly) {
  device.kit(4).pads[0].params.mode = LayerMode::kXfade;  // kit 5 pad 1
  ops::SetModeRequest request;
  request.kits = {{5, 6}};
  request.target = LayerMode::kHiHat;

  std::vector<KitData> snapshot;
  for (int i = 0; i < DeviceModel::kKitCount; ++i) {
    snapshot.push_back(device.kit(i));
  }
  const auto plan = PlanSetMode(snapshot, request);
  juce::UndoManager undo;
  undo.beginNewTransaction("layer mode change");
  ASSERT_TRUE(
      undo.perform(new SetModeAction(*document, device, plan, request.target)));

  EXPECT_EQ(device.kit(4).pads[0].params.mode, LayerMode::kHiHat);
  EXPECT_EQ(device.kit(5).pads[3].params.mode, LayerMode::kHiHat);
  EXPECT_TRUE(document->KitDirtyVsBase(5));
  EXPECT_EQ(undo.getUndoDescription(), "layer mode change");

  ASSERT_TRUE(undo.undo());
  EXPECT_EQ(device.kit(4).pads[0].params.mode, LayerMode::kXfade);
  EXPECT_EQ(device.kit(5).pads[3].params.mode, LayerMode::kMix);
  // Kit 5 was ALREADY dirty before the action (XFADE against a MIX
  // base), so undo returns it to dirty; kit 6 started clean and must
  // end clean.
  EXPECT_TRUE(document->KitDirtyVsBase(4));
  EXPECT_FALSE(document->KitDirtyVsBase(5));
}

TEST_F(SetModeActionTest, TheActiveKitReloadsIntoTheModel) {
  device.set_current_kit(9);
  document->SwitchKit(9);
  ops::SetModeRequest request;
  request.kits = {{10, 10}};
  request.target = LayerMode::kAlternate;
  std::vector<KitData> snapshot;
  for (int i = 0; i < DeviceModel::kKitCount; ++i) {
    snapshot.push_back(device.kit(i));
  }
  juce::UndoManager undo;
  undo.perform(new SetModeAction(
      *document, device, PlanSetMode(snapshot, request), request.target));
  EXPECT_EQ(model.params(0).mode, LayerMode::kAlternate);
  undo.undo();
  EXPECT_EQ(model.params(0).mode, LayerMode::kMix);
}

TEST_F(SetModeActionTest, PadLinkActionRoundTrips) {
  device.kit(2).pads[6].params.pad_link = 4;  // kit 3 pad 7, group 4
  ops::PadLinkRequest request;
  request.kits = {{3, 4}};
  request.pads = {7};
  request.group = 11;
  std::vector<KitData> snapshot;
  for (int i = 0; i < DeviceModel::kKitCount; ++i) {
    snapshot.push_back(device.kit(i));
  }
  juce::UndoManager undo;
  undo.beginNewTransaction("pad link change");
  ASSERT_TRUE(undo.perform(new PadLinkAction(
      *document, device, PlanPadLink(snapshot, request), request.group)));
  EXPECT_EQ(device.kit(2).pads[6].params.pad_link, 11);
  EXPECT_EQ(device.kit(3).pads[6].params.pad_link, 11);
  ASSERT_TRUE(undo.undo());
  EXPECT_EQ(device.kit(2).pads[6].params.pad_link, 4);
  EXPECT_EQ(device.kit(3).pads[6].params.pad_link, 0);
}

}  // namespace
}  // namespace spdsx::bulk
