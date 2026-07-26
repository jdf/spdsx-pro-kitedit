#include "bulk_ops_window.h"

#include <memory>
#include <thread>
#include <utility>

#include "app_log.h"
#include "device/serial_port.h"

namespace spdsx {
namespace {

constexpr int kNavWidth = 190;
constexpr int kMargin = 16;
constexpr int kRowHeight = 34;

const juce::Colour kPanelBg(0xff12161b);
const juce::Colour kNavBg(0xff0d1117);
const juce::Colour kText(0xffe6edf3);
const juce::Colour kMeta(0xff8b949e);
const juce::Colour kPreviewBg(0xff05070a);

// A label + control row, laid out by the pages.
juce::Label& Caption(juce::Label& label, const juce::String& text) {
  label.setText(text, juce::dontSendNotification);
  label.setColour(juce::Label::textColourId, kMeta);
  label.setFont(juce::FontOptions(13.0f));
  return label;
}

void FillModes(juce::ComboBox& box) {
  for (int i = 0; i < kLayerModeCount; ++i) {
    box.addItem(
        juce::String(std::string(LayerModeName(static_cast<LayerMode>(i)))),
        i + 1);
  }
}

// ---- setmode ----

class SetModePage : public BulkOpPage {
public:
  SetModePage() {
    addAndMakeVisible(Caption(kits_caption_, "Kits"));
    kits_.setTextToShowWhenEmpty("108-200, or 1,5,10-20", kMeta);
    kits_.onTextChange = [this] { Changed(); };
    addAndMakeVisible(kits_);

    addAndMakeVisible(Caption(pads_caption_, "Pads"));
    for (int pad = 1; pad <= 9; ++pad) {
      auto button = std::make_unique<juce::ToggleButton>(juce::String(pad));
      button->onClick = [this] { Changed(); };
      addAndMakeVisible(*button);
      pads_.push_back(std::move(button));
    }
    addAndMakeVisible(Caption(pads_note_, "none ticked = all nine"));

    addAndMakeVisible(Caption(mode_caption_, "Set mode to"));
    FillModes(mode_);
    mode_.setSelectedId(1, juce::dontSendNotification);
    mode_.onChange = [this] { Changed(); };
    addAndMakeVisible(mode_);

    if_mode_enabled_.setButtonText("Only pads currently in");
    if_mode_enabled_.onClick = [this] { Changed(); };
    addAndMakeVisible(if_mode_enabled_);
    FillModes(if_mode_);
    if_mode_.setSelectedId(1, juce::dontSendNotification);
    if_mode_.onChange = [this] { Changed(); };
    addAndMakeVisible(if_mode_);

    commit_.setButtonText("Commit to flash (otherwise a power cycle reverts)");
    commit_.setToggleState(true, juce::dontSendNotification);
    commit_.onClick = [this] { Changed(); };
    addAndMakeVisible(commit_);
  }

  juce::String Problem() const override {
    std::vector<spdutil::KitRange> ranges;
    std::string error;
    if (!spdutil::ParseKitSpec(
            kits_.getText().trim().toStdString(), &ranges, &error)) {
      return kits_.getText().trim().isEmpty()
          ? juce::String::fromUTF8("Say which kits — a write never guesses.")
          : juce::String(error);
    }
    return {};
  }

  juce::String CommandLine(bool dry_run) const override {
    return juce::String(ops::CommandLine(Request(dry_run)));
  }

  juce::String Run(device::SpdsxDevice& dev,
                   bool dry_run,
                   const ops::ProgressFn& progress,
                   const ops::AbortFn& should_abort) override {
    const ops::SetModeResult result =
        ops::SetMode(dev, Request(dry_run), progress, should_abort);
    if (result.aborted) {
      return "aborted after " + juce::String(result.changed) + " planned";
    }
    if (dry_run) {
      return juce::String(result.changed) + " pad(s) would change";
    }
    if (result.changed == 0) {
      return "nothing to change";
    }
    return juce::String(result.changed) + " pad(s) changed"
        + (result.committed ? ", committed"
                            : (commit_.getToggleState() ? ", COMMIT DID NOT "
                                                          "CONFIRM"
                                                        : ", working state"));
  }

  void resized() override {
    auto area = getLocalBounds();
    auto row = [&area](int h) { return area.removeFromTop(h); };
    kits_caption_.setBounds(row(18));
    kits_.setBounds(row(26));
    area.removeFromTop(10);

    pads_caption_.setBounds(row(18));
    auto pad_row = row(26);
    for (auto& pad : pads_) {
      pad->setBounds(pad_row.removeFromLeft(46));
    }
    pads_note_.setBounds(row(16));
    area.removeFromTop(10);

    mode_caption_.setBounds(row(18));
    mode_.setBounds(row(26).removeFromLeft(180));
    area.removeFromTop(10);

    auto if_row = row(26);
    if_mode_enabled_.setBounds(if_row.removeFromLeft(190));
    if_mode_.setBounds(if_row.removeFromLeft(180));
    area.removeFromTop(10);

    commit_.setBounds(row(26));
  }

private:
  void Changed() {
    if_mode_.setEnabled(if_mode_enabled_.getToggleState());
    if (on_changed) {
      on_changed();
    }
  }

  ops::SetModeRequest Request(bool dry_run) const {
    ops::SetModeRequest request;
    std::string error;
    spdutil::ParseKitSpec(
        kits_.getText().trim().toStdString(), &request.kits, &error);
    for (size_t i = 0; i < pads_.size(); ++i) {
      if (pads_[i]->getToggleState()) {
        request.pads.push_back(static_cast<int>(i) + 1);
      }
    }
    request.target = static_cast<LayerMode>(mode_.getSelectedId() - 1);
    request.has_if_mode = if_mode_enabled_.getToggleState();
    request.if_mode = static_cast<LayerMode>(if_mode_.getSelectedId() - 1);
    request.commit = commit_.getToggleState();
    request.dry_run = dry_run;
    return request;
  }

  juce::Label kits_caption_;
  juce::TextEditor kits_;
  juce::Label pads_caption_;
  juce::Label pads_note_;
  std::vector<std::unique_ptr<juce::ToggleButton>> pads_;
  juce::Label mode_caption_;
  juce::ComboBox mode_;
  juce::ToggleButton if_mode_enabled_;
  juce::ComboBox if_mode_;
  juce::ToggleButton commit_;
};

// ---- setname ----

class SetNamePage : public BulkOpPage {
public:
  SetNamePage() {
    addAndMakeVisible(Caption(kit_caption_, "Kit"));
    kit_.setRange(spdutil::kFirstKit, spdutil::kLastKit, 1);
    kit_.setSliderStyle(juce::Slider::IncDecButtons);
    kit_.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 60, 24);
    kit_.setValue(1, juce::dontSendNotification);
    kit_.onValueChange = [this] { Changed(); };
    addAndMakeVisible(kit_);

    addAndMakeVisible(Caption(name_caption_, "Name"));
    // The device's field is a fixed 16 characters; what you type is what
    // the kit gets.
    name_.setInputRestrictions(device::kKitNameLength);
    name_.setTextToShowWhenEmpty("up to 16 characters", kMeta);
    name_.onTextChange = [this] { Changed(); };
    addAndMakeVisible(name_);

    commit_.setButtonText("Commit to flash (otherwise a power cycle reverts)");
    commit_.setToggleState(true, juce::dontSendNotification);
    commit_.onClick = [this] { Changed(); };
    addAndMakeVisible(commit_);
  }

  juce::String Problem() const override {
    if (name_.getText().trim().isEmpty()) {
      return "A kit name cannot be empty.";
    }
    return {};
  }

  juce::String CommandLine(bool dry_run) const override {
    return juce::String(ops::CommandLine(Request(dry_run)));
  }

  juce::String Run(device::SpdsxDevice& dev,
                   bool dry_run,
                   const ops::ProgressFn& progress,
                   const ops::AbortFn& should_abort) override {
    const ops::SetNameResult result =
        ops::SetName(dev, Request(dry_run), progress, should_abort);
    if (dry_run) {
      return "would name kit " + juce::String(Request(true).kit);
    }
    return "kit named"
        + juce::String(result.committed ? ", committed"
                                        : (commit_.getToggleState()
                                               ? ", COMMIT DID NOT CONFIRM"
                                               : ", working state"));
  }

  void resized() override {
    auto area = getLocalBounds();
    auto row = [&area](int h) { return area.removeFromTop(h); };
    kit_caption_.setBounds(row(18));
    kit_.setBounds(row(26).removeFromLeft(160));
    area.removeFromTop(10);
    name_caption_.setBounds(row(18));
    name_.setBounds(row(26).removeFromLeft(260));
    area.removeFromTop(10);
    commit_.setBounds(row(26));
  }

private:
  void Changed() {
    if (on_changed) {
      on_changed();
    }
  }

  ops::SetNameRequest Request(bool dry_run) const {
    ops::SetNameRequest request;
    request.kit = static_cast<int>(kit_.getValue());
    request.name = name_.getText().trim().toStdString();
    request.commit = commit_.getToggleState();
    request.dry_run = dry_run;
    return request;
  }

  juce::Label kit_caption_;
  juce::Slider kit_;
  juce::Label name_caption_;
  juce::TextEditor name_;
  juce::ToggleButton commit_;
};

}  // namespace

BulkOpsPanel::BulkOpsPanel() {
  modes_.push_back({"Layer mode",
                    "Set the layer mode of pads across a range of kits.",
                    std::make_unique<SetModePage>()});
  modes_.push_back(
      {"Kit name", "Rename one kit.", std::make_unique<SetNamePage>()});

  nav_.setModel(this);
  nav_.setRowHeight(kRowHeight);
  nav_.setColour(juce::ListBox::backgroundColourId, kNavBg);
  addAndMakeVisible(nav_);

  blurb_.setColour(juce::Label::textColourId, kMeta);
  blurb_.setFont(juce::FontOptions(13.0f));
  addAndMakeVisible(blurb_);

  preview_.setColour(juce::Label::textColourId, kText);
  preview_.setFont(juce::FontOptions(
      juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain));
  preview_.setJustificationType(juce::Justification::topLeft);
  preview_.setMinimumHorizontalScale(1.0f);
  addAndMakeVisible(preview_);

  dry_run_.onClick = [this] { Start(true); };
  run_.onClick = [this] { Start(false); };
  abort_.onClick = [this] {
    if (abort_flag_ != nullptr) {
      *abort_flag_ = true;
    }
  };
  addAndMakeVisible(dry_run_);
  addAndMakeVisible(run_);
  addChildComponent(abort_);

  progress_bar_.setVisible(false);
  addChildComponent(progress_bar_);
  status_.setColour(juce::Label::textColourId, kMeta);
  status_.setFont(juce::FontOptions(12.0f));
  addAndMakeVisible(status_);

  for (auto& mode : modes_) {
    mode.page->on_changed = [this] { RefreshPreview(); };
    addChildComponent(*mode.page);
  }
  nav_.selectRow(0);
  ShowMode(0);
}

BulkOpsPanel::~BulkOpsPanel() {
  if (abort_flag_ != nullptr) {
    *abort_flag_ = true;  // the worker checks this and stops
  }
}

int BulkOpsPanel::getNumRows() {
  return static_cast<int>(modes_.size());
}

void BulkOpsPanel::paintListBoxItem(
    int row, juce::Graphics& g, int width, int height, bool selected) {
  if (row < 0 || static_cast<size_t>(row) >= modes_.size()) {
    return;
  }
  if (selected) {
    g.setColour(juce::Colour(0xff1f6feb).withAlpha(0.25f));
    g.fillRect(0, 0, width, height);
  }
  g.setColour(selected ? kText : kMeta);
  g.setFont(juce::FontOptions(14.0f));
  g.drawText(modes_[static_cast<size_t>(row)].name,
             juce::Rectangle<int>(12, 0, width - 16, height),
             juce::Justification::centredLeft);
}

void BulkOpsPanel::selectedRowsChanged(int row) {
  ShowMode(row);
}

void BulkOpsPanel::ShowMode(int index) {
  if (index < 0 || static_cast<size_t>(index) >= modes_.size()) {
    return;
  }
  for (size_t i = 0; i < modes_.size(); ++i) {
    modes_[i].page->setVisible(static_cast<int>(i) == index);
  }
  current_ = index;
  blurb_.setText(modes_[static_cast<size_t>(index)].blurb,
                 juce::dontSendNotification);
  resized();
  RefreshPreview();
}

void BulkOpsPanel::RefreshPreview() {
  if (current_ < 0) {
    return;
  }
  BulkOpPage& page = *modes_[static_cast<size_t>(current_)].page;
  const juce::String problem = page.Problem();
  const bool ready = problem.isEmpty();
  preview_.setText(ready ? page.CommandLine(false) : problem,
                   juce::dontSendNotification);
  preview_.setColour(juce::Label::textColourId, ready ? kText : kMeta);
  dry_run_.setEnabled(ready && !running_);
  run_.setEnabled(ready && !running_);
}

void BulkOpsPanel::Start(bool dry_run) {
  if (running_ || current_ < 0) {
    return;
  }
  BulkOpPage& page = *modes_[static_cast<size_t>(current_)].page;
  if (page.Problem().isNotEmpty()) {
    return;
  }
  running_ = true;
  progress_ = 0.0;
  progress_bar_.setVisible(true);
  abort_.setVisible(true);
  status_.setText(juce::String::fromUTF8(dry_run ? "planning…" : "working…"),
                  juce::dontSendNotification);
  RefreshPreview();

  abort_flag_ = std::make_shared<std::atomic<bool>>(false);
  auto abort_flag = abort_flag_;
  const juce::String command = page.CommandLine(dry_run);
  AppLog::Note("bulk op: " + command);
  juce::Component::SafePointer<BulkOpsPanel> safe(this);
  BulkOpPage* page_ptr = &page;

  std::thread([safe, page_ptr, dry_run, abort_flag] {
    juce::String outcome;
    juce::String error;
    try {
      const std::string port = device::FindDevicePort();
      const std::unique_ptr<device::SerialPort> serial =
          device::PlatformPorts().Open(port);
      device::SpdsxDevice dev(serial.get());
      outcome = page_ptr->Run(
          dev,
          dry_run,
          [safe](const ops::Progress& p) {
            juce::MessageManager::callAsync([safe, p] {
              if (safe == nullptr) {
                return;
              }
              safe->progress_ = p.total > 0
                  ? static_cast<double>(p.done) / p.total
                  : -1.0;  // indeterminate while the total is unknown
              safe->status_.setText(juce::String(p.note),
                                    juce::dontSendNotification);
            });
          },
          [abort_flag] { return abort_flag->load(); });
    } catch (const std::exception& e) {
      error = e.what();
    }
    juce::MessageManager::callAsync([safe, outcome, error] {
      if (safe != nullptr) {
        safe->Finish(outcome, error);
      }
    });
  }).detach();
}

void BulkOpsPanel::Finish(const juce::String& outcome,
                          const juce::String& error) {
  running_ = false;
  progress_bar_.setVisible(false);
  abort_.setVisible(false);
  abort_flag_.reset();
  status_.setText(error.isNotEmpty() ? "failed: " + error : outcome,
                  juce::dontSendNotification);
  AppLog::Note("bulk op finished: "
               + (error.isNotEmpty() ? "error " + error : outcome));
  RefreshPreview();
}

void BulkOpsPanel::paint(juce::Graphics& g) {
  g.fillAll(kPanelBg);
  auto preview = preview_.getBounds().expanded(8, 6);
  g.setColour(kPreviewBg);
  g.fillRoundedRectangle(preview.toFloat(), 5.0f);
}

void BulkOpsPanel::resized() {
  auto area = getLocalBounds();
  nav_.setBounds(area.removeFromLeft(kNavWidth));
  area = area.reduced(kMargin);

  auto buttons = area.removeFromBottom(30);
  run_.setBounds(buttons.removeFromRight(110));
  buttons.removeFromRight(8);
  dry_run_.setBounds(buttons.removeFromRight(110));
  buttons.removeFromRight(8);
  abort_.setBounds(buttons.removeFromRight(90));
  status_.setBounds(buttons.withTrimmedRight(8));
  area.removeFromBottom(8);
  progress_bar_.setBounds(area.removeFromBottom(18));
  area.removeFromBottom(10);

  // The command line sits just above the buttons, framed by paint().
  preview_.setBounds(area.removeFromBottom(38).reduced(8, 6));
  area.removeFromBottom(10);

  blurb_.setBounds(area.removeFromTop(20));
  area.removeFromTop(8);
  for (auto& mode : modes_) {
    mode.page->setBounds(area);
  }
}

BulkOpsWindow::BulkOpsWindow()
    : juce::DocumentWindow("Bulk Operations",
                           juce::Colour(0xff12161b),
                           juce::DocumentWindow::closeButton
                               | juce::DocumentWindow::minimiseButton) {
  setUsingNativeTitleBar(true);
  setContentOwned(new BulkOpsPanel(), false);
  setResizable(true, true);
  setResizeLimits(640, 420, 1400, 1000);
  centreWithSize(760, 520);
  setVisible(true);
}

void BulkOpsWindow::closeButtonPressed() {
  setVisible(false);  // the owner keeps it; the Window menu brings it back
}

}  // namespace spdsx
