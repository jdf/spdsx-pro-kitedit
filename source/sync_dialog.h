// The sync conflict-resolution dialog: one row per conflicted item (a
// pad, or a kit name). Rows are selected with checkboxes and answered in
// bulk — keep mine, keep the device's, or do nothing — because a sync
// that conflicts on one pad usually conflicts on many, and the same
// answer applies to all of them. "Do nothing" leaves both sides as they
// are and the item re-flags on the next sync. Pure view: Show() launches
// it and reports the choices through the callbacks; closing the window
// any other way (escape, the close button) cancels.
#ifndef SPDSX_PATCHEDIT_SOURCE_SYNC_DIALOG_H_
#define SPDSX_PATCHEDIT_SOURCE_SYNC_DIALOG_H_

#include <functional>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "device_sync.h"

namespace spdsx {

// A checkbox with the third state a select-all needs: some but not all of
// what it governs is checked. JUCE has no such button, so the mixed state
// paints a dash over the (unticked) box.
class TriStateToggle : public juce::ToggleButton {
public:
  using juce::ToggleButton::ToggleButton;

  void SetMixed(bool mixed);

  bool mixed() const { return mixed_; }

  void paintButton(juce::Graphics& g,
                   bool should_draw_highlighted,
                   bool should_draw_down) override;

private:
  bool mixed_ = false;
};

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
    juce::ToggleButton check;
    juce::Label label;
    juce::Label outcome;  // what this row will do, in words
    SyncResolution resolution = SyncResolution::kMine;
  };

  // Sets every checked row's resolution and redraws their outcomes.
  void ApplyToChecked(SyncResolution resolution);
  // Checks or clears every row.
  void CheckAll(bool checked);
  // Re-reads the checkboxes into the select-all's state, and enables the
  // bulk buttons only when they would do something.
  void RefreshSelection();
  void ShowOutcome(Row& row);

  std::vector<SyncResolution> Resolutions() const;
  void CloseDialog();

  juce::Label heading_;
  TriStateToggle select_all_ {"Select all"};
  juce::TextButton keep_mine_ {"Keep Mine"};
  juce::TextButton keep_theirs_ {"Keep Device's"};
  juce::TextButton keep_neither_ {"Do Nothing"};
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
