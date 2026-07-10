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
const juce::Colour kBorderDrop(0xffd9a94f);

bool looks_like_audio(const juce::String& path)
{
  static const juce::StringArray kExtensions {
      ".wav", ".aif", ".aiff", ".flac", ".ogg", ".mp3"};
  for (const auto& ext : kExtensions) {
    if (path.endsWithIgnoreCase(ext)) {
      return true;
    }
  }
  return false;
}

}  // namespace

SampleSlot::SampleSlot(int index)
    : index_(index)
{
  setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void SampleSlot::set_sample_name(const juce::String& name)
{
  sample_name_ = name;
  repaint();
}

void SampleSlot::set_image(const juce::Image& image)
{
  image_ = image;
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
  g.setColour(hovered_ || drag_hover_ ? kSlotBgHover : kSlotBg);
  g.fillRoundedRectangle(bounds, 8.0f);

  if (image_.isValid()) {
    g.drawImage(image_,
        bounds.reduced(4.0f),
        juce::RectanglePlacement::stretchToFit);
  }

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

  g.setColour(drag_hover_
          ? kBorderDrop
          : (playing_ ? kBorderPlaying
                      : (hovered_ ? kBorderHover : kBorder)));
  g.drawRoundedRectangle(bounds.reduced(1.0f), 8.0f,
      drag_hover_ || playing_ ? 2.0f : 1.0f);
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

void SampleSlot::mouseUp(const juce::MouseEvent& event)
{
  if (!event.mouseWasDraggedSinceMouseDown()
      && contains(event.getPosition()) && on_click)
  {
    on_click(index_);
  }
}

bool SampleSlot::isInterestedInFileDrag(const juce::StringArray& files)
{
  return files.size() == 1 && looks_like_audio(files[0]);
}

void SampleSlot::fileDragEnter(const juce::StringArray&, int, int)
{
  drag_hover_ = true;
  repaint();
}

void SampleSlot::fileDragExit(const juce::StringArray&)
{
  drag_hover_ = false;
  repaint();
}

void SampleSlot::filesDropped(const juce::StringArray& files, int, int)
{
  drag_hover_ = false;
  repaint();
  if (on_drop) {
    on_drop(index_, juce::File(files[0]));
  }
}

}  // namespace spdsx
