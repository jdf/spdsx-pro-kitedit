// A modal, non-dismissable progress panel for the long serial operations
// (Load Device State, and eventually the sync push): a megabyte-scale bulk
// read over a 230400-baud link takes a minute or two, and a greyed menu
// item plus a status line reads as a frozen window. This blocks input
// while it runs and shows an animated bar plus a live detail line.
//
// Indeterminate by default (the total block count isn't known up front);
// SetProgress with a 0..1 fraction switches it to a determinate bar. It is
// updated only from the message thread. There is deliberately no Cancel:
// the worker owns the port mid-dump and can't be interrupted safely.
#ifndef SPDSX_PATCHEDIT_SOURCE_PROGRESS_DIALOG_H_
#define SPDSX_PATCHEDIT_SOURCE_PROGRESS_DIALOG_H_

#include <juce_gui_basics/juce_gui_basics.h>

namespace spdsx {

class ProgressDialog : public juce::Component {
public:
  explicit ProgressDialog(const juce::String& message);

  void SetMessage(const juce::String& message);
  // fraction < 0 keeps the bar indeterminate; 0..1 makes it determinate.
  void SetProgress(double fraction);

  void resized() override;

  // Launches the panel in a modal DialogWindow (native title bar, no close
  // button, escape disabled) and returns it. The window owns a fresh
  // ProgressDialog, reachable through `dialog_out`; both are destroyed when
  // the caller ends the modal state (DialogWindow::exitModalState). Hold
  // the returned pointers in SafePointers.
  static juce::DialogWindow* Show(const juce::String& title,
                                  const juce::String& message,
                                  ProgressDialog** dialog_out);

private:
  double value_ = -1.0;  // negative = indeterminate animation
  juce::Label message_;
  juce::ProgressBar bar_ {value_};
};

}  // namespace spdsx

#endif  // SPDSX_PATCHEDIT_SOURCE_PROGRESS_DIALOG_H_
