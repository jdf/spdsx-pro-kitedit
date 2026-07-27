#include "bulk_edit_panel.h"

#include <utility>

#include "app_log.h"
#include "spdutil_args.h"

namespace spdsx {
namespace {

constexpr int kNavWidth = 200;
constexpr int kMargin = 16;
constexpr int kRowHeight = 44;  // nav rows

// Larger than JUCE's defaults, sized to sit alongside the Edit Kits tab.
constexpr float kBodyFont = 19.0f;
constexpr float kMetaFont = 17.0f;
constexpr int kCaptionRow = 26;
constexpr int kControlRow = 36;
constexpr int kHintRow = 26;

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
// moment you type.
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

class SetModePage : public BulkEditPage {
public:
  explicit SetModePage(
      std::function<juce::String(const ops::SetModeRequest&, bool)> apply)
      : apply_(std::move(apply)) {
    kits_.on_changed = [this] { Changed(); };
    addAndMakeVisible(kits_);

    addAndMakeVisible(Caption(pads_caption_, "Pads"));
    // No pad starts ticked: choosing the pads is part of saying what the
    // operation is, and the buttons stay disabled until you do.
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

  juce::String CommandLine() const override {
    return juce::String(ops::CommandLine(Request()));
  }

  juce::String Apply(bool preview) override {
    return apply_(Request(), preview);
  }

  void resized() override {
    auto area = getLocalBounds();
    auto row = [&area](int h) { return area.removeFromTop(h); };
    kits_.setBounds(row(KitSpecField::kHeight));
    area.removeFromTop(12);

    pads_caption_.setBounds(row(kCaptionRow));
    auto pad_row = row(kControlRow);
    for (auto& pad : pads_) {
      pad->setBounds(pad_row.removeFromLeft(60));
    }
    pads_note_.setBounds(row(kHintRow));
    area.removeFromTop(12);

    mode_caption_.setBounds(row(kCaptionRow));
    mode_.setBounds(row(kControlRow).removeFromLeft(230));
    area.removeFromTop(12);

    auto if_row = row(kControlRow);
    if_mode_enabled_.setBounds(if_row.removeFromLeft(260));
    if_mode_.setBounds(if_row.removeFromLeft(230));
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

  ops::SetModeRequest Request() const {
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
    return request;
  }

  std::function<juce::String(const ops::SetModeRequest&, bool)> apply_;
  KitSpecField kits_;
  juce::Label pads_caption_;
  juce::Label pads_note_;
  std::vector<std::unique_ptr<juce::ToggleButton>> pads_;
  juce::Label mode_caption_;
  juce::ComboBox mode_;
  juce::ToggleButton if_mode_enabled_;
  juce::ComboBox if_mode_;
};

}  // namespace

BulkEditPanel::BulkEditPanel(Handlers handlers) {
  look_and_feel_ = std::make_unique<BulkLookAndFeel>();
  setLookAndFeel(look_and_feel_.get());

  operations_.push_back(
      {"Layer mode",
       "Set the layer mode of pads across a range of kits.",
       std::make_unique<SetModePage>(std::move(handlers.set_mode))});

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

  preview_button_.onClick = [this] { Run(true); };
  apply_button_.onClick = [this] { Run(false); };
  addAndMakeVisible(preview_button_);
  addAndMakeVisible(apply_button_);

  status_.setColour(juce::Label::textColourId, kMeta);
  status_.setFont(juce::FontOptions(kMetaFont));
  addAndMakeVisible(status_);

  for (auto& operation : operations_) {
    operation.page->on_changed = [this] { RefreshPreview(); };
    addChildComponent(*operation.page);
  }
  nav_.selectRow(0);
  ShowOperation(0);
}

BulkEditPanel::~BulkEditPanel() {
  setLookAndFeel(nullptr);
}

int BulkEditPanel::getNumRows() {
  return static_cast<int>(operations_.size());
}

void BulkEditPanel::paintListBoxItem(
    int row, juce::Graphics& g, int width, int height, bool selected) {
  if (row < 0 || static_cast<size_t>(row) >= operations_.size()) {
    return;
  }
  if (selected) {
    g.setColour(juce::Colour(0xff1f6feb).withAlpha(0.25f));
    g.fillRect(0, 0, width, height);
  }
  g.setColour(selected ? kText : kMeta);
  g.setFont(juce::FontOptions(kBodyFont));
  g.drawText(operations_[static_cast<size_t>(row)].name,
             juce::Rectangle<int>(12, 0, width - 16, height),
             juce::Justification::centredLeft);
}

void BulkEditPanel::selectedRowsChanged(int row) {
  ShowOperation(row);
}

void BulkEditPanel::ShowOperation(int index) {
  if (index < 0 || static_cast<size_t>(index) >= operations_.size()) {
    return;
  }
  for (size_t i = 0; i < operations_.size(); ++i) {
    operations_[i].page->setVisible(static_cast<int>(i) == index);
  }
  current_ = index;
  blurb_.setText(operations_[static_cast<size_t>(index)].blurb,
                 juce::dontSendNotification);
  status_.setText({}, juce::dontSendNotification);
  resized();
  RefreshPreview();
}

void BulkEditPanel::RefreshPreview() {
  if (current_ < 0) {
    return;
  }
  BulkEditPage& page = *operations_[static_cast<size_t>(current_)].page;
  const juce::String problem = page.Problem();
  const bool ready = problem.isEmpty();
  preview_.setText(ready ? page.CommandLine() : problem,
                   juce::dontSendNotification);
  preview_.setColour(juce::Label::textColourId, ready ? kText : kMeta);
  preview_button_.setEnabled(ready);
  apply_button_.setEnabled(ready);
}

void BulkEditPanel::Run(bool preview) {
  if (current_ < 0) {
    return;
  }
  BulkEditPage& page = *operations_[static_cast<size_t>(current_)].page;
  if (page.Problem().isNotEmpty()) {
    return;
  }
  if (!preview) {
    AppLog::Note("bulk edit: " + page.CommandLine());
  }
  status_.setText(page.Apply(preview), juce::dontSendNotification);
}

void BulkEditPanel::paint(juce::Graphics& g) {
  g.fillAll(kPanelBg);
  auto preview = preview_.getBounds().expanded(8, 6);
  g.setColour(kPreviewBg);
  g.fillRoundedRectangle(preview.toFloat(), 5.0f);
}

void BulkEditPanel::resized() {
  auto area = getLocalBounds();
  nav_.setBounds(area.removeFromLeft(kNavWidth));
  area = area.reduced(kMargin);

  auto buttons = area.removeFromBottom(38);
  apply_button_.setBounds(buttons.removeFromRight(120));
  buttons.removeFromRight(10);
  preview_button_.setBounds(buttons.removeFromRight(140));
  status_.setBounds(buttons.withTrimmedRight(8));
  area.removeFromBottom(12);

  // The command line sits just above the buttons, framed by paint().
  preview_.setBounds(area.removeFromBottom(48).reduced(10, 8));
  area.removeFromBottom(12);

  blurb_.setBounds(area.removeFromTop(kCaptionRow + 4));
  area.removeFromTop(10);
  for (auto& operation : operations_) {
    operation.page->setBounds(area);
  }
}

}  // namespace spdsx
