#include "spdutil_args.h"

#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace spdsx::spdutil {
namespace {

TEST(SpdutilCommands, KnowsTheRealOnes) {
  EXPECT_TRUE(IsCommand("ping"));
  EXPECT_TRUE(IsCommand("setmode"));
  EXPECT_TRUE(IsCommand("padlink"));
  EXPECT_FALSE(IsCommand("pnig"));
  EXPECT_FALSE(IsCommand(""));
}

TEST(SpdutilCommands, SuggestsANearMiss) {
  EXPECT_EQ(NearestCommand("pnig"), "ping");
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
  EXPECT_EQ(UnacceptedFlag("ping", {"pad"}), "pad");
}

// A rehearsal that writes is worse than no rehearsal.
TEST(UnacceptedFlag, DryRunIsAcceptedByTheWriteCommandsThatHonorIt) {
  for (const char* command :
       {"assign", "setname", "setparams", "setlayer", "setmode", "padlink"}) {
    EXPECT_EQ(UnacceptedFlag(command, {"dry-run"}), "") << command;
  }
}

TEST(UnacceptedFlag, DryRunIsRejectedWhereItCannotWork) {
  for (const char* command : {"ping", "dump", "readwave", "deletewave"}) {
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
  EXPECT_EQ(UnacceptedFlag("ping", {"kits"}), "kits");
}

// --kits is the only spelling; --range is not a flag.
TEST(UnacceptedFlag, RangeIsNotAFlag) {
  EXPECT_EQ(UnacceptedFlag("setmode", {"range"}), "range");
  EXPECT_EQ(UnacceptedFlag("padlink", {"range"}), "range");
}

TEST(UnacceptedFlag, PortAndVersionAreUniversal) {
  EXPECT_EQ(UnacceptedFlag("ping", {"port", "version"}), "");
  EXPECT_EQ(UnacceptedFlag("padlink", {"port"}), "");
}

TEST(UnacceptedFlag, ReportsTheFirstOffender) {
  EXPECT_EQ(UnacceptedFlag("ping", {"port", "range", "pad"}), "range");
}

TEST(UnacceptedFlag, EmptyWhenNothingWasPassed) {
  EXPECT_EQ(UnacceptedFlag("ping", {}), "");
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
  for (const char* command : {"ping", "info", "dump", "kits"}) {
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
  EXPECT_NE(FlagRejectionHint("ping", "dry-run").find("only reads"),
            std::string::npos);
  EXPECT_NE(FlagRejectionHint("deletewave", "dry-run").find("go through"),
            std::string::npos);
  EXPECT_NE(FlagRejectionHint("selectkit", "commit").find("nothing to commit"),
            std::string::npos);
  EXPECT_FALSE(FlagRejectionHint("ping", "sample").empty());
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

}  // namespace
}  // namespace spdsx::spdutil
