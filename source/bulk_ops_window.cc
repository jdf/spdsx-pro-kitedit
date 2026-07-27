#include "bulk_ops_window.h"

#include <memory>
#include <thread>
#include <utility>

#include "app_log.h"
#include "device/serial_port.h"

namespace spdsx {
namespace {

constexpr int kNavWidth = 240;
constexpr int kMargin = 16;
constexpr int kRowHeight = 56;  // nav rows

// Roughly twice JUCE's defaults throughout: body text and controls.
constexpr float kBodyFont = 24.0f;
constexpr float kMetaFont = 22.0f;
constexpr int kCaptionRow = 32;
constexpr int kControlRow = 44;
constexpr int kHintRow = 32;

const juce::Colour kPanelBg(0xff12161b);
const juce::Colour kNavBg(0xff0d1117);
const juce::Colour kText(0xffe6edf3);
const juce::Colour kMeta(0xff8b949e);
const juce::Colour kPreviewBg(0xff05070a);

// A label + control row, laid out by the pages.
juce::Label& Caption(juce::Label& label, const juce::String& text) {
  label.setText(text, juce::dontSendNotification);
  label.setColour(juce::Label::textColourId, kMeta);
  label.setFont(juce::FontOptions(kBodyFont));
  return label;
}

// The stock theme with every control's text scaled to match kBodyFont;
// LookAndFeel_V4 otherwise pins toggles and buttons near 15pt.
class BulkLookAndFeel : public juce::LookAndFeel_V4 {
public:
  juce::Font getTextButtonFont(juce::TextButton&, int) override {
    return juce::Font(juce::FontOptions(kBodyFont));
  }

  juce::Font getComboBoxFont(juce::ComboBox&) override {
    return juce::Font(juce::FontOptions(kBodyFont));
  }

  juce::Font getPopupMenuFont() override {
    return juce::Font(juce::FontOptions(kBodyFont));
  }

  // LookAndFeel_V4's drawing, minus its 15pt font ceiling.
  void drawToggleButton(juce::Graphics& g,
                        juce::ToggleButton& button,
                        bool highlighted,
                        bool down) override {
    const float font_size =
        juce::jmin(kBodyFont, static_cast<float>(button.getHeight()) * 0.75f);
    const float tick = font_size * 1.1f;
    drawTickBox(g,
                button,
                4.0f,
                (static_cast<float>(button.getHeight()) - tick) * 0.5f,
                tick,
                tick,
                button.getToggleState(),
                button.isEnabled(),
                highlighted,
                down);
    g.setColour(button.findColour(juce::ToggleButton::textColourId));
    g.setFont(font_size);
    if (!button.isEnabled()) {
      g.setOpacity(0.5f);
    }
    g.drawFittedText(button.getButtonText(),
                     button.getLocalBounds()
                         .withTrimmedLeft(juce::roundToInt(tick) + 10)
                         .withTrimmedRight(2),
                     juce::Justification::centredLeft,
                     10);
  }
};

// A --kits entry: caption, field, and the format spelled out. The hint
// stays on screen rather than living in a placeholder that vanishes the
// moment you type, because a spec is easy to get slightly wrong and
// expensive to get wrong.
class KitSpecField : public juce::Component {
public:
  KitSpecField() {
    addAndMakeVisible(Caption(caption_, "Kits"));
    editor_.setFont(juce::FontOptions(kBodyFont));
    editor_.onTextChange = [this] {
      if (on_changed) {
        on_changed();
      }
    };
    addAndMakeVisible(editor_);
    addAndMakeVisible(Caption(
        hint_, "one kit (108), a range (108-200), or a list (1,5,10-20)"));
    hint_.setFont(juce::FontOptions(kMetaFont));
  }

  std::function<void()> on_changed;

  static constexpr int kHeight = kCaptionRow + kControlRow + kHintRow;

  bool Parse(std::vector<spdutil::KitRange>* out, std::string* error) const {
    return spdutil::ParseKitSpec(
        editor_.getText().trim().toStdString(), out, error);
  }

  // Empty when the field names some kits; otherwise why it does not.
  juce::String Problem() const {
    std::vector<spdutil::KitRange> ranges;
    std::string error;
    if (Parse(&ranges, &error)) {
      return {};
    }
    return editor_.getText().trim().isEmpty()
        ? juce::String("Enter the kits to change.")
        : juce::String(error);
  }

  void resized() override {
    auto area = getLocalBounds();
    caption_.setBounds(area.removeFromTop(kCaptionRow));
    editor_.setBounds(area.removeFromTop(kControlRow));
    hint_.setBounds(area.removeFromTop(kHintRow));
  }

private:
  juce::Label caption_;
  juce::TextEditor editor_;
  juce::Label hint_;
};

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
    kits_.on_changed = [this] { Changed(); };
    addAndMakeVisible(kits_);

    addAndMakeVisible(Caption(pads_caption_, "Pads"));
    // No pad starts ticked: choosing the pads is part of saying what the
    // operation is, and the Run buttons stay disabled until you do.
    for (int pad = 1; pad <= 9; ++pad) {
      auto button = std::make_unique<juce::ToggleButton>(juce::String(pad));
      button->onClick = [this] { Changed(); };
      addAndMakeVisible(*button);
      pads_.push_back(std::move(button));
    }
    addAndMakeVisible(Caption(pads_note_, ""));

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
    Changed();  // fills in the pad summary
  }

  juce::String Problem() const override {
    if (const juce::String kits = kits_.Problem(); kits.isNotEmpty()) {
      return kits;
    }
    if (TickedPads().empty()) {
      return "You must select at least one pad.";
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
    kits_.setBounds(row(KitSpecField::kHeight));
    area.removeFromTop(12);

    pads_caption_.setBounds(row(kCaptionRow));
    auto pad_row = row(kControlRow);
    for (auto& pad : pads_) {
      pad->setBounds(pad_row.removeFromLeft(74));
    }
    pads_note_.setBounds(row(kHintRow));
    area.removeFromTop(12);

    mode_caption_.setBounds(row(kCaptionRow));
    mode_.setBounds(row(kControlRow).removeFromLeft(280));
    area.removeFromTop(12);

    auto if_row = row(kControlRow);
    if_mode_enabled_.setBounds(if_row.removeFromLeft(320));
    if_mode_.setBounds(if_row.removeFromLeft(280));
    area.removeFromTop(12);

    commit_.setBounds(row(kControlRow));
  }

private:
  void Changed() {
    if_mode_.setEnabled(if_mode_enabled_.getToggleState());
    const std::vector<int> pads = TickedPads();
    pads_note_.setText(
        pads.size() == pads_.size()
            ? "all nine pads"
            : (pads.empty() ? "none selected"
                            : juce::String(pads.size()) + " of nine"),
        juce::dontSendNotification);
    if (on_changed) {
      on_changed();
    }
  }

  std::vector<int> TickedPads() const {
    std::vector<int> pads;
    for (size_t i = 0; i < pads_.size(); ++i) {
      if (pads_[i]->getToggleState()) {
        pads.push_back(static_cast<int>(i) + 1);
      }
    }
    return pads;
  }

  ops::SetModeRequest Request(bool dry_run) const {
    ops::SetModeRequest request;
    std::string error;
    kits_.Parse(&request.kits, &error);
    // All nine ticked is no restriction at all, which is what an omitted
    // --pad means; rendering nine of them would be noise.
    if (std::vector<int> pads = TickedPads(); pads.size() < pads_.size()) {
      request.pads = std::move(pads);
    }
    request.target = static_cast<LayerMode>(mode_.getSelectedId() - 1);
    request.has_if_mode = if_mode_enabled_.getToggleState();
    request.if_mode = static_cast<LayerMode>(if_mode_.getSelectedId() - 1);
    request.commit = commit_.getToggleState();
    request.dry_run = dry_run;
    return request;
  }

  KitSpecField kits_;
  juce::Label pads_caption_;
  juce::Label pads_note_;
  std::vector<std::unique_ptr<juce::ToggleButton>> pads_;
  juce::Label mode_caption_;
  juce::ComboBox mode_;
  juce::ToggleButton if_mode_enabled_;
  juce::ComboBox if_mode_;
  juce::ToggleButton commit_;
};

// ---- info ----

class InfoPage : public BulkOpPage {
public:
  InfoPage() {
    addAndMakeVisible(Caption(
        text_, "Finds the device, pings it, and asks its firmware version."));
  }

  juce::String Problem() const override { return {}; }

  bool CanDryRun() const override { return false; }  // it only reads

  juce::String CommandLine(bool) const override {
    return juce::String(ops::CommandLine(ops::InfoRequest {}));
  }

  juce::String Run(device::SpdsxDevice& dev,
                   bool,
                   const ops::ProgressFn& progress,
                   const ops::AbortFn&) override {
    const ops::InfoResult result = ops::Info(dev, progress);
    if (result.version.empty()) {
      return "connected, but the unit did not answer the version query";
    }
    juce::String out =
        juce::String::fromUTF8("connected \xe2\x80\x94 firmware ")
        + juce::String(result.version);
    if (!result.build.empty()) {
      out << " (build " << juce::String(result.build) << ")";
    }
    return out;
  }

  void resized() override {
    text_.setBounds(getLocalBounds().removeFromTop(kCaptionRow));
  }

private:
  juce::Label text_;
};

}  // namespace

BulkOpsPanel::BulkOpsPanel() {
  look_and_feel_ = std::make_unique<BulkLookAndFeel>();
  setLookAndFeel(look_and_feel_.get());

  modes_.push_back({"Device info",
                    "Who is on the other end of the cable.",
                    std::make_unique<InfoPage>()});
  modes_.push_back({"Layer mode",
                    "Set the layer mode of pads across a range of kits.",
                    std::make_unique<SetModePage>()});

  nav_.setModel(this);
  nav_.setRowHeight(kRowHeight);
  nav_.setColour(juce::ListBox::backgroundColourId, kNavBg);
  addAndMakeVisible(nav_);

  blurb_.setColour(juce::Label::textColourId, kMeta);
  blurb_.setFont(juce::FontOptions(kBodyFont));
  addAndMakeVisible(blurb_);

  preview_.setColour(juce::Label::textColourId, kText);
  preview_.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(),
                                     kMetaFont,
                                     juce::Font::plain));
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
  status_.setFont(juce::FontOptions(kMetaFont));
  addAndMakeVisible(status_);

  connection_text_.setColour(juce::Label::textColourId, kMeta);
  connection_text_.setFont(juce::FontOptions(kMetaFont));
  connection_text_.setText("no device connected", juce::dontSendNotification);
  connection_dot_.SetDiameter(12.0f);
  addAndMakeVisible(connection_dot_);
  addAndMakeVisible(connection_text_);

  for (auto& mode : modes_) {
    mode.page->on_changed = [this] { RefreshPreview(); };
    addChildComponent(*mode.page);
  }
  nav_.selectRow(0);
  ShowMode(0);
}

BulkOpsPanel::~BulkOpsPanel() {
  setLookAndFeel(nullptr);
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
  g.setFont(juce::FontOptions(kBodyFont));
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
  dry_run_.setVisible(page.CanDryRun());
  // Even a dry run reads the device, so nothing runs without one.
  dry_run_.setEnabled(ready && !running_ && device_connected_);
  run_.setEnabled(ready && !running_ && device_connected_);
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

void BulkOpsPanel::SetConnection(bool connected, const juce::String& text) {
  device_connected_ = connected;
  connection_dot_.SetConnected(connected);
  connection_text_.setText(text, juce::dontSendNotification);
  RefreshPreview();
}

void BulkOpsPanel::paint(juce::Graphics& g) {
  g.fillAll(kPanelBg);
  const auto strip = getLocalBounds().removeFromBottom(38);
  g.setColour(kNavBg);
  g.fillRect(strip);
  g.setColour(juce::Colour(0xff222831));
  g.fillRect(strip.withHeight(1));
  auto preview = preview_.getBounds().expanded(8, 6);
  g.setColour(kPreviewBg);
  g.fillRoundedRectangle(preview.toFloat(), 5.0f);
}

void BulkOpsPanel::resized() {
  auto area = getLocalBounds();
  auto strip = area.removeFromBottom(38);
  // Nudged to the text's OPTICAL center: lowercase pixel mass sits ~2px
  // below the geometric middle of the label (measured off a render), and
  // a dot centered geometrically reads as floating high.
  connection_dot_.setBounds(strip.removeFromLeft(34).translated(0, 2));
  connection_text_.setBounds(strip.withTrimmedRight(8));
  nav_.setBounds(area.removeFromLeft(kNavWidth));
  area = area.reduced(kMargin);

  auto buttons = area.removeFromBottom(46);
  run_.setBounds(buttons.removeFromRight(150));
  buttons.removeFromRight(10);
  dry_run_.setBounds(buttons.removeFromRight(170));
  buttons.removeFromRight(10);
  abort_.setBounds(buttons.removeFromRight(130));
  status_.setBounds(buttons.withTrimmedRight(8));
  area.removeFromBottom(10);
  progress_bar_.setBounds(area.removeFromBottom(24));
  area.removeFromBottom(12);

  // The command line sits just above the buttons, framed by paint().
  preview_.setBounds(area.removeFromBottom(60).reduced(10, 8));
  area.removeFromBottom(12);

  blurb_.setBounds(area.removeFromTop(kCaptionRow + 4));
  area.removeFromTop(10);
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
  panel_ = new BulkOpsPanel();
  setContentOwned(panel_, false);
  setResizable(true, true);
  setResizeLimits(900, 740, 1600, 1200);
  centreWithSize(980, 800);
  setVisible(true);
}

void BulkOpsWindow::SetConnection(bool connected, const juce::String& text) {
  if (panel_ != nullptr) {
    panel_->SetConnection(connected, text);
  }
}

void BulkOpsWindow::closeButtonPressed() {
  setVisible(false);  // the owner keeps it; the Window menu brings it back
}

}  // namespace spdsx
