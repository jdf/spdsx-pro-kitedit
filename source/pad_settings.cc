#include "pad_settings.h"

#include <cmath>

#include "layers.h"

namespace spdsx {

namespace {

constexpr int kPanelWidth = 210;
constexpr int kPadding = 12;
constexpr int kRowHeight = 24;
constexpr int kRowGap = 6;
constexpr int kKnobTextHeight = 18;
constexpr int kKnobSize = 60;  // the dial square, textbox below it
constexpr int kKnobHeight = kKnobSize + kKnobTextHeight;
constexpr int kLabelWidth = 60;
// The LookAndFeel draws checkbox squares at a small inset; labels and
// the knob get the same nudge so everything left-aligns.
constexpr int kAlignInset = 4;
// LookAndFeel_V4::drawRotarySlider draws the dial reduced(10) inside
// the slider bounds.
constexpr int kV4RotaryMargin = 10;

// The mix triples use small knobs so Layer A and B fit side by side.
constexpr int kMixLabelHeight = 16;
constexpr int kMixKnobSize = 48;
constexpr int kMixKnobHeight = kMixKnobSize + kKnobTextHeight;

constexpr int kMixHeight =
    2 * (kRowGap + kRowHeight + kMixLabelHeight + kMixKnobHeight);
constexpr int kBaseHeight = kPadding * 2 + kRowHeight * 3 + kRowGap * 3
    + kKnobHeight + kMixHeight;
constexpr int kPedalHeight = kRowGap + kRowHeight + (kRowGap + kKnobHeight) * 3;

// The dB range the volume knob offers; the device stores 0.1 dB steps in
// a signed 16-bit, so this is a UI choice, not a protocol limit.
constexpr double kMinVolumeDb = -60.0;
constexpr double kMaxVolumeDb = 12.0;

// What a knob's value means, and therefore its range and display: a plain
// 0-127 device value, or decibels with a tenth's precision.
enum class ControlMode { kLinear, kDecibels };

}  // namespace

PadSettingsPanel::PadSettingsPanel() {
  dynamics_.onClick = [this] {
    RefreshEnablement();
    Push();
  };
  addAndMakeVisible(dynamics_);

  for (int c = 0; c < kDynamicsCurveCount; ++c) {
    const auto name = DynamicsCurveName(static_cast<DynamicsCurve>(c));
    curve_.addItem(juce::String(name.data(), name.size()), c + 1);
  }
  curve_.onChange = [this] { Push(); };
  // Flush-left labels, nudged to line up with the checkbox squares
  // (the LookAndFeel draws those at a small inset).
  curve_label_.setBorderSize(juce::BorderSize<int>(0, kAlignInset, 0, 0));
  addAndMakeVisible(curve_label_);
  addAndMakeVisible(curve_);

  // A knob with a value box doubling as direct entry: click the
  // number and type. Double-click returns the knob to its default.
  auto init_knob = [this](juce::Slider& knob,
                          juce::Label& label,
                          int min,
                          int default_value) {
    knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    knob.setRange(min, 127, 1);
    knob.setDoubleClickReturnValue(true, default_value);
    knob.setTextBoxStyle(
        juce::Slider::TextBoxBelow, false, 48, kKnobTextHeight);
    knob.setTextBoxIsEditable(true);
    knob.onValueChange = [this] { Push(); };
    label.setBorderSize(juce::BorderSize<int>(0, kAlignInset, 0, 0));
    addAndMakeVisible(label);
    addAndMakeVisible(knob);
  };
  init_knob(velocity_, velocity_label_, 1, kDefaultFixedVelocity);

  trigger_reserve_.onClick = [this] { Push(); };
  addAndMakeVisible(trigger_reserve_);

  const char* mix_headings[2] = {"Layer A", "Layer B"};
  for (size_t l = 0; l < mix_.size(); ++l) {
    MixControls& m = mix_[l];
    m.heading.setText(mix_headings[l], juce::dontSendNotification);
    m.heading.setBorderSize(juce::BorderSize<int>(0, kAlignInset, 0, 0));
    addAndMakeVisible(m.heading);
    auto init_mix_knob = [this](juce::Slider& knob,
                                juce::Label& label,
                                ControlMode mode,
                                double default_value) {
      knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
      if (mode == ControlMode::kDecibels) {
        knob.setRange(kMinVolumeDb, kMaxVolumeDb, 0.1);
        knob.setNumDecimalPlacesToDisplay(1);
      } else {
        knob.setRange(0, 127, 1);
      }
      knob.setDoubleClickReturnValue(true, default_value);
      knob.setTextBoxStyle(
          juce::Slider::TextBoxBelow, false, kMixKnobSize, kKnobTextHeight);
      knob.setTextBoxIsEditable(true);
      knob.onValueChange = [this] { Push(); };
      label.setJustificationType(juce::Justification::centred);
      label.setFont(juce::FontOptions(12.0f));
      addAndMakeVisible(label);
      addAndMakeVisible(knob);
    };
    init_mix_knob(m.volume,
                  m.volume_label,
                  ControlMode::kDecibels,
                  kDefaultLayerVolumeDb10 / 10.0);
    init_mix_knob(
        m.fade_in, m.fade_label, ControlMode::kLinear, kDefaultLayerFadeIn);
    init_mix_knob(
        m.decay, m.decay_label, ControlMode::kLinear, kDefaultLayerDecay);
  }

  pedal_heading_.setBorderSize(juce::BorderSize<int>(0, kAlignInset, 0, 0));
  addAndMakeVisible(pedal_heading_);
  init_knob(volume_, volume_label_, 0, kDefaultHiHatVolume);
  init_knob(fade_in_, fade_in_label_, 0, kDefaultHiHatFadeIn);
  init_knob(decay_, decay_label_, 0, kDefaultHiHatDecay);

  setSize(kPanelWidth, kBaseHeight);
}

void PadSettingsPanel::SetParams(const PadParams& params) {
  dynamics_.setToggleState(params.dynamics, juce::dontSendNotification);
  curve_.setSelectedId(static_cast<int>(params.curve) + 1,
                       juce::dontSendNotification);
  velocity_.setValue(params.fixed_velocity, juce::dontSendNotification);
  trigger_reserve_.setToggleState(params.trigger_reserve,
                                  juce::dontSendNotification);
  const PadParams::LayerMix* mixes[2] = {&params.mix_top, &params.mix_bottom};
  for (size_t l = 0; l < mix_.size(); ++l) {
    mix_[l].volume.setValue(mixes[l]->volume_db10 / 10.0,
                            juce::dontSendNotification);
    mix_[l].fade_in.setValue(mixes[l]->fade_in, juce::dontSendNotification);
    mix_[l].decay.setValue(mixes[l]->decay, juce::dontSendNotification);
  }
  volume_.setValue(params.hi_hat_volume, juce::dontSendNotification);
  fade_in_.setValue(params.hi_hat_fade_in, juce::dontSendNotification);
  decay_.setValue(params.hi_hat_decay, juce::dontSendNotification);
  RefreshEnablement();

  show_pedal_ = params.mode == LayerMode::kHiHat;
  for (juce::Component* c : {static_cast<juce::Component*>(&pedal_heading_),
                             static_cast<juce::Component*>(&volume_label_),
                             static_cast<juce::Component*>(&volume_),
                             static_cast<juce::Component*>(&fade_in_label_),
                             static_cast<juce::Component*>(&fade_in_),
                             static_cast<juce::Component*>(&decay_label_),
                             static_cast<juce::Component*>(&decay_)}) {
    c->setVisible(show_pedal_);
  }
  // The CallOutBox tracks content size, so growing for the pedal
  // section repositions the box too.
  setSize(kPanelWidth, kBaseHeight + (show_pedal_ ? kPedalHeight : 0));
}

void PadSettingsPanel::resized() {
  // Knobs go on the left, aligned with the other controls; the label
  // follows on the right, vertically centred on the knob. The slider
  // gets exactly the dial's width — any extra and the dial drifts
  // toward the centre of its bounds — and is shifted left by the
  // margin LookAndFeel_V4 leaves around the dial, so the visible
  // circle (not the widget bounds) is what left-aligns.
  auto knob_row = [](juce::Rectangle<int>& area,
                     juce::Slider& knob,
                     juce::Label& label) {
    auto row = area.removeFromTop(kKnobHeight);
    row.removeFromLeft(kAlignInset);
    knob.setBounds(
        row.removeFromLeft(kKnobSize).translated(-kV4RotaryMargin, 0));
    label.setBounds(row.withHeight(kRowHeight)
                        .withY(row.getY() + (kKnobHeight - kRowHeight) / 2));
    area.removeFromTop(kRowGap);
  };

  auto area = getLocalBounds().reduced(kPadding);
  dynamics_.setBounds(area.removeFromTop(kRowHeight));
  area.removeFromTop(kRowGap);
  auto curve_row = area.removeFromTop(kRowHeight);
  curve_label_.setBounds(curve_row.removeFromLeft(kLabelWidth));
  curve_.setBounds(curve_row);
  area.removeFromTop(kRowGap);
  knob_row(area, velocity_, velocity_label_);
  trigger_reserve_.setBounds(area.removeFromTop(kRowHeight));

  // The two layer-mix triples: heading, then three knobs abreast with
  // their small labels above.
  for (MixControls& m : mix_) {
    area.removeFromTop(kRowGap);
    m.heading.setBounds(area.removeFromTop(kRowHeight));
    auto labels = area.removeFromTop(kMixLabelHeight);
    auto knobs = area.removeFromTop(kMixKnobHeight);
    const int third = knobs.getWidth() / 3;
    juce::Slider* sliders[3] = {&m.volume, &m.fade_in, &m.decay};
    juce::Label* names[3] = {&m.volume_label, &m.fade_label, &m.decay_label};
    for (int i = 0; i < 3; ++i) {
      const auto cell_label = labels.removeFromLeft(third);
      auto cell = knobs.removeFromLeft(third);
      names[i]->setBounds(cell_label);
      sliders[i]->setBounds(cell.withWidth(kMixKnobSize).withX(
          cell.getCentreX() - kMixKnobSize / 2));
    }
  }

  if (!show_pedal_) {
    return;
  }
  area.removeFromTop(kRowGap);
  pedal_heading_.setBounds(area.removeFromTop(kRowHeight));
  area.removeFromTop(kRowGap);
  knob_row(area, volume_, volume_label_);
  knob_row(area, fade_in_, fade_in_label_);
  knob_row(area, decay_, decay_label_);
}

void PadSettingsPanel::Push() {
  if (on_change == nullptr) {
    return;
  }
  PadParams params;
  params.dynamics = dynamics_.getToggleState();
  params.curve = static_cast<DynamicsCurve>(curve_.getSelectedId() - 1);
  params.fixed_velocity = static_cast<int>(velocity_.getValue());
  params.trigger_reserve = trigger_reserve_.getToggleState();
  params.hi_hat_volume = static_cast<int>(volume_.getValue());
  params.hi_hat_fade_in = static_cast<int>(fade_in_.getValue());
  params.hi_hat_decay = static_cast<int>(decay_.getValue());
  PadParams::LayerMix* mixes[2] = {&params.mix_top, &params.mix_bottom};
  for (size_t l = 0; l < mix_.size(); ++l) {
    mixes[l]->volume_db10 =
        static_cast<int>(std::lround(mix_[l].volume.getValue() * 10.0));
    mixes[l]->fade_in = static_cast<int>(mix_[l].fade_in.getValue());
    mixes[l]->decay = static_cast<int>(mix_[l].decay.getValue());
  }
  on_change(params);
}

void PadSettingsPanel::RefreshEnablement() {
  const bool dynamics = dynamics_.getToggleState();
  curve_.setEnabled(dynamics);
  curve_label_.setEnabled(dynamics);
  velocity_.setEnabled(!dynamics);
  velocity_label_.setEnabled(!dynamics);
}

}  // namespace spdsx
