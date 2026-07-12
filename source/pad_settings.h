// The pad "⋯" settings panel: real controls for the pad properties
// that don't earn a spot in the pad header — dynamics on/off, dynamics
// curve, fixed velocity, trigger reserve. Checkboxes for booleans, a
// knob (with a click-to-type value box) for the scalar. Pure view:
// SetParams in, on_change out; the parent shows it in a CallOutBox.
#ifndef SPDSX_PATCHEDIT_SOURCE_PAD_SETTINGS_H_
#define SPDSX_PATCHEDIT_SOURCE_PAD_SETTINGS_H_

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "kit_model.h"

namespace spdsx {

class PadSettingsPanel : public juce::Component {
public:
  PadSettingsPanel();

  // Fires on every edit with the panel's four fields filled in; the
  // other PadParams fields are defaults, so take only what this panel
  // owns (dynamics, curve, fixed_velocity, trigger_reserve).
  std::function<void(const PadParams&)> on_change;

  // Updates the controls without firing on_change (initial state, and
  // refreshes when undo/redo changes the pad underneath the panel).
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
};

}  // namespace spdsx

#endif  // SPDSX_PATCHEDIT_SOURCE_PAD_SETTINGS_H_
