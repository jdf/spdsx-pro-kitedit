#include "pad_settings.h"

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

constexpr int kBaseHeight =
    kPadding * 2 + kRowHeight * 3 + kRowGap * 3 + kKnobHeight;
constexpr int kPedalHeight = kRowGap + kRowHeight + (kRowGap + kKnobHeight) * 3;

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
  // number and type.
  auto init_knob = [this](juce::Slider& knob, juce::Label& label, int min) {
    knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    knob.setRange(min, 127, 1);
    knob.setTextBoxStyle(
        juce::Slider::TextBoxBelow, false, 48, kKnobTextHeight);
    knob.setTextBoxIsEditable(true);
    knob.onValueChange = [this] { Push(); };
    label.setBorderSize(juce::BorderSize<int>(0, kAlignInset, 0, 0));
    addAndMakeVisible(label);
    addAndMakeVisible(knob);
  };
  init_knob(velocity_, velocity_label_, 1);

  trigger_reserve_.onClick = [this] { Push(); };
  addAndMakeVisible(trigger_reserve_);

  pedal_heading_.setBorderSize(juce::BorderSize<int>(0, kAlignInset, 0, 0));
  addAndMakeVisible(pedal_heading_);
  init_knob(volume_, volume_label_, 0);
  init_knob(fade_in_, fade_in_label_, 0);
  init_knob(decay_, decay_label_, 0);

  setSize(kPanelWidth, kBaseHeight);
}

void PadSettingsPanel::SetParams(const PadParams& params) {
  dynamics_.setToggleState(params.dynamics, juce::dontSendNotification);
  curve_.setSelectedId(static_cast<int>(params.curve) + 1,
                       juce::dontSendNotification);
  velocity_.setValue(params.fixed_velocity, juce::dontSendNotification);
  trigger_reserve_.setToggleState(params.trigger_reserve,
                                  juce::dontSendNotification);
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
