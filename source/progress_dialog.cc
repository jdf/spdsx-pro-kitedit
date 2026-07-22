#include "progress_dialog.h"

namespace spdsx {

ProgressDialog::ProgressDialog(const juce::String& message) {
  message_.setText(message, juce::dontSendNotification);
  message_.setJustificationType(juce::Justification::centredTop);
  message_.setMinimumHorizontalScale(1.0f);
  addAndMakeVisible(message_);
  bar_.setPercentageDisplay(false);
  addAndMakeVisible(bar_);
  setSize(420, 132);
}

void ProgressDialog::SetMessage(const juce::String& message) {
  message_.setText(message, juce::dontSendNotification);
}

void ProgressDialog::SetProgress(double fraction) {
  value_ = fraction;
  bar_.setPercentageDisplay(fraction >= 0.0);
}

void ProgressDialog::resized() {
  auto area = getLocalBounds().reduced(20);
  bar_.setBounds(area.removeFromBottom(22));
  area.removeFromBottom(14);
  message_.setBounds(area);
}

juce::DialogWindow* ProgressDialog::Show(const juce::String& title,
                                         const juce::String& message,
                                         ProgressDialog** dialog_out) {
  auto panel = std::make_unique<ProgressDialog>(message);
  if (dialog_out != nullptr) {
    *dialog_out = panel.get();
  }
  juce::DialogWindow::LaunchOptions options;
  options.content.setOwned(panel.release());
  options.dialogTitle = title;
  options.dialogBackgroundColour = juce::Colour(0xff2b2b2b);
  options.escapeKeyTriggersCloseButton = false;
  options.useNativeTitleBar = true;
  options.resizable = false;
  // No native close button: the operation owns the serial port and can't
  // be interrupted, so the dialog is closed programmatically when it ends.
  return options.launchAsync();
}

}  // namespace spdsx
