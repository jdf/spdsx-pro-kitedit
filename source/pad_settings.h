// The selected object's settings panel, hosted in the left panel's
// Properties tab: a title strip naming the object, then real controls
// for everything about how it responds to a hit — layer type, fade
// knobs, the Dynamics section (a radio pair: velocity through a curve,
// or a fixed velocity), the link group, the per-layer mixes, and (for
// HI-HAT objects) the closed-pedal shaping. Checkboxes/radios for
// choices, knobs (with a click-to-type value box) for scalars. Pure
// view: set_title/SetParams in, on_change out with the FULL PadParams
// (fields without controls pass through unchanged).
#ifndef SPDSX_PATCHEDIT_SOURCE_PAD_SETTINGS_H_
#define SPDSX_PATCHEDIT_SOURCE_PAD_SETTINGS_H_

#include <array>
#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "kit_model.h"
#include "knob_look_and_feel.h"

namespace spdsx {

class PadSettingsPanel : public juce::Component {
public:
  PadSettingsPanel();
  ~PadSettingsPanel() override;

  // Fires on every edit with the complete edited PadParams.
  std::function<void(const PadParams&)> on_change;

  // The title strip's text ("Pad 3", "Trigger 5").
  void set_title(const juce::String& title);

  // The hosting tab's width; header bands span it edge to edge and the
  // panel resizes to fill it.
  void set_content_width(int width);

  // Updates the controls without firing on_change (initial state, and
  // refreshes when undo/redo changes the object underneath the panel).
  // Shows or hides the fade knobs and the closed-pedal section by the
  // layer mode, resizing the panel (the hosting viewport scrolls).
  void SetParams(const PadParams& params);

  void resized() override;

private:
  void Push();
  // Selecting the Curve radio is dynamics on; Fixed Velocity is off.
  // Grey out whichever control is dormant.
  void RefreshEnablement();
  // Applies the mode-dependent section visibility and the panel height.
  void RefreshSections();

  // The last params set; fields this panel has no control for (trigger
  // reserve) ride through Push unchanged.
  PadParams params_;

  // Draws every knob below as the skeuomorphic bitmap. Declared before
  // the sliders so it outlives them on destruction.
  KnobLookAndFeel knob_look_;

  int content_width_ = 0;  // 0 = the default width

  // Full-width gray bands: the object's name, then one per section.
  juce::Label title_;
  juce::Label mode_header_ {{}, "Layer Type"};
  juce::Label dynamics_header_ {{}, "Dynamics"};
  juce::Label link_header_ {{}, "Link Group"};
  juce::Label envelopes_header_ {{}, "Layer Envelopes"};

  juce::ComboBox mode_;
  juce::Label fade_point_label_ {{}, "Fade Pt"};
  juce::Slider fade_point_;
  juce::Label fade_end_label_ {{}, "Fade End"};
  juce::Slider fade_end_;

  juce::ToggleButton curve_radio_ {"Curve"};
  juce::ComboBox curve_;
  juce::ToggleButton velocity_radio_ {"Fixed Velocity"};
  juce::Slider velocity_;

  // The directional link pair: this object sends its hits to a group,
  // or receives a group's hits (0 = none).
  juce::Label link_send_label_ {{}, "Send"};
  juce::Slider link_send_;
  juce::Label link_receive_label_ {{}, "Receive"};
  juce::Slider link_receive_;
  juce::Label pad_link_hint_ {{}, "0 = none"};

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
  juce::Label pedal_header_ {{}, "Closed Pedal"};
  juce::Label volume_label_ {{}, "Volume"};
  juce::Slider volume_;
  juce::Label fade_in_label_ {{}, "Fade In"};
  juce::Slider fade_in_;
  juce::Label decay_label_ {{}, "Decay"};
  juce::Slider decay_;

  bool show_fades_ = false;
  bool show_pedal_ = false;
};

}  // namespace spdsx

#endif  // SPDSX_PATCHEDIT_SOURCE_PAD_SETTINGS_H_
