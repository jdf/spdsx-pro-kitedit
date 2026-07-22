#include "bug_report.h"

#include <gtest/gtest.h>

namespace spdsx {
namespace {

BugReport SampleReport() {
  BugReport r;
  r.category = "bug";
  r.text = "the pad plays the wrong sample";
  r.app_version = "0.1.0";
  r.os = "macOS 15.5 (arm64)";
  r.device = "connected, firmware 2.00 (0094)";
  r.document = "schema v2";
  r.log = "    0.0  opened the port\n    1.2  sync started\n";
  return r;
}

TEST(EnvironmentReport, CarriesEveryFieldAsKeyValueLines) {
  const juce::String env = EnvironmentReport(SampleReport());
  EXPECT_TRUE(env.contains("app: spdsx-patchedit 0.1.0"));
  EXPECT_TRUE(env.contains("os: macOS 15.5 (arm64)"));
  EXPECT_TRUE(env.contains("device: connected, firmware 2.00 (0094)"));
  EXPECT_TRUE(env.contains("document: schema v2"));
}

TEST(ReportJson, RoundTripsThroughAJsonParser) {
  const juce::var parsed = juce::JSON::parse(ReportJson(SampleReport()));
  ASSERT_TRUE(parsed.isObject());
  EXPECT_EQ(parsed["category"].toString(), "bug");
  EXPECT_EQ(parsed["text"].toString(), "the pad plays the wrong sample");
  EXPECT_TRUE(parsed["environment"].toString().contains("os: macOS"));
  EXPECT_TRUE(parsed["log"].toString().contains("sync started"));
}

// The user's words can hold anything — quotes, newlines, emoji — and must
// survive the JSON encoding untouched.
TEST(ReportJson, SurvivesHostileText) {
  BugReport r = SampleReport();
  r.text = "she said \"boom\"\nthen \\n literally, and 🎛️";
  const juce::var parsed = juce::JSON::parse(ReportJson(r));
  EXPECT_EQ(parsed["text"].toString(), r.text);
}

}  // namespace
}  // namespace spdsx
