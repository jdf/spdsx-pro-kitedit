#include "sync_dialog.h"

namespace spdsx {

namespace {

constexpr int kRowHeight = 52;
constexpr int kChoiceWidth = 150;

// ComboBox ids (0 is reserved for "nothing selected").
constexpr int kIdMine = 1;
constexpr int kIdTheirs = 2;
constexpr int kIdSkip = 3;

SyncResolution ResolutionForId(int id) {
  switch (id) {
    case kIdTheirs:
      return SyncResolution::kTheirs;
    case kIdSkip:
      return SyncResolution::kSkip;
    default:
      return SyncResolution::kMine;
  }
}

}  // namespace

SyncConflictPanel::SyncConflictPanel(std::vector<SyncConflict> conflicts) {
  heading_.setText(
      juce::String::fromUTF8(
          "The device changed since the last sync. Choose a side for each "
          "conflict \xe2\x80\x94 \xe2\x80\x9c"
          "Do nothing\xe2\x80\x9d leaves "
          "the two copies different and asks again next time."),
      juce::dontSendNotification);
  heading_.setJustificationType(juce::Justification::topLeft);
  heading_.setMinimumHorizontalScale(1.0f);
  addAndMakeVisible(heading_);

  for (const SyncConflict& conflict : conflicts) {
    auto row = std::make_unique<Row>();
    row->label.setText(conflict.description, juce::dontSendNotification);
    row->label.setJustificationType(juce::Justification::centredLeft);
    row->label.setMinimumHorizontalScale(0.7f);
    row_holder_.addAndMakeVisible(row->label);
    row->choice.addItem("My copy wins", kIdMine);
    row->choice.addItem("Device wins", kIdTheirs);
    row->choice.addItem("Do nothing", kIdSkip);
    // The user pressed "Save Changes to Device": pushing their copy is
    // the expected default.
    row->choice.setSelectedId(kIdMine, juce::dontSendNotification);
    row_holder_.addAndMakeVisible(row->choice);
    rows_.push_back(std::move(row));
  }
  viewport_.setViewedComponent(&row_holder_, false);
  viewport_.setScrollBarsShown(true, false);
  addAndMakeVisible(viewport_);

  // Close the dialog BEFORE running the callback: the apply path opens the
  // sync progress dialog, which must not stack on this still-modal window.
  // Capture the callback by value and defer it so it fires once this window
  // is gone (CloseDialog may delete `this`).
  apply_.onClick = [this] {
    decided_ = true;
    auto cb = on_apply;
    auto resolutions = Resolutions();
    CloseDialog();
    if (cb) {
      juce::MessageManager::callAsync([cb, resolutions] { cb(resolutions); });
    }
  };
  cancel_.onClick = [this] {
    decided_ = true;
    auto cb = on_cancel;
    CloseDialog();
    if (cb) {
      juce::MessageManager::callAsync([cb] { cb(); });
    }
  };
  addAndMakeVisible(apply_);
  addAndMakeVisible(cancel_);
}

SyncConflictPanel::~SyncConflictPanel() {
  // The window's own close paths (escape, the close button) never hit a
  // button; they still owe the caller an answer.
  if (!decided_ && on_cancel) {
    on_cancel();
  }
}

std::vector<SyncResolution> SyncConflictPanel::Resolutions() const {
  std::vector<SyncResolution> resolutions;
  resolutions.reserve(rows_.size());
  for (const auto& row : rows_) {
    resolutions.push_back(ResolutionForId(row->choice.getSelectedId()));
  }
  return resolutions;
}

void SyncConflictPanel::CloseDialog() {
  if (auto* dialog = findParentComponentOfClass<juce::DialogWindow>()) {
    dialog->exitModalState(0);
  }
}

void SyncConflictPanel::resized() {
  auto area = getLocalBounds().reduced(16);
  heading_.setBounds(area.removeFromTop(48));
  auto buttons = area.removeFromBottom(36).withTrimmedTop(8);
  apply_.setBounds(buttons.removeFromRight(110));
  buttons.removeFromRight(8);
  cancel_.setBounds(buttons.removeFromRight(110));
  viewport_.setBounds(area);

  const int width = area.getWidth() - viewport_.getScrollBarThickness();
  row_holder_.setSize(width, static_cast<int>(rows_.size()) * kRowHeight);
  int y = 0;
  for (const auto& row : rows_) {
    auto row_area = juce::Rectangle<int>(0, y, width, kRowHeight).reduced(0, 6);
    row->choice.setBounds(row_area.removeFromRight(kChoiceWidth));
    row_area.removeFromRight(8);
    row->label.setBounds(row_area);
    y += kRowHeight;
  }
}

void SyncConflictPanel::Show(
    std::vector<SyncConflict> conflicts,
    std::function<void(std::vector<SyncResolution>)> on_apply,
    std::function<void()> on_cancel) {
  auto panel = std::make_unique<SyncConflictPanel>(std::move(conflicts));
  panel->on_apply = std::move(on_apply);
  panel->on_cancel = std::move(on_cancel);
  const int rows = static_cast<int>(panel->rows_.size());
  panel->setSize(680, juce::jmin(560, 16 + 48 + rows * kRowHeight + 44 + 16));

  juce::DialogWindow::LaunchOptions options;
  options.content.setOwned(panel.release());
  options.dialogTitle = "Sync with Device";
  options.dialogBackgroundColour = juce::Colour(0xff2b2b2b);
  options.escapeKeyTriggersCloseButton = true;
  options.useNativeTitleBar = true;
  options.resizable = false;
  options.launchAsync();
}

}  // namespace spdsx
