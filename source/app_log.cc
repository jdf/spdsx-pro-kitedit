#include "app_log.h"

#include <deque>
#include <mutex>

namespace spdsx {

namespace {

std::mutex& Mutex() {
  static std::mutex m;
  return m;
}

std::deque<juce::String>& Lines() {
  static std::deque<juce::String> lines;
  return lines;
}

// The zero point is the first Note, so stamps read as "time into the
// session" — what a bug report wants.
juce::uint32& Epoch() {
  static juce::uint32 epoch = 0;
  return epoch;
}

}  // namespace

void AppLog::Note(const juce::String& line) {
  const std::lock_guard<std::mutex> hold(Mutex());
  const juce::uint32 now = juce::Time::getMillisecondCounter();
  if (Epoch() == 0) {
    Epoch() = now;
  }
  const double seconds = (now - Epoch()) / 1000.0;
  Lines().push_back(juce::String(seconds, 1).paddedLeft(' ', 7) + "  " + line);
  while (Lines().size() > static_cast<size_t>(kMaxLines)) {
    Lines().pop_front();
  }
}

juce::String AppLog::Recent() {
  const std::lock_guard<std::mutex> hold(Mutex());
  juce::String out;
  for (const juce::String& line : Lines()) {
    out << line << "\n";
  }
  return out;
}

void AppLog::Clear() {
  const std::lock_guard<std::mutex> hold(Mutex());
  Lines().clear();
  Epoch() = 0;
}

}  // namespace spdsx
