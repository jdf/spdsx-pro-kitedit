#include "device_ops.h"

#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace spdsx::ops {
namespace {

// Nine pads, all MIX unless told otherwise.
device::KitRecord KitWithModes(std::vector<LayerMode> modes) {
  device::KitRecord record;
  for (size_t pad = 0; pad < record.pads.size(); ++pad) {
    record.pads[pad].layer_mode =
        static_cast<uint8_t>(pad < modes.size() ? modes[pad] : LayerMode::kMix);
  }
  return record;
}

std::vector<device::KitRecord> Kits(size_t count) {
  return std::vector<device::KitRecord>(count, KitWithModes({}));
}

SetModeRequest Request(std::vector<spdutil::KitRange> kits, LayerMode target) {
  SetModeRequest request;
  request.kits = std::move(kits);
  request.target = target;
  return request;
}

TEST(PlanSetMode, ChangesEveryPadOfEveryNamedKit) {
  const auto plan = PlanSetMode(Kits(3), Request({{1, 3}}, LayerMode::kHiHat));
  EXPECT_EQ(plan.size(), 27u);  // 3 kits x 9 pads
}

TEST(PlanSetMode, LeavesKitsOutsideTheSpecAlone) {
  const auto plan = PlanSetMode(Kits(10), Request({{2, 3}}, LayerMode::kHiHat));
  ASSERT_FALSE(plan.empty());
  for (const ModeChange& change : plan) {
    EXPECT_GE(change.kit, 2);
    EXPECT_LE(change.kit, 3);
  }
}

// The bug this planning exists to make impossible: a sweep meant for one
// pad reaching all nine.
TEST(PlanSetMode, PadFilterRestrictsToThosePads) {
  SetModeRequest request = Request({{1, 5}}, LayerMode::kHiHat);
  request.pads = {9};
  const auto plan = PlanSetMode(Kits(5), request);
  EXPECT_EQ(plan.size(), 5u);
  for (const ModeChange& change : plan) {
    EXPECT_EQ(change.pad, 9);
  }
}

TEST(PlanSetMode, SkipsPadsAlreadyInTheTargetMode) {
  auto kits = Kits(1);
  kits[0] = KitWithModes({LayerMode::kHiHat, LayerMode::kHiHat});
  const auto plan = PlanSetMode(kits, Request({{1, 1}}, LayerMode::kHiHat));
  EXPECT_EQ(plan.size(), 7u);  // the other seven
}

TEST(PlanSetMode, IfModeRestrictsToPadsCurrentlyInThatMode) {
  auto kits = Kits(1);
  kits[0] = KitWithModes({LayerMode::kXfade, LayerMode::kSwitch});
  SetModeRequest request = Request({{1, 1}}, LayerMode::kHiHat);
  request.has_if_mode = true;
  request.if_mode = LayerMode::kXfade;
  const auto plan = PlanSetMode(kits, request);
  ASSERT_EQ(plan.size(), 1u);
  EXPECT_EQ(plan[0].pad, 1);
  EXPECT_EQ(plan[0].from, LayerMode::kXfade);
}

TEST(PlanSetMode, ReportsWhatEachPadIsChangingFrom) {
  auto kits = Kits(1);
  kits[0] = KitWithModes({LayerMode::kAlternate});
  const auto plan = PlanSetMode(kits, Request({{1, 1}}, LayerMode::kMix));
  ASSERT_FALSE(plan.empty());
  EXPECT_EQ(plan[0].from, LayerMode::kAlternate);
}

TEST(PlanSetMode, IgnoresKitNumbersPastTheEndOfTheImage) {
  const auto plan =
      PlanSetMode(Kits(2), Request({{1, 200}}, LayerMode::kHiHat));
  EXPECT_EQ(plan.size(), 18u);  // only the two kits that exist
}

TEST(PlanSetMode, NoKitsMeansNoChanges) {
  EXPECT_TRUE(PlanSetMode(Kits(5), Request({}, LayerMode::kHiHat)).empty());
}

TEST(KitSpecText, RendersSingleKitsAndRanges) {
  EXPECT_EQ(KitSpecText({{108, 108}}), "108");
  EXPECT_EQ(KitSpecText({{108, 200}}), "108-200");
  EXPECT_EQ(KitSpecText({{1, 1}, {5, 5}, {10, 20}}), "1,5,10-20");
}

// The window shows this line and then runs the very request it came from,
// so the two cannot disagree about what is about to happen.
TEST(CommandLine, InfoIsJustTheCommand) {
  EXPECT_EQ(CommandLine(InfoRequest {}), "spdutil info");
}

TEST(CommandLine, SetModeReadsBackAsTheSpdutilInvocation) {
  SetModeRequest request = Request({{108, 200}}, LayerMode::kHiHat);
  request.pads = {9};
  request.commit = true;
  EXPECT_EQ(CommandLine(request),
            "spdutil setmode --kits 108-200 --mode HI-HAT --pad 9 --commit");
}

TEST(CommandLine, SetModeShowsTheFilterAndTheDryRun) {
  SetModeRequest request = Request({{1, 20}}, LayerMode::kMix);
  request.has_if_mode = true;
  request.if_mode = LayerMode::kHiHat;
  request.dry_run = true;
  EXPECT_EQ(CommandLine(request),
            "spdutil setmode --kits 1-20 --mode MIX --if-mode HI-HAT "
            "--dry-run");
}

TEST(CommandLine, PadLinkReadsBackAsTheSpdutilInvocation) {
  PadLinkRequest request;
  request.kits = {{108, 200}};
  request.pads = {7};
  request.group = 11;
  EXPECT_EQ(CommandLine(request),
            "spdutil padlink --kits 108-200 --group 11 --pad 7");
}

TEST(CommandLine, SetNameQuotesANameWithSpaces) {
  SetNameRequest request;
  request.kit = 199;
  request.name = "MY KIT";
  request.commit = true;
  EXPECT_EQ(CommandLine(request),
            "spdutil setname --kits 199 --name \"MY KIT\" --commit");
}

TEST(CommandLine, SetNameLeavesAPlainNameUnquoted) {
  SetNameRequest request;
  request.kit = 7;
  request.name = "DRUMS";
  EXPECT_EQ(CommandLine(request), "spdutil setname --kits 7 --name DRUMS");
}

TEST(CommandLine, SetNameEscapesAQuoteInTheName) {
  SetNameRequest request;
  request.kit = 7;
  request.name = "IT\"S";
  EXPECT_NE(CommandLine(request).find("\\\""), std::string::npos);
}

}  // namespace
}  // namespace spdsx::ops
