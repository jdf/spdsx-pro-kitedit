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

// The bug this table exists to prevent: --pad parsed fine for setmode and
// was dropped, so a sweep meant for one pad hit all nine.
TEST(UnacceptedFlag, SetmodeTakesThePadFilter) {
  EXPECT_EQ(UnacceptedFlag("setmode", {"mode", "pad", "range"}), "");
}

TEST(UnacceptedFlag, PingTakesNoPad) {
  EXPECT_EQ(UnacceptedFlag("ping", {"pad"}), "pad");
}

// --dry-run used to parse and be ignored by every write command but
// setmode and padlink, so rehearsing a write performed it.
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

TEST(UnacceptedFlag, RangeBelongsToSweepsOnly) {
  EXPECT_EQ(UnacceptedFlag("setmode", {"range"}), "");
  EXPECT_EQ(UnacceptedFlag("padlink", {"range"}), "");
  EXPECT_EQ(UnacceptedFlag("setname", {"range"}), "range");
  EXPECT_EQ(UnacceptedFlag("kit", {"range"}), "range");
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

// setmode ignored a positional kit and swept all 200 kits instead.
TEST(TakesPositionalNumber, SetmodeAcceptsASingleKit) {
  EXPECT_TRUE(TakesPositionalNumber("setmode"));
}

TEST(TakesPositionalNumber, TrueForTheKitAndIndexCommands) {
  for (const char* command : {"kit",
                              "readwave",
                              "sendwave",
                              "deletewave",
                              "selectkit",
                              "assign",
                              "setname",
                              "setparams",
                              "setlayer"}) {
    EXPECT_TRUE(TakesPositionalNumber(command)) << command;
  }
}

TEST(TakesPositionalNumber, FalseForCommandsWithoutOne) {
  for (const char* command : {"ping", "info", "dump", "kits", "padlink"}) {
    EXPECT_FALSE(TakesPositionalNumber(command)) << command;
  }
}

// Omitting the kit used to mean kit 1 for the single-kit writes and
// every kit for the sweeps — a forgotten argument wrote unasked.
TEST(RequiresExplicitKit, TrueForEveryCommandThatWritesToKits) {
  for (const char* command :
       {"assign", "setname", "setparams", "setlayer", "setmode", "padlink"}) {
    EXPECT_TRUE(RequiresExplicitKit(command)) << command;
  }
}

TEST(RequiresExplicitKit, FalseForReadsAndPoolCommands) {
  for (const char* command :
       {"ping", "kits", "kit", "dump", "selectkit", "sendwave", "deletewave"}) {
    EXPECT_FALSE(RequiresExplicitKit(command)) << command;
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

}  // namespace
}  // namespace spdsx::spdutil
