// The bulk-operations window: a front end for the same device operations
// spdutil runs, for the jobs that span many kits and are miserable to do
// a pad at a time.
//
// Left is a list of modes, one per spdutil command; choosing one swaps
// the controls on the right. Above the buttons it shows the spdutil
// command line the controls describe — rendered from the very request it
// will run (ops::CommandLine), so it is a statement about what will
// happen rather than a guess. Nothing here shells out: the operation runs
// in-process on a worker thread, which is what allows a progress bar and
// an Abort.
#ifndef SPDSX_PATCHEDIT_SOURCE_BULK_OPS_WINDOW_H_
#define SPDSX_PATCHEDIT_SOURCE_BULK_OPS_WINDOW_H_

#include <atomic>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "connection_dot.h"
#include "device_ops.h"

namespace spdsx {

// One mode's controls. A page owns its inputs, says what is wrong with
// them, renders the equivalent command line, and runs itself against a
// device on a worker thread.
class BulkOpPage : public juce::Component {
public:
  ~BulkOpPage() override = default;

  // Called whenever a control changes, so the window can refresh the
  // command-line preview and the Run buttons.
  std::function<void()> on_changed;

  // Empty when the controls make a runnable request; otherwise why not,
  // in a sentence to show the user.
  virtual juce::String Problem() const = 0;

  // False for read-only modes, where a dry run would rehearse nothing;
  // the panel hides the Dry Run button for them.
  virtual bool CanDryRun() const { return true; }

  // The spdutil command line these controls describe. dry_run picks
  // which of the two buttons is being previewed.
  virtual juce::String CommandLine(bool dry_run) const = 0;

  // Runs on a worker thread. Returns the line to show when it finishes.
  virtual juce::String Run(device::SpdsxDevice& dev,
                           bool dry_run,
                           const ops::ProgressFn& progress,
                           const ops::AbortFn& should_abort) = 0;
};

class BulkOpsPanel
    : public juce::Component
    , private juce::ListBoxModel {
public:
  BulkOpsPanel();
  ~BulkOpsPanel() override;

  void paint(juce::Graphics& g) override;
  void resized() override;

  // Fed by the main window's connection poller (the one place that opens
  // the port for status). The Run buttons stay disabled while no device
  // is present — an operation that cannot start should not look like it
  // could.
  void SetConnection(bool connected, const juce::String& text);

private:
  struct Mode {
    juce::String name;
    juce::String blurb;
    std::unique_ptr<BulkOpPage> page;
  };

  int getNumRows() override;
  void paintListBoxItem(int row,
                        juce::Graphics& g,
                        int width,
                        int height,
                        bool selected) override;
  void selectedRowsChanged(int row) override;

  void ShowMode(int index);
  void RefreshPreview();
  void Start(bool dry_run);
  void Finish(const juce::String& outcome, const juce::String& error);

  std::vector<Mode> modes_;
  int current_ = -1;

  // The window's own LookAndFeel: the same theme, at roughly twice the
  // default text size (this is a utility window read at arm's length).
  std::unique_ptr<juce::LookAndFeel> look_and_feel_;

  juce::ListBox nav_;
  juce::Label blurb_;
  juce::Label preview_;  // the equivalent spdutil command line
  juce::TextButton dry_run_ {"Dry Run"};
  juce::TextButton run_ {"Run"};
  juce::TextButton abort_ {"Abort"};
  juce::ProgressBar progress_bar_ {progress_};
  juce::Label status_;

  ConnectionDot connection_dot_;
  juce::Label connection_text_;

  double progress_ = 0.0;
  bool running_ = false;
  bool device_connected_ = false;
  std::shared_ptr<std::atomic<bool>> abort_flag_;

  JUCE_DECLARE_WEAK_REFERENCEABLE(BulkOpsPanel)
};

// The window itself. One at a time: Show() fronts the open one.
class BulkOpsWindow : public juce::DocumentWindow {
public:
  BulkOpsWindow();
  void closeButtonPressed() override;

  // Forwarded to the panel; see BulkOpsPanel::SetConnection.
  void SetConnection(bool connected, const juce::String& text);

private:
  BulkOpsPanel* panel_ = nullptr;  // owned by the window as its content
};

}  // namespace spdsx

#endif  // SPDSX_PATCHEDIT_SOURCE_BULK_OPS_WINDOW_H_
