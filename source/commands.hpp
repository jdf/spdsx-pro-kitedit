// Application command IDs, shared by menus and command targets.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace spdsx::commands {

enum : juce::CommandID {
  undo = 1,
  redo,
};

}  // namespace spdsx::commands
