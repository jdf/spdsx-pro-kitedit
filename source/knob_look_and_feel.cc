#include "knob_look_and_feel.h"

#include "BinaryData.h"

namespace spdsx {

KnobLookAndFeel::KnobLookAndFeel()
    : knob_(juce::ImageCache::getFromMemory(BinaryData::knob_png,
                                            BinaryData::knob_pngSize)) {}

void KnobLookAndFeel::drawRotarySlider(juce::Graphics& g,
                                       int x,
                                       int y,
                                       int width,
                                       int height,
                                       float slider_pos,
                                       float rotary_start_angle,
                                       float rotary_end_angle,
                                       juce::Slider& slider) {
  if (!knob_.isValid()) {
    LookAndFeel_V4::drawRotarySlider(g,
                                     x,
                                     y,
                                     width,
                                     height,
                                     slider_pos,
                                     rotary_start_angle,
                                     rotary_end_angle,
                                     slider);
    return;
  }
  const float angle =
      rotary_start_angle + slider_pos * (rotary_end_angle - rotary_start_angle);
  // JUCE rotary angles run clockwise from 12 o'clock; the image's
  // indicator points at 3 o'clock, a quarter turn ahead.
  const float rotation = angle - juce::MathConstants<float>::halfPi;
  const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat();
  const float size = juce::jmin(bounds.getWidth(), bounds.getHeight());
  const float scale = size / static_cast<float>(knob_.getWidth());
  const auto transform =
      juce::AffineTransform::rotation(
          rotation, knob_.getWidth() * 0.5f, knob_.getHeight() * 0.5f)
          .scaled(scale)
          .translated(bounds.getCentreX() - size * 0.5f,
                      bounds.getCentreY() - size * 0.5f);
  g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
  g.setOpacity(slider.isEnabled() ? 1.0f : 0.4f);
  g.drawImageTransformed(knob_, transform);
}

}  // namespace spdsx
