#ifndef SPDSX_PATCHEDIT_TESTING_TEMP_DIR_H_
#define SPDSX_PATCHEDIT_TESTING_TEMP_DIR_H_

#include <juce_core/juce_core.h>

namespace spdsx_testing {

// RAII scratch directory under the system temp location, deleted with all its
// contents on destruction. Handy for DeviceDb / DeviceDocument tests that need
// a real *.spdsx file on disk.
class TempDir {
public:
  TempDir() {
    dir_ = juce::File::getSpecialLocation(juce::File::tempDirectory)
               .getChildFile("spdsx-test-" + juce::Uuid().toString());
    dir_.createDirectory();
  }

  ~TempDir() { dir_.deleteRecursively(); }

  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;

  const juce::File& dir() const { return dir_; }

  juce::File file(const juce::String& name) const {
    return dir_.getChildFile(name);
  }

private:
  juce::File dir_;
};

}  // namespace spdsx_testing

#endif  // SPDSX_PATCHEDIT_TESTING_TEMP_DIR_H_
