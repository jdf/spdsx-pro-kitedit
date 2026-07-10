// Application command IDs, shared by menus and command targets.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace spdsx::commands {

enum : juce::CommandID {
  kUndo = 1,
  kRedo,
  kFileNew,
  kFileOpen,
  kFileSave,
  kFileSaveAs,
  kToggleBrowser,
};

}  // namespace spdsx::commands
