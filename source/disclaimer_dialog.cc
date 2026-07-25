#include "disclaimer_dialog.h"

#include <memory>
#include <utility>

namespace spdsx {

DisclaimerPanel::DisclaimerPanel() {
  text_.setText(
      juce::String::fromUTF8(
          "This software is provided as is, without warranty of any kind "
          "(see the MIT and AGPL license texts). It uses an unofficial, "
          "reverse-engineered protocol and can write to your SPD-SX PRO’s "
          "memory — back up your device before syncing.\n\n"
          "By continuing, you accept this risk."),
      juce::dontSendNotification);
  text_.setJustificationType(juce::Justification::topLeft);
  addAndMakeVisible(text_);

  accept_.onClick = [this] { Finish(true); };
  addAndMakeVisible(accept_);
  quit_.onClick = [this] { Finish(false); };
  addAndMakeVisible(quit_);
}

DisclaimerPanel::~DisclaimerPanel() {
  // Escape or the window's close button: not an acceptance.
  if (on_result_ != nullptr) {
    std::exchange(on_result_, nullptr)(false);
  }
}

void DisclaimerPanel::Finish(bool accepted) {
  if (auto* dialog = findParentComponentOfClass<juce::DialogWindow>()) {
    dialog->exitModalState(0);
  }
  if (on_result_ != nullptr) {
    std::exchange(on_result_, nullptr)(accepted);
  }
}

void DisclaimerPanel::resized() {
  auto area = getLocalBounds().reduced(20);
  auto buttons = area.removeFromBottom(32);
  accept_.setBounds(buttons.removeFromRight(150));
  buttons.removeFromRight(8);
  quit_.setBounds(buttons.removeFromRight(90));
  text_.setBounds(area.withTrimmedBottom(12));
}

void DisclaimerPanel::Show(std::function<void(bool accepted)> on_result) {
  auto panel = std::make_unique<DisclaimerPanel>();
  panel->on_result_ = std::move(on_result);
  panel->setSize(470, 190);

  juce::DialogWindow::LaunchOptions options;
  options.content.setOwned(panel.release());
  options.dialogTitle = "Use at Your Own Risk";
  options.dialogBackgroundColour = juce::Colour(0xff2b2b2b);
  options.escapeKeyTriggersCloseButton = true;
  options.useNativeTitleBar = true;
  options.resizable = false;
  options.launchAsync();
}

}  // namespace spdsx
