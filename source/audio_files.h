// What counts as a loadable audio file, shared by the drop targets, the
// sample browser's filter, and drag payload identification.
#ifndef SPDSX_PATCHEDIT_SOURCE_AUDIO_FILES_H_
#define SPDSX_PATCHEDIT_SOURCE_AUDIO_FILES_H_

#include <juce_core/juce_core.h>

namespace spdsx {

inline constexpr const char* kAudioFileWildcard =
    "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3";

// Drag-and-drop description for samples dragged from the browser panel.
inline constexpr const char* kSampleDragId = "sample-browser-file";
// Drag-and-drop description prefix for device pool waves dragged from
// the device samples panel: "spdsx-devsample:<pool index>".
inline constexpr const char* kDeviceSampleDragPrefix = "spdsx-devsample:";

inline bool LooksLikeAudio(const juce::String& path) {
  static const juce::StringArray kExtensions {
      ".wav", ".aif", ".aiff", ".flac", ".ogg", ".mp3"};
  for (const auto& ext : kExtensions) {
    if (path.endsWithIgnoreCase(ext)) {
      return true;
    }
  }
  return false;
}

}  // namespace spdsx

#endif  // SPDSX_PATCHEDIT_SOURCE_AUDIO_FILES_H_
