#include "bug_report.h"

namespace spdsx {

juce::String EnvironmentReport(const BugReport& report) {
  juce::String out;
  out << "app: spdsx-patchedit " << report.app_version << "\n"
      << "os: " << report.os << "\n"
      << "device: " << report.device << "\n"
      << "document: " << report.document << "\n";
  return out;
}

juce::String ReportJson(const BugReport& report) {
  auto* obj = new juce::DynamicObject();
  obj->setProperty("category", report.category);
  obj->setProperty("text", report.text);
  obj->setProperty("environment", EnvironmentReport(report));
  obj->setProperty("log", report.log);
  return juce::JSON::toString(juce::var(obj), /*allOnOneLine=*/true);
}

}  // namespace spdsx
