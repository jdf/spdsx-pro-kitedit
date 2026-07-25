// First-launch clickwrap: the as-is / reverse-engineered-protocol risk
// acknowledgment. Accept continues (the caller records acceptance); any
// other close path — Quit, the close button, escape — reports decline via
// the destructor, and the caller quits the app. Pure view.
#ifndef SPDSX_PATCHEDIT_SOURCE_DISCLAIMER_DIALOG_H_
#define SPDSX_PATCHEDIT_SOURCE_DISCLAIMER_DIALOG_H_

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

namespace spdsx {

// Bump when the disclaimer text changes materially; everyone re-accepts.
inline constexpr int kDisclaimerVersion = 1;
inline constexpr const char* kDisclaimerAcceptedKey =
    "disclaimerAcceptedVersion";

class DisclaimerPanel : public juce::Component {
public:
  DisclaimerPanel();
  ~DisclaimerPanel() override;

  void resized() override;

  // Opens the modal dialog. on_result fires exactly once.
  static void Show(std::function<void(bool accepted)> on_result);

private:
  void Finish(bool accepted);

  std::function<void(bool)> on_result_;
  juce::Label text_;
  juce::TextButton accept_ {"I Accept the Risk"};
  juce::TextButton quit_ {"Quit"};
};

}  // namespace spdsx

#endif  // SPDSX_PATCHEDIT_SOURCE_DISCLAIMER_DIALOG_H_
