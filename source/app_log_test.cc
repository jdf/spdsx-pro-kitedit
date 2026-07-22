#include "app_log.h"

#include <thread>
#include <vector>

#include <gtest/gtest.h>

namespace spdsx {
namespace {

class AppLogTest : public ::testing::Test {
protected:
  void SetUp() override { AppLog::Clear(); }
  void TearDown() override { AppLog::Clear(); }
};

TEST_F(AppLogTest, KeepsNotesInOrderWithTimestamps) {
  AppLog::Note("opened the port");
  AppLog::Note("sync started");

  const juce::String log = AppLog::Recent();
  EXPECT_TRUE(log.contains("opened the port"));
  EXPECT_TRUE(log.contains("sync started"));
  EXPECT_LT(log.indexOf("opened the port"), log.indexOf("sync started"));
  // Each line leads with a seconds-into-session stamp.
  EXPECT_TRUE(log.startsWith("    0.0  "));
}

TEST_F(AppLogTest, DropsTheOldestLinesPastTheCap) {
  for (int i = 0; i < AppLog::kMaxLines + 10; ++i) {
    AppLog::Note("line " + juce::String(i));
  }

  const juce::String log = AppLog::Recent();
  EXPECT_FALSE(log.contains("line 9\n"));  // fell off the front
  EXPECT_TRUE(log.contains("line 10\n"));
  EXPECT_TRUE(
      log.contains("line " + juce::String(AppLog::kMaxLines + 9) + "\n"));
}

TEST_F(AppLogTest, ClearEmptiesTheRing) {
  AppLog::Note("something");
  AppLog::Clear();
  EXPECT_TRUE(AppLog::Recent().isEmpty());
}

// Note() is called from device worker threads while the message thread
// reads; this just has to not tear or crash under a thread sanitizer.
TEST_F(AppLogTest, TakesNotesFromSeveralThreads) {
  std::vector<std::thread> writers;
  for (int t = 0; t < 4; ++t) {
    writers.emplace_back([t] {
      for (int i = 0; i < 100; ++i) {
        AppLog::Note("t" + juce::String(t) + " " + juce::String(i));
      }
    });
  }
  for (auto& w : writers) {
    w.join();
  }
  EXPECT_FALSE(AppLog::Recent().isEmpty());
}

}  // namespace
}  // namespace spdsx
