#include "pad_settings.h"

#include <algorithm>
#include <cmath>

#include "layers.h"

namespace spdsx {

namespace {

// The mixed-state marker a multi-selection shows where values differ.
const juce::String MixedDash() {
  return juce::String::fromUTF8("—");
}

constexpr int kDefaultPanelWidth = 210;
constexpr int kPadding = 12;
constexpr int kTitleHeight = 28;
constexpr int kHeaderHeight = 24;
constexpr int kRowHeight = 24;
constexpr int kRowGap = 6;  // horizontal spacing within a row
// The one precise amount of clear vertical space between any two lines
// of controls (and between a section header and its first line).
constexpr int kControlGap = 12;
// The Layer A/B captions sit tight against their own knobs.
constexpr int kCaptionGap = 2;
constexpr int kSectionGap = 16;
constexpr int kKnobTextHeight = 18;
constexpr int kKnobSize = 60;  // the dial square, textbox below it
constexpr int kKnobHeight = kKnobSize + kKnobTextHeight;
// The LookAndFeel draws checkbox squares at a small inset; labels and
// the knobs get the same nudge so everything left-aligns.
constexpr int kAlignInset = 4;
// The small knobs used in triples/pairs so they fit abreast.
constexpr int kMixLabelHeight = 16;
constexpr int kMixKnobSize = 48;
constexpr int kMixKnobHeight = kMixKnobSize + kKnobTextHeight;

// The gray band behind the title and each section header.
constexpr juce::uint32 kBandColour = 0xff2d333b;

// The dB range the volume knob offers; the device stores 0.1 dB steps in
// a signed 16-bit, so this is a UI choice, not a protocol limit.
constexpr double kMinVolumeDb = -60.0;
constexpr double kMaxVolumeDb = 12.0;

// What a knob's value means, and therefore its range and display: a plain
// 0-127 device value, or decibels with a tenth's precision.
enum class ControlMode {
  kLinear,
  kDecibels
};

}  // namespace

PadSettingsPanel::PadSettingsPanel() {
  // Every rotary in the panel becomes the bitmap knob.
  setLookAndFeel(&knob_look_);

  // The full-width gray bands: the object's name on top, then one
  // header per section.
  auto init_band = [this](juce::Label& label,
                          float font_size,
                          juce::uint32 text_colour) {
    label.setFont(juce::Font(juce::FontOptions(font_size)).boldened());
    label.setColour(juce::Label::textColourId, juce::Colour(text_colour));
    label.setColour(juce::Label::backgroundColourId, juce::Colour(kBandColour));
    label.setBorderSize(juce::BorderSize<int>(0, kPadding + kAlignInset, 0, 0));
    addAndMakeVisible(label);
  };
  init_band(title_, 15.0f, 0xffc9d1d9);
  // The section headers wear a subtle greenish tint.
  for (juce::Label* header : {&mode_header_,
                              &dynamics_header_,
                              &link_header_,
                              &envelopes_header_,
                              &pedal_header_}) {
    init_band(*header, 14.0f, 0xffb5d0bb);
  }

  for (int m = 0; m < kLayerModeCount; ++m) {
    const auto name = LayerModeName(static_cast<LayerMode>(m));
    // Item ids are mode + 1; 0 means "nothing selected" to ComboBox.
    mode_.addItem(juce::String(name.data(), name.size()), m + 1);
  }
  // "Nothing selected" is how a combo shows a mixed multi-selection.
  mode_.setTextWhenNothingSelected(MixedDash());
  mode_.onChange = [this] {
    if (mode_.getSelectedId() > 0) {
      Push(PadParamsField::kMode);
    }
  };
  addAndMakeVisible(mode_);

  // The mixed-state plumbing every scalar control gets: the value box
  // shows a dash while the selection disagrees, confirming the dash (or
  // an empty box) keeps the value, and grabbing the dial resolves the
  // display to the value being dragged.
  auto init_mixed_text = [](juce::Slider& knob, int decimals) {
    knob.textFromValueFunction = [&knob, decimals](double value) {
      if (knob.getProperties().contains(KnobLookAndFeel::kMixedValues)) {
        return MixedDash();
      }
      return decimals > 0 ? juce::String(value, decimals)
                          : juce::String(juce::roundToInt(value));
    };
    knob.valueFromTextFunction = [&knob](const juce::String& text) {
      const auto trimmed = text.trim();
      if (trimmed.isEmpty() || trimmed == MixedDash()) {
        return knob.getValue();
      }
      return trimmed.getDoubleValue();
    };
    knob.onDragStart = [&knob] {
      if (knob.getProperties().contains(KnobLookAndFeel::kMixedValues)) {
        knob.getProperties().remove(KnobLookAndFeel::kMixedValues);
        knob.updateText();
      }
    };
  };

  // The small-knob style shared by the fade pair and the mix triples:
  // a dial with its label above and a type-in value box below.
  auto init_small_knob = [this, &init_mixed_text](juce::Slider& knob,
                                                  juce::Label& label,
                                                  ControlMode control,
                                                  double default_value,
                                                  PadParamsField field) {
    knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    if (control == ControlMode::kDecibels) {
      knob.setRange(kMinVolumeDb, kMaxVolumeDb, 0.1);
      knob.setNumDecimalPlacesToDisplay(1);
    } else {
      knob.setRange(0, 127, 1);
    }
    knob.setDoubleClickReturnValue(true, default_value);
    knob.setTextBoxStyle(
        juce::Slider::TextBoxBelow, false, kMixKnobSize, kKnobTextHeight);
    // One undo step per adjustment, not one per drag pixel —
    // otherwise a drag leaves a pile of transactions and a single
    // undo can never get back to clean.
    knob.setChangeNotificationOnlyOnRelease(true);
    knob.setTextBoxIsEditable(true);
    init_mixed_text(knob, control == ControlMode::kDecibels ? 1 : 0);
    knob.onValueChange = [this, field] { Push(field); };
    label.setJustificationType(juce::Justification::centred);
    label.setFont(juce::FontOptions(12.0f));
    addAndMakeVisible(label);
    addAndMakeVisible(knob);
  };

  init_small_knob(fade_point_,
                  fade_point_label_,
                  ControlMode::kLinear,
                  kDefaultFadePoint,
                  PadParamsField::kFadePoint);
  fade_point_.setRange(1, 127, 1);
  init_small_knob(fade_end_,
                  fade_end_label_,
                  ControlMode::kLinear,
                  kDefaultFadeEnd,
                  PadParamsField::kFadeEnd);
  // The end can't go below the point; SetParams keeps the range's floor
  // at the current fade point so drags stop there.
  fade_end_.setRange(1, 127, 1);

  // The radio pair: hits play through the curve (dynamics on), or at
  // one fixed velocity (dynamics off). JUCE never click-deselects a
  // radio-grouped button, so only the newly selected one acts; the
  // loser's onClick also fires (as the group turns it off) and must do
  // nothing, or it would fight the selection.
  auto init_radio = [this](juce::ToggleButton& radio) {
    radio.setRadioGroupId(1);
    radio.onClick = [this, &radio] {
      if (radio.getToggleState()) {
        dynamics_mixed_ = false;
        RefreshEnablement();
        Push(PadParamsField::kDynamics);
      }
    };
    addAndMakeVisible(radio);
  };
  init_radio(curve_radio_);
  init_radio(velocity_radio_);

  for (int c = 0; c < kDynamicsCurveCount; ++c) {
    const auto name = DynamicsCurveName(static_cast<DynamicsCurve>(c));
    curve_.addItem(juce::String(name.data(), name.size()), c + 1);
  }
  curve_.setTextWhenNothingSelected(MixedDash());
  curve_.onChange = [this] {
    if (curve_.getSelectedId() > 0) {
      Push(PadParamsField::kCurve);
    }
  };
  addAndMakeVisible(curve_);

  velocity_.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
  velocity_.setRange(1, 127, 1);
  velocity_.setDoubleClickReturnValue(true, kDefaultFixedVelocity);
  velocity_.setTextBoxStyle(
      juce::Slider::TextBoxBelow, false, 48, kKnobTextHeight);
  // One undo step per adjustment, not one per drag pixel —
  // otherwise a drag leaves a pile of transactions and a single
  // undo can never get back to clean.
  velocity_.setChangeNotificationOnlyOnRelease(true);
  velocity_.setTextBoxIsEditable(true);
  init_mixed_text(velocity_, 0);
  velocity_.onValueChange = [this] { Push(PadParamsField::kFixedVelocity); };
  addAndMakeVisible(velocity_);

  // The directional link pair: send fires the group, receive gets
  // fired by it. 0 = none.
  auto init_link = [this, &init_mixed_text](juce::Slider& stepper,
                                            juce::Label& label,
                                            PadParamsField field) {
    stepper.setRange(0, 32, 1);
    stepper.setSliderStyle(juce::Slider::IncDecButtons);
    stepper.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 48, 22);
    stepper.setTextBoxIsEditable(true);
    stepper.setChangeNotificationOnlyOnRelease(true);
    init_mixed_text(stepper, 0);
    stepper.onValueChange = [this, field] { Push(field); };
    label.setBorderSize(juce::BorderSize<int>(0, kAlignInset, 0, 0));
    addAndMakeVisible(label);
    addAndMakeVisible(stepper);
  };
  init_link(link_send_, link_send_label_, PadParamsField::kLinkSend);
  init_link(link_receive_, link_receive_label_, PadParamsField::kLinkReceive);
  pad_link_hint_.setFont(juce::FontOptions(12.0f));
  pad_link_hint_.setColour(juce::Label::textColourId, juce::Colour(0xff8b949e));
  addAndMakeVisible(pad_link_hint_);

  const char* mix_headings[2] = {"Layer A", "Layer B"};
  const PadParamsField mix_fields[2][3] = {{PadParamsField::kMixTopVolume,
                                            PadParamsField::kMixTopFadeIn,
                                            PadParamsField::kMixTopDecay},
                                           {PadParamsField::kMixBottomVolume,
                                            PadParamsField::kMixBottomFadeIn,
                                            PadParamsField::kMixBottomDecay}};
  for (size_t l = 0; l < mix_.size(); ++l) {
    MixControls& m = mix_[l];
    m.heading.setText(mix_headings[l], juce::dontSendNotification);
    m.heading.setBorderSize(juce::BorderSize<int>(0, kAlignInset, 0, 0));
    addAndMakeVisible(m.heading);
    init_small_knob(m.volume,
                    m.volume_label,
                    ControlMode::kDecibels,
                    kDefaultLayerVolumeDb10 / 10.0,
                    mix_fields[l][0]);
    init_small_knob(m.fade_in,
                    m.fade_label,
                    ControlMode::kLinear,
                    kDefaultLayerFadeIn,
                    mix_fields[l][1]);
    init_small_knob(m.decay,
                    m.decay_label,
                    ControlMode::kLinear,
                    kDefaultLayerDecay,
                    mix_fields[l][2]);
  }

  // A knob with its label to the right, for the pedal section.
  auto init_knob = [this, &init_mixed_text](juce::Slider& knob,
                                            juce::Label& label,
                                            int min,
                                            int default_value,
                                            PadParamsField field) {
    knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    knob.setRange(min, 127, 1);
    knob.setDoubleClickReturnValue(true, default_value);
    knob.setTextBoxStyle(
        juce::Slider::TextBoxBelow, false, 48, kKnobTextHeight);
    // One undo step per adjustment, not one per drag pixel —
    // otherwise a drag leaves a pile of transactions and a single
    // undo can never get back to clean.
    knob.setChangeNotificationOnlyOnRelease(true);
    knob.setTextBoxIsEditable(true);
    init_mixed_text(knob, 0);
    knob.onValueChange = [this, field] { Push(field); };
    label.setBorderSize(juce::BorderSize<int>(0, kAlignInset, 0, 0));
    addAndMakeVisible(label);
    addAndMakeVisible(knob);
  };
  init_knob(volume_,
            volume_label_,
            0,
            kDefaultHiHatVolume,
            PadParamsField::kHiHatVolume);
  init_knob(fade_in_,
            fade_in_label_,
            0,
            kDefaultHiHatFadeIn,
            PadParamsField::kHiHatFadeIn);
  init_knob(
      decay_, decay_label_, 0, kDefaultHiHatDecay, PadParamsField::kHiHatDecay);

  RefreshSections();
}

PadSettingsPanel::~PadSettingsPanel() {
  setLookAndFeel(nullptr);
}

void PadSettingsPanel::set_title(const juce::String& title) {
  title_.setText(title, juce::dontSendNotification);
}

void PadSettingsPanel::set_content_width(int width) {
  if (width > 0 && width != content_width_) {
    content_width_ = width;
    RefreshSections();
  }
}

void PadSettingsPanel::SetKnobValues(juce::Slider& knob,
                                     const std::vector<double>& values) {
  juce::Array<juce::var> distinct;
  for (double v : values) {
    if (!distinct.contains(juce::var(v))) {
      distinct.add(juce::var(v));
    }
  }
  knob.setValue(values.front(), juce::dontSendNotification);
  if (distinct.size() > 1) {
    knob.getProperties().set(KnobLookAndFeel::kMixedValues, distinct);
  } else {
    knob.getProperties().remove(KnobLookAndFeel::kMixedValues);
  }
  knob.updateText();
  knob.repaint();
}

void PadSettingsPanel::SetParams(const std::vector<PadParams>& selection) {
  jassert(!selection.empty());
  selection_ = selection;
  const PadParams& params = selection.front();
  params_ = params;

  // Per-field values across the selection, anchor first.
  auto values = [this](auto get) {
    std::vector<double> v;
    v.reserve(selection_.size());
    for (const PadParams& p : selection_) {
      v.push_back(static_cast<double>(get(p)));
    }
    return v;
  };
  auto mixed = [&values](auto get) {
    const auto v = values(get);
    return std::any_of(
        v.begin(), v.end(), [&v](double x) { return x != v.front(); });
  };

  // Combos say "mixed" by selecting nothing (the dash placeholder).
  mode_.setSelectedId(
      mixed([](const PadParams& p) { return static_cast<int>(p.mode); })
          ? 0
          : static_cast<int>(params.mode) + 1,
      juce::dontSendNotification);
  SetKnobValues(fade_point_,
                values([](const PadParams& p) { return p.fade_point; }));
  // The end can't go below the point; with a mixed selection the floor
  // is the lowest point present (each pad still clamps on apply).
  int min_point = 127;
  for (const PadParams& p : selection_) {
    min_point = std::min(min_point, p.fade_point);
  }
  fade_end_.setRange(min_point, 127, 1);
  SetKnobValues(fade_end_,
                values([](const PadParams& p) { return p.fade_end; }));
  // A selection mixed on dynamics shows NEITHER radio selected; the
  // first radio click unifies it.
  dynamics_mixed_ = mixed([](const PadParams& p) { return p.dynamics; });
  curve_radio_.setToggleState(!dynamics_mixed_ && params.dynamics,
                              juce::dontSendNotification);
  velocity_radio_.setToggleState(!dynamics_mixed_ && !params.dynamics,
                                 juce::dontSendNotification);
  curve_.setSelectedId(
      mixed([](const PadParams& p) { return static_cast<int>(p.curve); })
          ? 0
          : static_cast<int>(params.curve) + 1,
      juce::dontSendNotification);
  SetKnobValues(velocity_,
                values([](const PadParams& p) { return p.fixed_velocity; }));
  SetKnobValues(link_send_,
                values([](const PadParams& p) { return p.link_send; }));
  SetKnobValues(link_receive_,
                values([](const PadParams& p) { return p.link_receive; }));
  auto layer_mix = [](const PadParams& p, size_t l) -> const auto& {
    return l == 0 ? p.mix_top : p.mix_bottom;
  };
  for (size_t l = 0; l < mix_.size(); ++l) {
    SetKnobValues(mix_[l].volume, values([&layer_mix, l](const PadParams& p) {
                    return layer_mix(p, l).volume_db10 / 10.0;
                  }));
    SetKnobValues(mix_[l].fade_in, values([&layer_mix, l](const PadParams& p) {
                    return layer_mix(p, l).fade_in;
                  }));
    SetKnobValues(mix_[l].decay, values([&layer_mix, l](const PadParams& p) {
                    return layer_mix(p, l).decay;
                  }));
  }
  SetKnobValues(volume_,
                values([](const PadParams& p) { return p.hi_hat_volume; }));
  SetKnobValues(fade_in_,
                values([](const PadParams& p) { return p.hi_hat_fade_in; }));
  SetKnobValues(decay_,
                values([](const PadParams& p) { return p.hi_hat_decay; }));
  RefreshEnablement();
  RefreshSections();
}

void PadSettingsPanel::RefreshSections() {
  // A control shows when ANY selected object's mode reads it, so a
  // mixed-mode selection can still edit what applies to part of it.
  bool any_point = false;
  bool any_end = false;
  bool any_pedal = false;
  for (const PadParams& p : selection_) {
    any_point = any_point || UsesFadePoint(p.mode);
    any_end = any_end || UsesFadeEnd(p.mode);
    any_pedal = any_pedal || p.mode == LayerMode::kHiHat;
  }
  show_fades_ = any_point || any_end;
  fade_point_label_.setVisible(any_point);
  fade_point_.setVisible(any_point);
  fade_end_label_.setVisible(any_end);
  fade_end_.setVisible(any_end);

  show_pedal_ = any_pedal;
  for (juce::Component* c : {static_cast<juce::Component*>(&pedal_header_),
                             static_cast<juce::Component*>(&volume_label_),
                             static_cast<juce::Component*>(&volume_),
                             static_cast<juce::Component*>(&fade_in_label_),
                             static_cast<juce::Component*>(&fade_in_),
                             static_cast<juce::Component*>(&decay_label_),
                             static_cast<juce::Component*>(&decay_)}) {
    c->setVisible(show_pedal_);
  }

  // One section = a gap (doubled above every header but the first),
  // its header band, a gap, and its content; every two lines inside
  // the content sit kControlGap apart.
  auto section = [](int content, int gap_above = 2 * kSectionGap) {
    return gap_above + kHeaderHeight + kControlGap + content;
  };
  const int knob_unit = kMixLabelHeight + kMixKnobHeight;
  const int height = kTitleHeight  // the title band
      + section(kRowHeight  // layer type: the combo row...
                    + (show_fades_ ? kControlGap + knob_unit
                                   : 0),  // ...plus fades when it fades
                kSectionGap)
      + section(kKnobHeight + kControlGap + kRowHeight)  // dynamics radios
      + section(2 * kRowHeight + kControlGap)  // link send + receive
      + section(2 * (kRowHeight + kCaptionGap + knob_unit)
                + kControlGap)  // the two layer envelopes
      + (show_pedal_ ? section(3 * kKnobHeight + 2 * kControlGap) : 0)
      + kPadding;
  setSize(content_width_ > 0 ? content_width_ : kDefaultPanelWidth, height);
}

void PadSettingsPanel::resized() {
  // Header bands span the panel edge to edge; content is inset.
  bool first_band = true;
  auto band = [this, &first_band](
                  juce::Rectangle<int>& area, juce::Label& header, int height) {
    // Double breathing room above every header but the first.
    area.removeFromTop(first_band ? kSectionGap : 2 * kSectionGap);
    first_band = false;
    header.setBounds(area.removeFromTop(height).withX(0).withWidth(getWidth()));
    area.removeFromTop(kControlGap);
  };

  // A pair/triple of small knobs abreast at the left edge, each with
  // its label above.
  auto small_knobs = [](juce::Rectangle<int>& area,
                        std::initializer_list<juce::Label*> names,
                        std::initializer_list<juce::Slider*> knobs) {
    auto labels = area.removeFromTop(kMixLabelHeight);
    auto dials = area.removeFromTop(kMixKnobHeight);
    // The same nudge every other control row gets, so the knobs' value
    // boxes (their alignment point) land on the content line.
    labels.removeFromLeft(kAlignInset);
    dials.removeFromLeft(kAlignInset);
    auto name = names.begin();
    auto knob = knobs.begin();
    for (; name != names.end(); ++name, ++knob) {
      (*name)->setBounds(labels.removeFromLeft(kMixKnobSize + kRowGap));
      (*knob)->setBounds(
          dials.removeFromLeft(kMixKnobSize + kRowGap).withWidth(kMixKnobSize));
    }
  };

  auto area = getLocalBounds().reduced(kPadding, 0);
  title_.setBounds(
      area.removeFromTop(kTitleHeight).withX(0).withWidth(getWidth()));

  band(area, mode_header_, kHeaderHeight);
  auto mode_row = area.removeFromTop(kRowHeight);
  mode_row.removeFromLeft(kAlignInset);
  mode_.setBounds(mode_row.removeFromLeft(140));
  if (show_fades_) {
    area.removeFromTop(kControlGap);
    small_knobs(area,
                {&fade_point_label_, &fade_end_label_},
                {&fade_point_, &fade_end_});
  }

  band(area, dynamics_header_, kHeaderHeight);
  auto velocity_row = area.removeFromTop(kKnobHeight);
  // The radio matches the knob's value box exactly — same height, same
  // bottom — so the two texts share a centreline.
  velocity_radio_.setBounds(
      velocity_row.removeFromLeft(120)
          .withHeight(kKnobTextHeight)
          .withY(velocity_row.getBottom() - kKnobTextHeight));
  velocity_.setBounds(velocity_row.removeFromLeft(kKnobSize));
  area.removeFromTop(kControlGap);
  auto curve_row = area.removeFromTop(kRowHeight);
  curve_radio_.setBounds(curve_row.removeFromLeft(76));
  curve_.setBounds(curve_row.removeFromLeft(120));

  band(area, link_header_, kHeaderHeight);
  auto send_row = area.removeFromTop(kRowHeight);
  send_row.removeFromLeft(kAlignInset);
  link_send_label_.setBounds(send_row.removeFromLeft(64));
  link_send_.setBounds(send_row.removeFromLeft(116).reduced(0, 2));
  send_row.removeFromLeft(8);
  pad_link_hint_.setBounds(send_row);
  area.removeFromTop(kControlGap);
  auto receive_row = area.removeFromTop(kRowHeight);
  receive_row.removeFromLeft(kAlignInset);
  link_receive_label_.setBounds(receive_row.removeFromLeft(64));
  link_receive_.setBounds(receive_row.removeFromLeft(116).reduced(0, 2));

  band(area, envelopes_header_, kHeaderHeight);
  for (size_t l = 0; l < mix_.size(); ++l) {
    MixControls& m = mix_[l];
    if (l > 0) {
      area.removeFromTop(kControlGap);
    }
    m.heading.setBounds(area.removeFromTop(kRowHeight));
    // The caption belongs to its knobs; just a sliver of air.
    area.removeFromTop(kCaptionGap);
    small_knobs(area,
                {&m.volume_label, &m.fade_label, &m.decay_label},
                {&m.volume, &m.fade_in, &m.decay});
  }

  if (!show_pedal_) {
    return;
  }
  band(area, pedal_header_, kHeaderHeight);
  bool first_pedal = true;
  auto pedal_knob = [&area, &first_pedal](juce::Slider& knob,
                                          juce::Label& label) {
    if (!first_pedal) {
      area.removeFromTop(kControlGap);
    }
    first_pedal = false;
    auto row = area.removeFromTop(kKnobHeight);
    row.removeFromLeft(kAlignInset);
    knob.setBounds(row.removeFromLeft(kKnobSize));
    // The label matches the knob's value box: same height, same bottom.
    label.setBounds(row.withHeight(kKnobTextHeight)
                        .withY(row.getBottom() - kKnobTextHeight));
  };
  pedal_knob(volume_, volume_label_);
  pedal_knob(fade_in_, fade_in_label_);
  pedal_knob(decay_, decay_label_);
}

void PadSettingsPanel::Push(PadParamsField field) {
  if (on_change == nullptr) {
    return;
  }
  // Only `field` will be applied; the rest is read anyway so the value
  // travels with a complete, coherent PadParams. Combos showing the
  // mixed dash (nothing selected) keep the anchor's value.
  PadParams params = params_;  // uncontrolled fields ride through
  if (mode_.getSelectedId() > 0) {
    params.mode = static_cast<LayerMode>(mode_.getSelectedId() - 1);
  }
  params.fade_point = static_cast<int>(fade_point_.getValue());
  params.fade_end =
      juce::jmax(params.fade_point, static_cast<int>(fade_end_.getValue()));
  if (!dynamics_mixed_) {
    params.dynamics = curve_radio_.getToggleState();
  }
  if (curve_.getSelectedId() > 0) {
    params.curve = static_cast<DynamicsCurve>(curve_.getSelectedId() - 1);
  }
  params.fixed_velocity = static_cast<int>(velocity_.getValue());
  params.link_send = static_cast<int>(link_send_.getValue());
  params.link_receive = static_cast<int>(link_receive_.getValue());
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
  on_change(field, params);
}

void PadSettingsPanel::RefreshEnablement() {
  if (dynamics_mixed_) {
    // Neither radio selected: both dependents stay live, so either can
    // be edited without first unifying the dynamics choice.
    curve_.setEnabled(true);
    velocity_.setEnabled(true);
    return;
  }
  const bool dynamics = curve_radio_.getToggleState();
  curve_.setEnabled(dynamics);
  velocity_.setEnabled(!dynamics);
}

}  // namespace spdsx
