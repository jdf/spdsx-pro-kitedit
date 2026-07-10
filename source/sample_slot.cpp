#include "sample_slot.hpp"

#include "audio_files.hpp"

namespace spdsx {

namespace {

const juce::Colour kSlotBg(0xff1b212a);
const juce::Colour kSlotBgHover(0xff232b36);
const juce::Colour kBorder(0xff313a46);
const juce::Colour kBorderHover(0xff4f8fd9);
const juce::Colour kBorderPlaying(0xff58c47a);
const juce::Colour kBorderDrop(0xffd9a94f);
const juce::Colour kPlaceholderText(0xff4c5866);
const juce::Colour kNameText(0xffe6edf5);
const juce::Colour kMetaText(0xff9aa7b4);
const juce::Colour kInfoBarScrim(0xc20a0d11);
const juce::Colour kPlayhead(0xffe6edf5);
const juce::Colour kIcon(0xff8a97a6);
const juce::Colour kIconOver(0xffffffff);
const juce::Colour kIconDown(0xff4f8fd9);
const juce::Colour kIconPlaying(0xff58c47a);
const juce::Colour kIconPaused(0xffd9a94f);

constexpr int kInfoBarHeight = 22;
constexpr int kButtonSize = 14;
constexpr int kButtonGap = 6;
constexpr float kImageInset = 4.0f;

// Both drag flavors land here: a single audio file or nothing.
juce::File dragged_browser_file(
    const juce::DragAndDropTarget::SourceDetails& details)
{
  if (details.description.toString() != kSampleDragId) {
    return {};
  }
  auto* tree = dynamic_cast<juce::FileTreeComponent*>(
      details.sourceComponent.get());
  if (tree == nullptr || tree->getNumSelectedFiles() != 1) {
    return {};
  }
  auto file = tree->getSelectedFile();
  return file.existsAsFile() && looks_like_audio(file.getFullPathName())
      ? file
      : juce::File();
}

juce::String format_meta(double duration_seconds, double sample_rate)
{
  juce::String duration;
  if (duration_seconds < 10.0) {
    duration = juce::String(duration_seconds, 2) + " s";
  } else if (duration_seconds < 60.0) {
    duration = juce::String(duration_seconds, 1) + " s";
  } else {
    auto total = static_cast<int>(duration_seconds + 0.5);
    duration = juce::String(total / 60) + ":"
        + juce::String(total % 60).paddedLeft('0', 2);
  }
  return duration + "  \xc2\xb7  " + juce::String(sample_rate / 1000.0, 1)
      + " kHz";
}

juce::Path make_shape(TransportAction action)
{
  juce::Path p;
  switch (action) {
    case TransportAction::play:
      p.addTriangle(0.0f, 0.0f, 0.0f, 1.0f, 0.9f, 0.5f);
      break;
    case TransportAction::pause:
      p.addRectangle(0.0f, 0.0f, 0.32f, 1.0f);
      p.addRectangle(0.68f, 0.0f, 0.32f, 1.0f);
      break;
    case TransportAction::stop:
      p.addRectangle(0.0f, 0.0f, 1.0f, 1.0f);
      break;
  }
  return p;
}

}  // namespace

SampleSlot::SampleSlot(int index)
    : index_(index)
    , play_button_("play", kIcon, kIconOver, kIconDown)
    , pause_button_("pause", kIcon, kIconOver, kIconDown)
    , stop_button_("stop", kIcon, kIconOver, kIconDown)
{
  setMouseCursor(juce::MouseCursor::PointingHandCursor);

  const std::pair<juce::ShapeButton*, TransportAction> buttons[] = {
      {&play_button_, TransportAction::play},
      {&pause_button_, TransportAction::pause},
      {&stop_button_, TransportAction::stop},
  };
  for (auto [button, action] : buttons) {
    button->setShape(make_shape(action), false, true, false);
    button->onClick = [this, action = action]
    {
      if (on_transport) {
        on_transport(index_, action);
      }
    };
    button->setVisible(false);
    addChildComponent(*button);
  }
}

void SampleSlot::set_sample(const juce::String& name,
    double duration_seconds,
    double sample_rate,
    const juce::Image& image)
{
  sample_name_ = name;
  sample_meta_ = format_meta(duration_seconds, sample_rate);
  image_ = image;
  playable_ = true;
  play_state_ = PlayState::stopped;
  position_ = 0.0;
  play_button_.setVisible(true);
  pause_button_.setVisible(true);
  stop_button_.setVisible(true);
  update_button_colours();
  repaint();
}

void SampleSlot::set_sample_missing(const juce::String& name)
{
  sample_name_ = name;
  sample_meta_ = "missing";
  image_ = juce::Image();
  playable_ = false;
  play_state_ = PlayState::stopped;
  position_ = 0.0;
  play_button_.setVisible(false);
  pause_button_.setVisible(false);
  stop_button_.setVisible(false);
  repaint();
}

void SampleSlot::clear_sample()
{
  sample_name_.clear();
  sample_meta_.clear();
  image_ = juce::Image();
  playable_ = false;
  play_state_ = PlayState::stopped;
  position_ = 0.0;
  play_button_.setVisible(false);
  pause_button_.setVisible(false);
  stop_button_.setVisible(false);
  repaint();
}

void SampleSlot::set_play_state(PlayState state)
{
  if (play_state_ != state) {
    play_state_ = state;
    if (state == PlayState::stopped) {
      position_ = 0.0;
    }
    update_button_colours();
    repaint();
  }
}

void SampleSlot::set_position(double fraction)
{
  if (std::abs(fraction - position_) > 1.0e-4) {
    position_ = fraction;
    repaint();
  }
}

void SampleSlot::set_hovered(bool hovered)
{
  if (hovered_ != hovered) {
    hovered_ = hovered;
    repaint();
  }
}

void SampleSlot::flash_transport_button(TransportAction action)
{
  auto* button = button_for(action);
  button->setState(juce::Button::buttonDown);
  juce::Timer::callAfterDelay(120,
      [safe = juce::Component::SafePointer<juce::ShapeButton>(button)]
      {
        if (safe != nullptr) {
          safe->setState(juce::Button::buttonNormal);
        }
      });
}

juce::ShapeButton* SampleSlot::button_for(TransportAction action)
{
  switch (action) {
    case TransportAction::play:
      return &play_button_;
    case TransportAction::pause:
      return &pause_button_;
    case TransportAction::stop:
      return &stop_button_;
  }
  return &play_button_;
}

void SampleSlot::update_button_colours()
{
  // The active state tints its button: green while playing, amber while
  // paused.
  auto play_normal =
      play_state_ == PlayState::playing ? kIconPlaying : kIcon;
  auto pause_normal =
      play_state_ == PlayState::paused ? kIconPaused : kIcon;
  play_button_.setColours(play_normal, kIconOver, kIconDown);
  pause_button_.setColours(pause_normal, kIconOver, kIconDown);
  stop_button_.setColours(kIcon, kIconOver, kIconDown);
  play_button_.repaint();
  pause_button_.repaint();
  stop_button_.repaint();
}

void SampleSlot::paint(juce::Graphics& g)
{
  auto bounds = getLocalBounds().toFloat();
  g.setColour(hovered_ || drag_hover_ ? kSlotBgHover : kSlotBg);
  g.fillRoundedRectangle(bounds, 8.0f);

  if (image_.isValid()) {
    g.drawImage(image_,
        bounds.reduced(kImageInset),
        juce::RectanglePlacement::stretchToFit);
  }

  if (has_sample()) {
    // Playhead, visible while playing or paused.
    if (play_state_ != PlayState::stopped) {
      const float span = bounds.getWidth() - 2.0f * kImageInset;
      const float x =
          kImageInset + span * static_cast<float>(position_);
      g.setColour(kPlayhead);
      g.fillRect(x - 1.0f, kImageInset, 2.0f,
          bounds.getHeight() - 2.0f * kImageInset);
    }

    // Info bar: a scrim keeps the text legible over the spectrogram.
    auto bar = info_bar_bounds();
    g.setColour(kInfoBarScrim);
    g.fillRect(bar);

    // Name on the left (ellipsized), duration/rate on the right, both
    // clear of the buttons.
    auto text_area = bar.reduced(6, 0);
    text_area.removeFromRight(3 * kButtonSize + 3 * kButtonGap);
    g.setFont(11.0f);
    g.setColour(playable_ ? kMetaText : kBorderDrop);
    const auto meta_area =
        text_area.removeFromRight(text_area.getWidth() * 2 / 5);
    g.drawText(sample_meta_, meta_area, juce::Justification::centredRight);
    g.setColour(kNameText);
    g.drawText(sample_name_,
        text_area.withTrimmedRight(6),
        juce::Justification::centredLeft);
  } else {
    g.setColour(kPlaceholderText);
    g.setFont(13.0f);
    g.drawText(
        "drop or click", getLocalBounds(), juce::Justification::centred);
  }

  g.setColour(drag_hover_
          ? kBorderDrop
          : (play_state_ == PlayState::playing
                    ? kBorderPlaying
                    : (hovered_ ? kBorderHover : kBorder)));
  g.drawRoundedRectangle(bounds.reduced(1.0f), 8.0f,
      drag_hover_ || play_state_ == PlayState::playing ? 2.0f : 1.0f);
}

juce::Rectangle<int> SampleSlot::info_bar_bounds() const
{
  return getLocalBounds()
      .reduced(static_cast<int>(kImageInset))
      .removeFromBottom(kInfoBarHeight);
}

void SampleSlot::resized()
{
  auto bar = info_bar_bounds();
  auto buttons = bar.reduced(6, (kInfoBarHeight - kButtonSize) / 2);
  stop_button_.setBounds(buttons.removeFromRight(kButtonSize));
  buttons.removeFromRight(kButtonGap);
  pause_button_.setBounds(buttons.removeFromRight(kButtonSize));
  buttons.removeFromRight(kButtonGap);
  play_button_.setBounds(buttons.removeFromRight(kButtonSize));
}

void SampleSlot::mouseUp(const juce::MouseEvent& event)
{
  // Clicks anywhere across the control strip's vertical band belong to
  // the transport, not to file browsing, even between the buttons. A
  // missing sample has no transport, so it stays fully browsable.
  if (is_playable()
      && event.getPosition().getY() >= info_bar_bounds().getY())
  {
    return;
  }
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

bool SampleSlot::isInterestedInDragSource(const SourceDetails& details)
{
  return dragged_browser_file(details) != juce::File();
}

void SampleSlot::itemDragEnter(const SourceDetails&)
{
  drag_hover_ = true;
  repaint();
}

void SampleSlot::itemDragExit(const SourceDetails&)
{
  drag_hover_ = false;
  repaint();
}

void SampleSlot::itemDropped(const SourceDetails& details)
{
  drag_hover_ = false;
  repaint();
  if (auto file = dragged_browser_file(details);
      file != juce::File() && on_drop)
  {
    on_drop(index_, file);
  }
}

}  // namespace spdsx
