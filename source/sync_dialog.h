// The sync conflict-resolution dialog: one row per conflicted item (a
// pad, or a kit name), each with a real choice control — my copy wins /
// device wins / do nothing. "Do nothing" leaves both sides as they are
// and the item re-flags on the next sync. Pure view: Show() launches it
// and reports the choices through the callbacks; closing the window any
// other way (escape, the close button) cancels.
#ifndef SPDSX_PATCHEDIT_SOURCE_SYNC_DIALOG_H_
#define SPDSX_PATCHEDIT_SOURCE_SYNC_DIALOG_H_

#include <functional>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "device_sync.h"

namespace spdsx {

class SyncConflictPanel : public juce::Component {
public:
  explicit SyncConflictPanel(std::vector<SyncConflict> conflicts);
  ~SyncConflictPanel() override;

  // Fired with one resolution per conflict, in the order given, when the
  // user chooses Sync. Exactly one of the two callbacks fires.
  std::function<void(std::vector<SyncResolution>)> on_apply;
  std::function<void()> on_cancel;

  void resized() override;

  // Builds the panel, wires the callbacks, and launches it in an async
  // modal dialog window.
  static void Show(std::vector<SyncConflict> conflicts,
                   std::function<void(std::vector<SyncResolution>)> on_apply,
                   std::function<void()> on_cancel);

private:
  struct Row {
    juce::Label label;
    juce::ComboBox choice;
  };

  std::vector<SyncResolution> Resolutions() const;
  void CloseDialog();

  juce::Label heading_;
  juce::Viewport viewport_;
  juce::Component row_holder_;
  std::vector<std::unique_ptr<Row>> rows_;
  juce::TextButton apply_ {"Sync"};
  juce::TextButton cancel_ {"Cancel"};
  // Set once a button decided the outcome; a destructor without it (the
  // window's close button, escape) reports cancel.
  bool decided_ = false;
};

}  // namespace spdsx

#endif  // SPDSX_PATCHEDIT_SOURCE_SYNC_DIALOG_H_
