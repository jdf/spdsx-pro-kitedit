// The Bulk Edit tab: builds one of spdutil's operations with real
// controls and applies it to the DOCUMENT — never the device. The edited
// kits read dirty against the base snapshot like any other edit, and
// Sync Changes with Device pushes them.
//
// Left is a list of operations; choosing one swaps the controls on the
// right. Above the buttons it shows the spdutil command line the
// controls describe, rendered from the very request Apply will run.
#ifndef SPDSX_PATCHEDIT_SOURCE_BULK_EDIT_PANEL_H_
#define SPDSX_PATCHEDIT_SOURCE_BULK_EDIT_PANEL_H_

#include <functional>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "device_ops.h"

namespace spdsx {

// One operation's controls. A page owns its inputs, says what is wrong
// with them, renders the equivalent command line, and applies itself
// through the handler its owner gave it.
class BulkEditPage : public juce::Component {
public:
  ~BulkEditPage() override = default;

  // Called whenever a control changes, so the panel can refresh the
  // command-line preview and the buttons.
  std::function<void()> on_changed;

  // Empty when the controls make a runnable request; otherwise why not,
  // in a sentence to show the user.
  virtual juce::String Problem() const = 0;

  // The spdutil command line these controls describe.
  virtual juce::String CommandLine() const = 0;

  // Applies to the document as one undoable transaction. Returns the
  // line to show. Runs on the message thread — document edits are
  // instant.
  virtual juce::String Apply() = 0;
};

class BulkEditPanel
    : public juce::Component
    , private juce::ListBoxModel {
public:
  // How the pages reach the document; provided by the owner.
  struct Handlers {
    std::function<juce::String(const ops::SetModeRequest&)> set_mode;
  };

  explicit BulkEditPanel(Handlers handlers);
  ~BulkEditPanel() override;

  void paint(juce::Graphics& g) override;
  void resized() override;

private:
  struct Operation {
    juce::String name;
    juce::String blurb;
    std::unique_ptr<BulkEditPage> page;
  };

  int getNumRows() override;
  void paintListBoxItem(int row,
                        juce::Graphics& g,
                        int width,
                        int height,
                        bool selected) override;
  void selectedRowsChanged(int row) override;

  void ShowOperation(int index);
  void RefreshPreview();
  void Run();

  // The window-sized text this panel uses; see the LookAndFeel note in
  // the implementation.
  std::unique_ptr<juce::LookAndFeel> look_and_feel_;

  std::vector<Operation> operations_;
  int current_ = -1;

  juce::ListBox nav_;
  juce::Label blurb_;
  juce::Label preview_;  // the equivalent spdutil command line
  juce::TextButton apply_button_ {"Apply"};
  juce::Label status_;
};

}  // namespace spdsx

#endif  // SPDSX_PATCHEDIT_SOURCE_BULK_EDIT_PANEL_H_
