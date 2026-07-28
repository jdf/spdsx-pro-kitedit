// The selection's settings panel, hosted in the window's right-side
// Properties panel: a title strip naming the selection, then real
// controls for everything about how a pad responds to a hit — layer
// type, fade knobs, the Dynamics section (a radio pair: velocity
// through a curve, or a fixed velocity), the link group, the per-layer
// mixes, and (for HI-HAT objects) the closed-pedal shaping.
// Checkboxes/radios for choices, knobs (with a click-to-type value box)
// for scalars. Multiple objects can be selected at once: a field they
// disagree on renders as a dash (knobs add one green dot per distinct
// value), and touching a control writes that one field to the whole
// selection. Pure view: set_title/SetParams in, on_change out with the
// touched field and the edited PadParams.
#ifndef SPDSX_PATCHEDIT_SOURCE_PAD_SETTINGS_H_
#define SPDSX_PATCHEDIT_SOURCE_PAD_SETTINGS_H_

#include <array>
#include <functional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "kit_model.h"
#include "knob_look_and_feel.h"

namespace spdsx {

class PadSettingsPanel : public juce::Component {
public:
  PadSettingsPanel();
  ~PadSettingsPanel() override;

  // Fires on every edit with the field the touched control governs and
  // the edited PadParams; the receiver applies just that field to each
  // selected object.
  std::function<void(PadParamsField, const PadParams&)> on_change;

  // The title strip's text ("Pad 3", "Trigger 5", "Selected Pads").
  void set_title(const juce::String& title);

  // The hosting tab's width; header bands span it edge to edge and the
  // panel resizes to fill it.
  void set_content_width(int width);

  // Updates the controls without firing on_change (initial state, and
  // refreshes when undo/redo changes the objects underneath the panel).
  // The first entry is the selection's anchor and speaks for uniform
  // fields; a field the selection disagrees on shows as mixed. Shows or
  // hides the fade knobs and the closed-pedal section by the layer
  // modes present, resizing the panel (the hosting viewport scrolls).
  void SetParams(const std::vector<PadParams>& selection);

  void resized() override;

private:
  void Push(PadParamsField field);
  // Selecting the Curve radio is dynamics on; Fixed Velocity is off.
  // Grey out whichever control is dormant (with the selection mixed on
  // dynamics, neither is).
  void RefreshEnablement();
  // Applies the mode-dependent section visibility and the panel height.
  void RefreshSections();
  // Shows the values a scalar control holds across the selection: the
  // anchor's value, plus the mixed marker (dashed box, multi-dot dial)
  // when they differ.
  static void SetKnobValues(juce::Slider& knob,
                            const std::vector<double>& values);

  // The last selection set; the anchor's params_ is the base every Push
  // starts from, so fields this panel has no control for (trigger
  // reserve) ride through unchanged.
  std::vector<PadParams> selection_ {PadParams {}};
  PadParams params_;
  bool dynamics_mixed_ = false;

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
