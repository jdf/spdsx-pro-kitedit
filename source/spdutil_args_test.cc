#include "spdutil_args.h"

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "layers.h"

namespace spdsx::spdutil {
namespace {

TEST(SpdutilCommands, KnowsTheRealOnes) {
  EXPECT_TRUE(IsCommand("help"));
  EXPECT_TRUE(IsCommand("setmode"));
  EXPECT_TRUE(IsCommand("padlink"));
  EXPECT_FALSE(IsCommand("pnig"));
  EXPECT_FALSE(IsCommand(""));
}

TEST(SpdutilCommands, SuggestsANearMiss) {
  EXPECT_EQ(NearestCommand("hlep"), "help");
  EXPECT_EQ(NearestCommand("setmoed"), "setmode");
}

TEST(SpdutilCommands, StaysQuietWhenNothingIsClose) {
  EXPECT_EQ(NearestCommand("frobnicate"), "");
}

// Dropping this flag would turn a one-pad sweep into a nine-pad one.
TEST(UnacceptedFlag, SetmodeTakesThePadFilter) {
  EXPECT_EQ(UnacceptedFlag("setmode", {"mode", "pad", "kits"}), "");
}

TEST(UnacceptedFlag, PingTakesNoPad) {
  EXPECT_EQ(UnacceptedFlag("help", {"pad"}), "pad");
}

// A rehearsal that writes is worse than no rehearsal.
TEST(UnacceptedFlag, DryRunIsAcceptedByTheWriteCommandsThatHonorIt) {
  for (const char* command :
       {"assign", "setname", "setparams", "setlayer", "setmode", "padlink"}) {
    EXPECT_EQ(UnacceptedFlag(command, {"dry-run"}), "") << command;
  }
}

TEST(UnacceptedFlag, DryRunIsRejectedWhereItCannotWork) {
  for (const char* command : {"help", "dump", "readwave", "deletewave"}) {
    EXPECT_EQ(UnacceptedFlag(command, {"dry-run"}), "dry-run") << command;
  }
}

TEST(UnacceptedFlag, CommitIsRejectedByReadOnlyCommands) {
  EXPECT_EQ(UnacceptedFlag("kits", {"commit"}), "commit");
  EXPECT_EQ(UnacceptedFlag("selectkit", {"commit"}), "commit");
}

TEST(UnacceptedFlag, CommitIsAcceptedWhereTheCommandReallyCommits) {
  EXPECT_EQ(UnacceptedFlag("setname", {"commit"}), "");
  EXPECT_EQ(UnacceptedFlag("deletewave", {"commit"}), "");
  EXPECT_EQ(UnacceptedFlag("sendwave", {"commit"}), "");
}

TEST(UnacceptedFlag, KitsBelongsToTheKitWrites) {
  for (const char* command :
       {"setmode", "padlink", "setname", "assign", "setparams", "setlayer"}) {
    EXPECT_EQ(UnacceptedFlag(command, {"kits"}), "") << command;
  }
  EXPECT_EQ(UnacceptedFlag("kit", {"kits"}), "kits");
  EXPECT_EQ(UnacceptedFlag("help", {"kits"}), "kits");
}

// --kits is the only spelling; --range is not a flag.
TEST(UnacceptedFlag, RangeIsNotAFlag) {
  EXPECT_EQ(UnacceptedFlag("setmode", {"range"}), "range");
  EXPECT_EQ(UnacceptedFlag("padlink", {"range"}), "range");
}

TEST(UnacceptedFlag, PortAndVersionAreUniversal) {
  EXPECT_EQ(UnacceptedFlag("help", {"port", "version"}), "");
  EXPECT_EQ(UnacceptedFlag("padlink", {"port"}), "");
}

TEST(UnacceptedFlag, ReportsTheFirstOffender) {
  EXPECT_EQ(UnacceptedFlag("help", {"port", "range", "pad"}), "range");
}

TEST(UnacceptedFlag, EmptyWhenNothingWasPassed) {
  EXPECT_EQ(UnacceptedFlag("help", {}), "");
}

TEST(AllowedFlags, UnknownCommandAllowsNothing) {
  EXPECT_TRUE(AllowedFlags("nonesuch").empty());
}

TEST(TakesPositionalNumber, TheKitWritesTakeNoneTheyUseKits) {
  for (const char* command :
       {"setmode", "assign", "setname", "setparams", "setlayer", "padlink"}) {
    EXPECT_FALSE(TakesPositionalNumber(command)) << command;
  }
}

TEST(TakesPositionalNumber, TrueForTheKitAndIndexCommands) {
  for (const char* command :
       {"kit", "readwave", "sendwave", "deletewave", "selectkit"}) {
    EXPECT_TRUE(TakesPositionalNumber(command)) << command;
  }
}

TEST(TakesPositionalNumber, FalseForCommandsWithoutOne) {
  for (const char* command : {"help", "info", "dump", "kits"}) {
    EXPECT_FALSE(TakesPositionalNumber(command)) << command;
  }
}

// A write with no kit named must not pick one.
TEST(RequiresKitSpec, TrueForEveryCommandThatWritesToKits) {
  for (const char* command :
       {"assign", "setname", "setparams", "setlayer", "setmode", "padlink"}) {
    EXPECT_TRUE(RequiresKitSpec(command)) << command;
  }
}

TEST(TakesSingleKit, TrueOnlyForTheOneKitWrites) {
  for (const char* command : {"assign", "setname", "setparams", "setlayer"}) {
    EXPECT_TRUE(TakesSingleKit(command)) << command;
  }
  EXPECT_FALSE(TakesSingleKit("setmode"));
  EXPECT_FALSE(TakesSingleKit("padlink"));
}

TEST(RequiresKitSpec, FalseForReadsAndPoolCommands) {
  for (const char* command :
       {"ping", "kits", "kit", "dump", "selectkit", "sendwave", "deletewave"}) {
    EXPECT_FALSE(RequiresKitSpec(command)) << command;
  }
}

TEST(FlagRejectionHint, SaysWhyRatherThanJustNo) {
  // A read-only command is not "about to write" — say the true reason.
  EXPECT_NE(FlagRejectionHint("info", "dry-run").find("only reads"),
            std::string::npos);
  EXPECT_NE(FlagRejectionHint("deletewave", "dry-run").find("go through"),
            std::string::npos);
  EXPECT_NE(FlagRejectionHint("selectkit", "commit").find("nothing to commit"),
            std::string::npos);
  EXPECT_FALSE(FlagRejectionHint("info", "sample").empty());
}

TEST(ParseKitSpec, ASingleKit) {
  std::vector<KitRange> ranges;
  std::string error;
  ASSERT_TRUE(ParseKitSpec("108", &ranges, &error)) << error;
  EXPECT_EQ(ranges, (std::vector<KitRange> {{108, 108}}));
}

TEST(ParseKitSpec, AnInclusiveRange) {
  std::vector<KitRange> ranges;
  std::string error;
  ASSERT_TRUE(ParseKitSpec("108-200", &ranges, &error)) << error;
  EXPECT_EQ(ranges, (std::vector<KitRange> {{108, 200}}));
  EXPECT_EQ(KitCount(ranges), 93);
}

TEST(ParseKitSpec, ACommaSeparatedMix) {
  std::vector<KitRange> ranges;
  std::string error;
  ASSERT_TRUE(ParseKitSpec("1,5,10-20", &ranges, &error)) << error;
  EXPECT_EQ(ranges, (std::vector<KitRange> {{1, 1}, {5, 5}, {10, 20}}));
  EXPECT_EQ(KitCount(ranges), 13);
}

TEST(ParseKitSpec, TheWholeDeviceIsSpeltOut) {
  std::vector<KitRange> ranges;
  std::string error;
  ASSERT_TRUE(ParseKitSpec("1-200", &ranges, &error)) << error;
  EXPECT_EQ(KitCount(ranges), 200);
}

TEST(KitCount, CountsOverlapsOnce) {
  EXPECT_EQ(KitCount({{1, 10}, {5, 15}}), 15);
  EXPECT_EQ(KitCount({{7, 7}, {7, 7}}), 1);
}

TEST(ParseKitSpec, RejectsJunk) {
  std::vector<KitRange> ranges;
  std::string error;
  for (const char* spec :
       {"", "abc", "12abc", "1-", "-5", "1,,2", "1-2-3", "1,"}) {
    EXPECT_FALSE(ParseKitSpec(spec, &ranges, &error))
        << "accepted \"" << spec << "\"";
    EXPECT_FALSE(error.empty());
    EXPECT_TRUE(ranges.empty());
  }
}

TEST(ParseKitSpec, RejectsOutOfRangeAndBackwards) {
  std::vector<KitRange> ranges;
  std::string error;
  EXPECT_FALSE(ParseKitSpec("0", &ranges, &error));
  EXPECT_FALSE(ParseKitSpec("201", &ranges, &error));
  EXPECT_FALSE(ParseKitSpec("1-201", &ranges, &error));
  EXPECT_FALSE(ParseKitSpec("20-10", &ranges, &error));
  EXPECT_NE(error.find("backwards"), std::string::npos);
}

TEST(ParseKitSpec, ErrorsSayWhatWasWrong) {
  std::vector<KitRange> ranges;
  std::string error;
  ParseKitSpec("999", &ranges, &error);
  EXPECT_NE(error.find("999"), std::string::npos);
}

// A name read as a number writes the wrong thing in silence: "SWITCH"
// through atoi is 0, which is MIX.
TEST(ParsePadParams, TakesModeAndCurveByName) {
  device::PadDeviceParams p;
  std::string error;
  ASSERT_TRUE(
      ParsePadParams("SWITCH,80,127,ON,LOUD2,127,80,25,75,OFF", &p, &error))
      << error;
  EXPECT_EQ(p.layer_mode, static_cast<uint8_t>(LayerMode::kSwitch));
  EXPECT_EQ(p.dynamics_curve, static_cast<uint8_t>(DynamicsCurve::kLoud2));
  EXPECT_EQ(p.dynamics, 1);
  EXPECT_EQ(p.trigger_reserve, 0);
  EXPECT_EQ(p.fade_point, 80);
  EXPECT_EQ(p.fade_end, 127);
}

// The ordinal behind a mode is an implementation detail; typing 4 for
// SWITCH is a coincidence waiting to break.
TEST(ParsePadParams, RefusesModeAndCurveAsNumbers) {
  device::PadDeviceParams p;
  std::string error;
  EXPECT_FALSE(
      ParsePadParams("4,80,127,ON,LINEAR,127,80,25,75,OFF", &p, &error));
  EXPECT_FALSE(ParsePadParams("MIX,80,127,ON,2,127,80,25,75,OFF", &p, &error));
}

TEST(ParsePadParams, RefusesTheSwitchesAsNumbers) {
  device::PadDeviceParams p;
  std::string error;
  EXPECT_FALSE(
      ParsePadParams("MIX,80,127,1,LINEAR,127,80,25,75,OFF", &p, &error));
  EXPECT_NE(error.find("dynamics"), std::string::npos) << error;
  EXPECT_FALSE(
      ParsePadParams("MIX,80,127,ON,LINEAR,127,80,25,75,0", &p, &error));
  EXPECT_NE(error.find("trigger reserve"), std::string::npos) << error;
}

TEST(ParsePadParams, TheErrorListsTheNamesItKnows) {
  device::PadDeviceParams p;
  std::string error;
  ParsePadParams("SWTICH,80,127,ON,LINEAR,127,80,25,75,OFF", &p, &error);
  EXPECT_NE(error.find("HI-HAT"), std::string::npos) << error;
  ParsePadParams("MIX,80,127,ON,LOWD1,127,80,25,75,OFF", &p, &error);
  EXPECT_NE(error.find("LOUD3"), std::string::npos) << error;
}

TEST(ParsePadParams, TakesHiHatByName) {
  device::PadDeviceParams p;
  std::string error;
  ASSERT_TRUE(
      ParsePadParams("HI-HAT,80,127,ON,LINEAR,127,80,25,75,ON", &p, &error))
      << error;
  EXPECT_EQ(p.layer_mode, static_cast<uint8_t>(LayerMode::kHiHat));
  EXPECT_EQ(p.trigger_reserve, 1);
}

TEST(ParsePadParams, RejectsAnUnknownName) {
  device::PadDeviceParams p;
  std::string error;
  EXPECT_FALSE(
      ParsePadParams("SWTICH,80,127,ON,LINEAR,127,80,25,75,OFF", &p, &error));
  EXPECT_NE(error.find("SWTICH"), std::string::npos) << error;
}

TEST(ParsePadParams, RejectsAValueOverOneTwentySeven) {
  device::PadDeviceParams p;
  std::string error;
  EXPECT_FALSE(
      ParsePadParams("MIX,200,127,ON,LINEAR,127,80,25,75,OFF", &p, &error));
  EXPECT_NE(error.find("fade point"), std::string::npos) << error;
}

TEST(ParsePadParams, RejectsJunkInANumericField) {
  device::PadDeviceParams p;
  std::string error;
  EXPECT_FALSE(
      ParsePadParams("MIX,8O,127,ON,LINEAR,127,80,25,75,OFF", &p, &error));
}

TEST(ParsePadParams, RejectsTheWrongCount) {
  device::PadDeviceParams p;
  std::string error;
  EXPECT_FALSE(ParsePadParams("MIX,80,127", &p, &error));
  EXPECT_NE(error.find("10"), std::string::npos) << error;
}

TEST(ParsePadParams, RejectsANonsenseFlag) {
  device::PadDeviceParams p;
  std::string error;
  EXPECT_FALSE(
      ParsePadParams("MIX,80,127,YES,LINEAR,127,80,25,75,OFF", &p, &error));
  EXPECT_NE(error.find("dynamics"), std::string::npos) << error;
}

}  // namespace
}  // namespace spdsx::spdutil
