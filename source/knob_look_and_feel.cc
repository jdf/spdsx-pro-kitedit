#include "knob_look_and_feel.h"

#include <cmath>

#include "BinaryData.h"

namespace spdsx {

namespace {

// The knob circle measured from the bitmap's alpha channel (the art is
// not quite centred in its canvas): centre (63.5, 62.0), diameter 112
// of the 128px image. Rotating about the TRUE centre is what keeps the
// knob from wobbling as it turns.
constexpr float kCircleCentreX = 63.5f;
constexpr float kCircleCentreY = 62.0f;
constexpr float kCircleDiameter = 112.0f;

// The value ring: an arc around the knob from the sweep's start to the
// current angle, running dark gray to white and thickening clockwise.
constexpr juce::uint32 kRingDark = 0xff30363d;
constexpr float kRingMinWidth = 1.0f;
constexpr float kRingMaxWidth = 3.0f;

// The bitmap's indicator dot, measured like the circle: centre
// (100, 62) — exactly on the 0-radian line — radius 2.5. The green
// marker drawn over it is bigger, since the baked-in dot reads small.
constexpr float kDotCentreX = 100.0f;
constexpr float kDotCentreY = 62.0f;
constexpr float kDotDrawRadius = 13.5f;  // in image pixels, pre-scale
constexpr juce::uint32 kDotGreen = 0xff3fb950;

}  // namespace

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
  const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat();
  const float size = juce::jmin(bounds.getWidth(), bounds.getHeight());
  const float cx = bounds.getCentreX();
  const float cy = bounds.getCentreY();
  g.setOpacity(slider.isEnabled() ? 1.0f : 0.4f);

  // The ring hugs the dial square's edge; the knob shrinks just enough
  // to sit inside it.
  const float ring_radius = size * 0.5f - kRingMaxWidth * 0.5f;
  const float knob_diameter = size - 2.0f * (kRingMaxWidth + 1.5f);

  // JUCE rotary angles run clockwise from 12 o'clock; the image's
  // indicator points at 3 o'clock, a quarter turn ahead.
  const float rotation = angle - juce::MathConstants<float>::halfPi;
  const float scale = knob_diameter / kCircleDiameter;
  const auto transform =
      juce::AffineTransform::rotation(rotation, kCircleCentreX, kCircleCentreY)
          .scaled(scale)
          .translated(cx - kCircleCentreX * scale, cy - kCircleCentreY * scale);
  g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
  g.drawImageTransformed(knob_, transform);

  // The indicator: a green circle over the bitmap's (too-small) dot,
  // carried to its rotated position by the same transform. The emboss
  // arcs keep a fixed light direction whatever the rotation.
  {
    const float enabled_alpha = slider.isEnabled() ? 1.0f : 0.4f;
    float dot_x = kDotCentreX;
    float dot_y = kDotCentreY;
    transform.transformPoint(dot_x, dot_y);
    const float r = kDotDrawRadius * scale;
    g.setColour(juce::Colour(kDotGreen).withMultipliedAlpha(enabled_alpha));
    g.fillEllipse(dot_x - r, dot_y - r, 2.0f * r, 2.0f * r);
    // A hint of inner emboss: shadow on the top-left inner edge, a
    // faint highlight opposite.
    auto inner_arc = [&](float from, float to, juce::Colour colour) {
      juce::Path arc;
      arc.addCentredArc(
          dot_x, dot_y, r * 0.72f, r * 0.72f, 0.0f, from, to, true);
      g.setColour(colour.withMultipliedAlpha(enabled_alpha));
      g.strokePath(arc,
                   juce::PathStrokeType(r * 0.45f,
                                        juce::PathStrokeType::curved,
                                        juce::PathStrokeType::rounded));
    };
    constexpr float pi = juce::MathConstants<float>::pi;
    inner_arc(1.5f * pi - 0.4f,
              2.0f * pi + 0.4f,
              juce::Colours::black.withAlpha(0.30f));
    inner_arc(0.5f * pi - 0.4f,
              1.0f * pi + 0.4f,
              juce::Colours::white.withAlpha(0.20f));
  }

  // The value arc, in short stroked segments so the colour and the
  // width can both grow along it. A segment's look follows its place
  // in the FULL sweep, so the value reveals more of one fixed ring
  // rather than restyling it.
  const float sweep = rotary_end_angle - rotary_start_angle;
  const float shown = angle - rotary_start_angle;
  const int steps = juce::jmax(2, static_cast<int>(std::ceil(shown / 0.06f)));
  const auto dark = juce::Colour(kRingDark);
  for (int i = 0; i < steps; ++i) {
    const float a0 = rotary_start_angle + shown * i / steps;
    const float a1 = rotary_start_angle + shown * (i + 1) / steps;
    const float mid_frac = (((a0 + a1) * 0.5f) - rotary_start_angle) / sweep;
    juce::Path segment;
    segment.addCentredArc(cx,
                          cy,
                          ring_radius,
                          ring_radius,
                          0.0f,
                          a0,
                          // Overlap the next segment slightly so the
                          // joins don't read as gaps.
                          juce::jmin(a1 + 0.02f, angle),
                          true);
    g.setColour(dark.interpolatedWith(juce::Colours::white, mid_frac)
                    .withAlpha(slider.isEnabled() ? 1.0f : 0.4f));
    g.strokePath(segment,
                 juce::PathStrokeType(
                     kRingMinWidth + (kRingMaxWidth - kRingMinWidth) * mid_frac,
                     juce::PathStrokeType::curved,
                     juce::PathStrokeType::rounded));
  }
}

juce::Slider::SliderLayout KnobLookAndFeel::getSliderLayout(
    juce::Slider& slider) {
  if (slider.isRotary()
      && slider.getTextBoxPosition() == juce::Slider::TextBoxBelow) {
    juce::Slider::SliderLayout layout;
    const int box_w = juce::jmin(slider.getTextBoxWidth(), slider.getWidth());
    const int box_h = slider.getTextBoxHeight();
    layout.textBoxBounds = {0, slider.getHeight() - box_h, box_w, box_h};
    layout.sliderBounds = {0, 0, box_w, slider.getHeight() - box_h};
    return layout;
  }
  return LookAndFeel_V4::getSliderLayout(slider);
}

void KnobLookAndFeel::drawTickBox(juce::Graphics& g,
                                  juce::Component& component,
                                  float x,
                                  float y,
                                  float w,
                                  float h,
                                  bool ticked,
                                  bool is_enabled,
                                  bool should_draw_button_as_highlighted,
                                  bool should_draw_button_as_down) {
  const auto* button = dynamic_cast<juce::Button*>(&component);
  if (button == nullptr || button->getRadioGroupId() == 0) {
    LookAndFeel_V4::drawTickBox(g,
                                component,
                                x,
                                y,
                                w,
                                h,
                                ticked,
                                is_enabled,
                                should_draw_button_as_highlighted,
                                should_draw_button_as_down);
    return;
  }
  const auto bounds = juce::Rectangle<float>(x, y, w, h)
                          .reduced(1.0f)
                          .withSizeKeepingCentre(juce::jmin(w, h) - 2.0f,
                                                 juce::jmin(w, h) - 2.0f);
  g.setColour(component.findColour(juce::ToggleButton::tickDisabledColourId));
  g.drawEllipse(bounds, 1.5f);
  if (ticked) {
    g.setColour(component.findColour(juce::ToggleButton::tickColourId)
                    .withMultipliedAlpha(is_enabled ? 1.0f : 0.5f));
    g.fillEllipse(bounds.reduced(bounds.getWidth() * 0.25f));
  }
}

}  // namespace spdsx
