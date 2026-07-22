// Builds the feedback report the app sends to the report relay (see
// report-relay/): the human-readable environment block the dialog shows
// the user before sending, and the JSON body the relay turns into a
// GitHub issue. Pure functions, so what gets attached is testable.
#ifndef SPDSX_PATCHEDIT_SOURCE_BUG_REPORT_H_
#define SPDSX_PATCHEDIT_SOURCE_BUG_REPORT_H_

#include <juce_core/juce_core.h>

namespace spdsx {

// Everything a report carries. `category` is one of "bug", "feature",
// "feedback" (the relay maps it to an issue label); `text` is the user's
// own words; the rest is collected by the app.
struct BugReport {
  juce::String category;
  juce::String text;
  juce::String app_version;
  juce::String os;
  juce::String device;  // e.g. "connected, firmware 2.00 (0094)"
  juce::String document;  // e.g. "schema v2"
  juce::String log;  // AppLog::Recent()
};

// The environment block, one "key: value" per line — shown verbatim in
// the dialog ("what's attached") and embedded in the issue.
juce::String EnvironmentReport(const BugReport& report);

// The POST body for the relay: category/text/environment/log as JSON.
juce::String ReportJson(const BugReport& report);

}  // namespace spdsx

#endif  // SPDSX_PATCHEDIT_SOURCE_BUG_REPORT_H_
