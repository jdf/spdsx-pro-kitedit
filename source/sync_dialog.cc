#include "sync_dialog.h"

#include <algorithm>

namespace spdsx {

namespace {

constexpr int kRowHeight = 52;
constexpr int kOutcomeWidth = 110;
constexpr int kCheckWidth = 30;
constexpr int kToolbarHeight = 32;

const char* OutcomeText(SyncResolution resolution) {
  switch (resolution) {
    case SyncResolution::kTheirs:
      return "Device's";
    case SyncResolution::kSkip:
      return "Do nothing";
    default:
      return "Mine";
  }
}

}  // namespace

void TriStateToggle::SetMixed(bool mixed) {
  if (mixed != mixed_) {
    mixed_ = mixed;
    repaint();
  }
}

void TriStateToggle::paintButton(juce::Graphics& g,
                                 bool should_draw_highlighted,
                                 bool should_draw_down) {
  juce::ToggleButton::paintButton(g, should_draw_highlighted, should_draw_down);
  if (!mixed_) {
    return;
  }
  // The same box LookAndFeel_V4 ticks, with a dash instead of a tick.
  const float size =
      juce::jmin(15.0f, static_cast<float>(getHeight()) * 0.75f) * 1.1f;
  const float y = (static_cast<float>(getHeight()) - size) * 0.5f;
  g.setColour(findColour(juce::ToggleButton::tickColourId));
  g.fillRoundedRectangle(
      4.0f + size * 0.25f, y + size * 0.46f, size * 0.5f, size * 0.1f, 1.0f);
}

SyncConflictPanel::SyncConflictPanel(std::vector<SyncConflict> conflicts) {
  heading_.setText(
      juce::String::fromUTF8(
          "The device changed since the last sync. Tick the conflicts you "
          "want to answer together, then choose a side — “Do Nothing” "
          "leaves that copy alone on both sides and asks again next time."),
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
    // The user pressed "Sync Changes with Device": pushing their copy is
    // the expected default, and every row starts selected so one click
    // can answer the whole list.
    row->check.setToggleState(true, juce::dontSendNotification);
    row->check.onClick = [this] { RefreshSelection(); };
    row_holder_.addAndMakeVisible(row->check);
    row->outcome.setJustificationType(juce::Justification::centredRight);
    row_holder_.addAndMakeVisible(row->outcome);
    ShowOutcome(*row);
    rows_.push_back(std::move(row));
  }
  viewport_.setViewedComponent(&row_holder_, false);
  viewport_.setScrollBarsShown(true, false);
  addAndMakeVisible(viewport_);

  select_all_.setToggleState(true, juce::dontSendNotification);
  select_all_.onClick = [this] {
    // Mixed means "not all", so the useful move is to select everything.
    CheckAll(select_all_.mixed() || select_all_.getToggleState());
  };
  addAndMakeVisible(select_all_);

  keep_mine_.onClick = [this] { ApplyToChecked(SyncResolution::kMine); };
  keep_theirs_.onClick = [this] { ApplyToChecked(SyncResolution::kTheirs); };
  keep_neither_.onClick = [this] { ApplyToChecked(SyncResolution::kSkip); };
  addAndMakeVisible(keep_mine_);
  addAndMakeVisible(keep_theirs_);
  addAndMakeVisible(keep_neither_);

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
  RefreshSelection();
}

SyncConflictPanel::~SyncConflictPanel() {
  // The window's own close paths (escape, the close button) never hit a
  // button; they still owe the caller an answer.
  if (!decided_ && on_cancel) {
    on_cancel();
  }
}

void SyncConflictPanel::ShowOutcome(Row& row) {
  row.outcome.setText(OutcomeText(row.resolution), juce::dontSendNotification);
}

void SyncConflictPanel::ApplyToChecked(SyncResolution resolution) {
  for (const auto& row : rows_) {
    if (row->check.getToggleState()) {
      row->resolution = resolution;
      ShowOutcome(*row);
    }
  }
}

void SyncConflictPanel::CheckAll(bool checked) {
  for (const auto& row : rows_) {
    row->check.setToggleState(checked, juce::dontSendNotification);
  }
  RefreshSelection();
}

void SyncConflictPanel::RefreshSelection() {
  const size_t checked = static_cast<size_t>(
      std::count_if(rows_.begin(), rows_.end(), [](const auto& row) {
        return row->check.getToggleState();
      }));
  const bool all = !rows_.empty() && checked == rows_.size();
  select_all_.setToggleState(all, juce::dontSendNotification);
  select_all_.SetMixed(checked > 0 && !all);
  // The bulk buttons act on the ticked rows, so they do nothing with none.
  const bool any = checked > 0;
  keep_mine_.setEnabled(any);
  keep_theirs_.setEnabled(any);
  keep_neither_.setEnabled(any);
}

std::vector<SyncResolution> SyncConflictPanel::Resolutions() const {
  std::vector<SyncResolution> resolutions;
  resolutions.reserve(rows_.size());
  for (const auto& row : rows_) {
    resolutions.push_back(row->resolution);
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
  heading_.setBounds(area.removeFromTop(52));

  auto toolbar = area.removeFromTop(kToolbarHeight);
  select_all_.setBounds(toolbar.removeFromLeft(110));
  keep_neither_.setBounds(toolbar.removeFromRight(110));
  toolbar.removeFromRight(6);
  keep_theirs_.setBounds(toolbar.removeFromRight(120));
  toolbar.removeFromRight(6);
  keep_mine_.setBounds(toolbar.removeFromRight(110));
  area.removeFromTop(8);

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
    row->check.setBounds(row_area.removeFromLeft(kCheckWidth));
    row->outcome.setBounds(row_area.removeFromRight(kOutcomeWidth));
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
  panel->setSize(
      700,
      juce::jmin(600,
                 16 + 52 + kToolbarHeight + 8 + rows * kRowHeight + 44 + 16));

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
