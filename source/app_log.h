// A small in-memory log of recent app and device events, kept so a bug
// report can carry what just happened without any logging infrastructure:
// no files, no levels, just the last few hundred timestamped lines in a
// ring. Threads may Note() freely (device work runs on workers).
#ifndef SPDSX_PATCHEDIT_SOURCE_APP_LOG_H_
#define SPDSX_PATCHEDIT_SOURCE_APP_LOG_H_

#include <juce_core/juce_core.h>

namespace spdsx {

class AppLog {
public:
  // How many lines the ring keeps; older lines fall off the front.
  static constexpr int kMaxLines = 200;

  // Appends one line, stamped with seconds since the first Note.
  static void Note(const juce::String& line);

  // The retained lines, oldest first, newline-joined.
  static juce::String Recent();

  static void Clear();
};

}  // namespace spdsx

#endif  // SPDSX_PATCHEDIT_SOURCE_APP_LOG_H_
