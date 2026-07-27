#include "bulk_edit.h"

#include <vector>

#include <gtest/gtest.h>

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

}  // namespace
}  // namespace spdsx::bulk
