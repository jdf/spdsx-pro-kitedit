// Application command IDs, shared by menus and command targets.
#ifndef SPDSX_PATCHEDIT_SOURCE_COMMANDS_H_
#define SPDSX_PATCHEDIT_SOURCE_COMMANDS_H_

#include <juce_gui_basics/juce_gui_basics.h>

namespace spdsx::commands {

enum : juce::CommandID {
  kUndo = 1,
  kRedo,
  kFileNew,
  kFileOpen,
  // No plain Save: every mutation autosaves. Save As relocates the
  // document (and future autosaves with it).
  kFileSaveAs,
  kImportKit,
  kLoadDeviceSamples,
  kToggleBrowser,
  kToggleAutoplay,
};

}  // namespace spdsx::commands

#endif  // SPDSX_PATCHEDIT_SOURCE_COMMANDS_H_
