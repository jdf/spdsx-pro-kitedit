#include "pad_settings.h"

#include "layers.h"

namespace spdsx {

namespace {

constexpr int kPanelWidth = 210;
constexpr int kPadding = 12;
constexpr int kRowHeight = 24;
constexpr int kRowGap = 6;
constexpr int kKnobHeight = 78;
constexpr int kLabelWidth = 60;

}  // namespace

PadSettingsPanel::PadSettingsPanel()
{
  dynamics_.onClick = [this]
  {
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
  curve_label_.setBorderSize(juce::BorderSize<int>(0, 4, 0, 0));
  addAndMakeVisible(curve_label_);
  addAndMakeVisible(curve_);

  velocity_.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
  velocity_.setRange(1, 127, 1);
  // The value box doubles as direct entry: click the number and type.
  velocity_.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 48, 18);
  velocity_.setTextBoxIsEditable(true);
  velocity_.onValueChange = [this] { Push(); };
  velocity_label_.setBorderSize(juce::BorderSize<int>(0, 4, 0, 0));
  addAndMakeVisible(velocity_label_);
  addAndMakeVisible(velocity_);

  trigger_reserve_.onClick = [this] { Push(); };
  addAndMakeVisible(trigger_reserve_);

  setSize(kPanelWidth, kPadding * 2 + kRowHeight * 3 + kRowGap * 3
      + kKnobHeight);
}

void PadSettingsPanel::SetParams(const PadParams& params)
{
  dynamics_.setToggleState(params.dynamics, juce::dontSendNotification);
  curve_.setSelectedId(
      static_cast<int>(params.curve) + 1, juce::dontSendNotification);
  velocity_.setValue(params.fixed_velocity, juce::dontSendNotification);
  trigger_reserve_.setToggleState(
      params.trigger_reserve, juce::dontSendNotification);
  RefreshEnablement();
}

void PadSettingsPanel::resized()
{
  auto area = getLocalBounds().reduced(kPadding);
  dynamics_.setBounds(area.removeFromTop(kRowHeight));
  area.removeFromTop(kRowGap);
  auto curve_row = area.removeFromTop(kRowHeight);
  curve_label_.setBounds(curve_row.removeFromLeft(kLabelWidth));
  curve_.setBounds(curve_row);
  area.removeFromTop(kRowGap);
  // Knob on the left, aligned with the other controls; its label
  // follows on the right, vertically centred on the knob.
  auto knob_row = area.removeFromTop(kKnobHeight);
  velocity_.setBounds(knob_row.removeFromLeft(84));
  velocity_label_.setBounds(knob_row.withHeight(kRowHeight)
          .withY(knob_row.getY() + (kKnobHeight - kRowHeight) / 2));
  area.removeFromTop(kRowGap);
  trigger_reserve_.setBounds(area.removeFromTop(kRowHeight));
}

void PadSettingsPanel::Push()
{
  if (on_change == nullptr) {
    return;
  }
  PadParams params;
  params.dynamics = dynamics_.getToggleState();
  params.curve = static_cast<DynamicsCurve>(curve_.getSelectedId() - 1);
  params.fixed_velocity = static_cast<int>(velocity_.getValue());
  params.trigger_reserve = trigger_reserve_.getToggleState();
  on_change(params);
}

void PadSettingsPanel::RefreshEnablement()
{
  const bool dynamics = dynamics_.getToggleState();
  curve_.setEnabled(dynamics);
  curve_label_.setEnabled(dynamics);
  velocity_.setEnabled(!dynamics);
  velocity_label_.setEnabled(!dynamics);
}

}  // namespace spdsx
