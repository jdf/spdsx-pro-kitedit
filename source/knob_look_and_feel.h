// A LookAndFeel that draws every rotary slider as the skeuomorphic
// knob image (assets/knob.png, embedded as binary data): the bitmap is
// rotated to the slider's angle and blitted. The image's indicator
// sits at exactly 0 radians (3 o'clock), so the rotation transform is
// the slider angle minus a quarter turn (JUCE measures rotary angles
// clockwise from 12 o'clock). Everything else inherits LookAndFeel_V4.
#ifndef SPDSX_PATCHEDIT_SOURCE_KNOB_LOOK_AND_FEEL_H_
#define SPDSX_PATCHEDIT_SOURCE_KNOB_LOOK_AND_FEEL_H_

#include <juce_gui_basics/juce_gui_basics.h>

namespace spdsx {

class KnobLookAndFeel : public juce::LookAndFeel_V4 {
public:
  KnobLookAndFeel();

  void drawRotarySlider(juce::Graphics& g,
                        int x,
                        int y,
                        int width,
                        int height,
                        float slider_pos,
                        float rotary_start_angle,
                        float rotary_end_angle,
                        juce::Slider& slider) override;

  // A ToggleButton in a radio group draws as a proper radio — a circle
  // with a filled dot — instead of V4's square-and-tick checkbox.
  void drawTickBox(juce::Graphics& g,
                   juce::Component& component,
                   float x,
                   float y,
                   float w,
                   float h,
                   bool ticked,
                   bool is_enabled,
                   bool should_draw_button_as_highlighted,
                   bool should_draw_button_as_down) override;

private:
  juce::Image knob_;
};

}  // namespace spdsx

#endif  // SPDSX_PATCHEDIT_SOURCE_KNOB_LOOK_AND_FEEL_H_
