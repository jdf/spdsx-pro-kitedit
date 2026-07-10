// Application command IDs, shared by menus and command targets.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace spdsx::commands {

enum : juce::CommandID {
  undo = 1,
  redo,
  file_new,
  file_open,
  file_save,
  file_save_as,
};

}  // namespace spdsx::commands
