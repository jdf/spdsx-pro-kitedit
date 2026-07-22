// The pad "⋯" settings panel: real controls for the pad properties
// that don't earn a spot in the pad header — dynamics on/off, dynamics
// curve, fixed velocity, trigger reserve, and (for HI-HAT pads) the
// closed-pedal shaping. Checkboxes for booleans, knobs (with a
// click-to-type value box) for scalars. Pure view: SetParams in,
// on_change out; the parent shows it in a CallOutBox.
#ifndef SPDSX_PATCHEDIT_SOURCE_PAD_SETTINGS_H_
#define SPDSX_PATCHEDIT_SOURCE_PAD_SETTINGS_H_

#include <array>
#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "kit_model.h"

namespace spdsx {

class PadSettingsPanel : public juce::Component {
public:
  PadSettingsPanel();

  // Fires on every edit with the panel's own fields filled in; the
  // other PadParams fields are defaults, so take only what this panel
  // owns (dynamics, curve, fixed_velocity, trigger_reserve, the
  // hi_hat_* trio, and the two per-layer mixes).
  std::function<void(const PadParams&)> on_change;

  // Updates the controls without firing on_change (initial state, and
  // refreshes when undo/redo changes the pad underneath the panel).
  // Shows or hides the closed-pedal section by the pad's layer mode,
  // resizing the panel (the CallOutBox follows).
  void SetParams(const PadParams& params);

  void resized() override;

private:
  void Push();
  // Curve only matters with dynamics on, fixed velocity only with it
  // off; grey out whichever is dormant.
  void RefreshEnablement();

  juce::ToggleButton dynamics_ {"Dynamics"};
  juce::Label curve_label_ {{}, "Curve"};
  juce::ComboBox curve_;
  juce::Label velocity_label_ {{}, "Fixed Velocity"};
  juce::Slider velocity_;
  juce::ToggleButton trigger_reserve_ {"Trigger Reserve"};
  // Per-layer mix (Layer A on top, Layer B below): three small knobs
  // side by side under a heading — volume in dB, fade-in, decay.
  struct MixControls {
    juce::Label heading;
    juce::Label volume_label {{}, "Vol"};
    juce::Slider volume;
    juce::Label fade_label {{}, "Fade"};
    juce::Slider fade_in;
    juce::Label decay_label {{}, "Decay"};
    juce::Slider decay;
  };
  std::array<MixControls, 2> mix_;  // [0] = layer A/top, [1] = layer B
  // HI-HAT only: closed-pedal volume/attack/decay knobs.
  juce::Label pedal_heading_ {{}, "Closed Pedal"};
  juce::Label volume_label_ {{}, "Volume"};
  juce::Slider volume_;
  juce::Label fade_in_label_ {{}, "Fade In"};
  juce::Slider fade_in_;
  juce::Label decay_label_ {{}, "Decay"};
  juce::Slider decay_;
  bool show_pedal_ = false;
};

}  // namespace spdsx

#endif  // SPDSX_PATCHEDIT_SOURCE_PAD_SETTINGS_H_
