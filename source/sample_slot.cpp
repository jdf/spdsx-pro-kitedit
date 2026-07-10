#include "sample_slot.hpp"

namespace spdsx {

namespace {

const juce::Colour kSlotBg(0xff1b212a);
const juce::Colour kSlotBgHover(0xff232b36);
const juce::Colour kBorder(0xff313a46);
const juce::Colour kBorderHover(0xff4f8fd9);
const juce::Colour kBorderPlaying(0xff58c47a);
const juce::Colour kPlaceholderText(0xff4c5866);
const juce::Colour kNameText(0xffcfd8e3);

}  // namespace

SampleSlot::SampleSlot(int index)
    : index_(index)
{
}

void SampleSlot::set_sample_name(const juce::String& name)
{
  sample_name_ = name;
  repaint();
}

void SampleSlot::set_playing(bool playing)
{
  if (playing_ != playing) {
    playing_ = playing;
    repaint();
  }
}

void SampleSlot::paint(juce::Graphics& g)
{
  auto bounds = getLocalBounds().toFloat();
  g.setColour(hovered_ ? kSlotBgHover : kSlotBg);
  g.fillRoundedRectangle(bounds, 8.0f);

  if (has_sample()) {
    g.setColour(kNameText);
    g.setFont(11.0f);
    g.drawText(sample_name_,
        getLocalBounds().reduced(8, 6),
        juce::Justification::bottomLeft);
  } else {
    g.setColour(kPlaceholderText);
    g.setFont(13.0f);
    g.drawText(
        "drop a .wav", getLocalBounds(), juce::Justification::centred);
  }

  g.setColour(
      playing_ ? kBorderPlaying : (hovered_ ? kBorderHover : kBorder));
  g.drawRoundedRectangle(
      bounds.reduced(1.0f), 8.0f, playing_ ? 2.0f : 1.0f);
}

void SampleSlot::mouseEnter(const juce::MouseEvent&)
{
  hovered_ = true;
  if (on_hover) {
    on_hover(index_, true);
  }
  repaint();
}

void SampleSlot::mouseExit(const juce::MouseEvent&)
{
  hovered_ = false;
  if (on_hover) {
    on_hover(index_, false);
  }
  repaint();
}

}  // namespace spdsx
