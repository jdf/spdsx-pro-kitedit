#include "sample_slot.h"

#include "audio_files.h"

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
juce::File DraggedBrowserFile(
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
  return file.existsAsFile() && LooksLikeAudio(file.getFullPathName())
      ? file
      : juce::File();
}

// Device-panel drags carry "spdsx-devsample:<pool index>". Returns the
// pool index, or -1 if this isn't a device wave drag.
int DraggedDeviceSample(
    const juce::DragAndDropTarget::SourceDetails& details)
{
  const juce::String d = details.description.toString();
  const juce::String prefix(kDeviceSampleDragPrefix);
  return d.startsWith(prefix) ? d.substring(prefix.length()).getIntValue()
                              : -1;
}

// Slot-to-slot drags carry "spdsx-slot:<index>". Returns the source slot
// index, or -1 if this isn't a slot drag.
const juce::String kSlotDragPrefix = "spdsx-slot:";
int DraggedSlotIndex(const juce::DragAndDropTarget::SourceDetails& details)
{
  const juce::String d = details.description.toString();
  return d.startsWith(kSlotDragPrefix)
      ? d.substring(kSlotDragPrefix.length()).getIntValue()
      : -1;
}

// A slot-to-slot drag with command held targets the whole pad (both layers).
bool WholePadDrag(const juce::DragAndDropTarget::SourceDetails& details)
{
  return DraggedSlotIndex(details) >= 0
      && juce::ModifierKeys::getCurrentModifiers().isCommandDown();
}

juce::String FormatMeta(double duration_seconds, double sample_rate)
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

juce::Path MakeShape(TransportAction action)
{
  juce::Path p;
  switch (action) {
    case TransportAction::kPlay:
      p.addTriangle(0.0f, 0.0f, 0.0f, 1.0f, 0.9f, 0.5f);
      break;
    case TransportAction::kPause:
      p.addRectangle(0.0f, 0.0f, 0.32f, 1.0f);
      p.addRectangle(0.68f, 0.0f, 0.32f, 1.0f);
      break;
    case TransportAction::kStop:
      p.addRectangle(0.0f, 0.0f, 1.0f, 1.0f);
      break;
  }
  return p;
}

}  // namespace

juce::Colour VelocityColour(int velocity)
{
  const float t =
      static_cast<float>(juce::jlimit(1, 127, velocity)) / 127.0f;
  const juce::Colour soft(0xff3b6ea5);   // cool blue
  const juce::Colour medium(0xffd9a94f); // amber
  const juce::Colour hard(0xffe0533a);   // hot red
  return t < 0.5f ? soft.interpolatedWith(medium, t * 2.0f)
                  : medium.interpolatedWith(hard, (t - 0.5f) * 2.0f);
}

SampleSlot::SampleSlot(int index)
    : index_(index)
    , play_button_("play", kIcon, kIconOver, kIconDown)
    , pause_button_("pause", kIcon, kIconOver, kIconDown)
    , stop_button_("stop", kIcon, kIconOver, kIconDown)
{
  const std::pair<juce::ShapeButton*, TransportAction> buttons[] = {
      {&play_button_, TransportAction::kPlay},
      {&pause_button_, TransportAction::kPause},
      {&stop_button_, TransportAction::kStop},
  };
  for (auto [button, action] : buttons) {
    button->setShape(MakeShape(action), false, true, false);
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

void SampleSlot::SetSample(const juce::String& name,
    double duration_seconds,
    double sample_rate,
    const juce::Image& image)
{
  sample_name_ = name;
  sample_meta_ = FormatMeta(duration_seconds, sample_rate);
  image_ = image;
  playable_ = true;
  device_wave_ = false;
  play_state_ = PlayState::kStopped;
  position_ = 0.0;
  play_button_.setVisible(true);
  pause_button_.setVisible(true);
  stop_button_.setVisible(true);
  // The body is click-to-play only when there's something to play.
  setMouseCursor(juce::MouseCursor::PointingHandCursor);
  UpdateButtonColours();
  repaint();
}

void SampleSlot::SetDeviceSample(const juce::String& name,
    double duration_seconds)
{
  sample_name_ = name;
  sample_meta_ = duration_seconds > 0.0
      ? juce::String(duration_seconds, 2) + " s  \xc2\xb7  device"
      : juce::String("device");
  image_ = juce::Image();
  playable_ = false;
  device_wave_ = true;
  play_state_ = PlayState::kStopped;
  position_ = 0.0;
  play_button_.setVisible(false);
  pause_button_.setVisible(false);
  stop_button_.setVisible(false);
  setMouseCursor(juce::MouseCursor::NormalCursor);
  repaint();
}

void SampleSlot::SetSampleMissing(const juce::String& name)
{
  sample_name_ = name;
  sample_meta_ = "missing";
  image_ = juce::Image();
  playable_ = false;
  device_wave_ = false;
  play_state_ = PlayState::kStopped;
  position_ = 0.0;
  play_button_.setVisible(false);
  pause_button_.setVisible(false);
  stop_button_.setVisible(false);
  setMouseCursor(juce::MouseCursor::NormalCursor);
  repaint();
}

void SampleSlot::ClearSample()
{
  sample_name_.clear();
  sample_meta_.clear();
  image_ = juce::Image();
  playable_ = false;
  device_wave_ = false;
  play_state_ = PlayState::kStopped;
  position_ = 0.0;
  play_button_.setVisible(false);
  pause_button_.setVisible(false);
  stop_button_.setVisible(false);
  setMouseCursor(juce::MouseCursor::NormalCursor);
  repaint();
}

void SampleSlot::set_play_state(PlayState state)
{
  if (play_state_ != state) {
    play_state_ = state;
    if (state == PlayState::kStopped) {
      position_ = 0.0;
      velocity_highlight_ = 0;
    }
    UpdateButtonColours();
    repaint();
  }
}

void SampleSlot::set_velocity_highlight(int velocity)
{
  if (velocity_highlight_ != velocity) {
    velocity_highlight_ = velocity;
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

void SampleSlot::FlashTransportButton(TransportAction action)
{
  auto* button = ButtonFor(action);
  button->setState(juce::Button::buttonDown);
  juce::Timer::callAfterDelay(120,
      [safe = juce::Component::SafePointer<juce::ShapeButton>(button)]
      {
        if (safe != nullptr) {
          safe->setState(juce::Button::buttonNormal);
        }
      });
}

juce::ShapeButton* SampleSlot::ButtonFor(TransportAction action)
{
  switch (action) {
    case TransportAction::kPlay:
      return &play_button_;
    case TransportAction::kPause:
      return &pause_button_;
    case TransportAction::kStop:
      return &stop_button_;
  }
  return &play_button_;
}

void SampleSlot::UpdateButtonColours()
{
  // The active state tints its button: green while playing, amber while
  // paused.
  auto play_normal =
      play_state_ == PlayState::kPlaying ? kIconPlaying : kIcon;
  auto pause_normal =
      play_state_ == PlayState::kPaused ? kIconPaused : kIcon;
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
  } else if (device_wave_) {
    // No spectrogram until the wave's audio can be pulled off the
    // device; the body says where the sound lives instead.
    g.setColour(kPlaceholderText);
    g.setFont(13.0f);
    g.drawText("on device", getLocalBounds(),
        juce::Justification::centred);
  }

  // While this layer sounds, a wash in the velocity colour shows how
  // loud the layer mode decided it should be.
  if (play_state_ != PlayState::kStopped && velocity_highlight_ > 0) {
    const float strength =
        static_cast<float>(velocity_highlight_) / 127.0f;
    g.setColour(VelocityColour(velocity_highlight_)
            .withAlpha(0.08f + 0.14f * strength));
    g.fillRoundedRectangle(bounds, 8.0f);
  }

  if (has_sample()) {
    // Playhead, visible while playing or paused.
    if (play_state_ != PlayState::kStopped) {
      const float span = bounds.getWidth() - 2.0f * kImageInset;
      const float x =
          kImageInset + span * static_cast<float>(position_);
      g.setColour(kPlayhead);
      g.fillRect(x - 1.0f, kImageInset, 2.0f,
          bounds.getHeight() - 2.0f * kImageInset);
    }

    // Info bar: a scrim keeps the text legible over the spectrogram.
    auto bar = InfoBarBounds();
    g.setColour(kInfoBarScrim);
    g.fillRect(bar);

    // Name on the left (ellipsized), duration/rate on the right, both
    // clear of the buttons.
    auto text_area = bar.reduced(6, 0);
    text_area.removeFromRight(3 * kButtonSize + 3 * kButtonGap);
    g.setFont(11.0f);
    // Missing files flag their meta in amber; device waves are a normal
    // state, just not playable yet.
    g.setColour(playable_ || device_wave_ ? kMetaText : kBorderDrop);
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
        "drop a sample", getLocalBounds(), juce::Justification::centred);
  }

  const auto playing_border = velocity_highlight_ > 0
      ? VelocityColour(velocity_highlight_)
      : kBorderPlaying;
  g.setColour(drag_hover_
          ? kBorderDrop
          : (play_state_ == PlayState::kPlaying
                    ? playing_border
                    : (hovered_ ? kBorderHover : kBorder)));
  g.drawRoundedRectangle(bounds.reduced(1.0f), 8.0f,
      drag_hover_ || play_state_ == PlayState::kPlaying ? 2.0f : 1.0f);
}

juce::Rectangle<int> SampleSlot::InfoBarBounds() const
{
  return getLocalBounds()
      .reduced(static_cast<int>(kImageInset))
      .removeFromBottom(kInfoBarHeight);
}

void SampleSlot::resized()
{
  auto bar = InfoBarBounds();
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
      && event.getPosition().getY() >= InfoBarBounds().getY())
  {
    return;
  }
  if (!event.mouseWasDraggedSinceMouseDown()
      && contains(event.getPosition()) && on_click)
  {
    on_click(index_);
  }
}

void SampleSlot::mouseDrag(const juce::MouseEvent& event)
{
  // Start dragging this slot's sample once the pointer moves past a small
  // threshold; the drop target reads "spdsx-slot:<index>".
  if (!has_sample() || event.getDistanceFromDragStart() < 5) {
    return;
  }
  auto* container =
      juce::DragAndDropContainer::findParentDragContainerFor(this);
  if (container != nullptr && !container->isDragAndDropActive()) {
    container->startDragging(kSlotDragPrefix + juce::String(index_), this);
  }
}

bool SampleSlot::isInterestedInFileDrag(const juce::StringArray& files)
{
  return files.size() == 1 && LooksLikeAudio(files[0]);
}

void SampleSlot::set_drag_hover(bool on)
{
  if (drag_hover_ != on) {
    drag_hover_ = on;
    repaint();
  }
}

void SampleSlot::fileDragEnter(const juce::StringArray&, int, int)
{
  if (on_drag_target) {
    on_drag_target(index_, false);
  }
}

void SampleSlot::fileDragExit(const juce::StringArray&)
{
  if (on_drag_target) {
    on_drag_target(-1, false);
  }
}

void SampleSlot::filesDropped(const juce::StringArray& files, int, int)
{
  if (on_drag_target) {
    on_drag_target(-1, false);
  }
  if (on_drop) {
    on_drop(index_, juce::File(files[0]));
  }
}

bool SampleSlot::isInterestedInDragSource(const SourceDetails& details)
{
  if (DraggedBrowserFile(details) != juce::File()
      || DraggedDeviceSample(details) > 0)
  {
    return true;
  }
  const int from = DraggedSlotIndex(details);
  return from >= 0 && from != index_;
}

void SampleSlot::itemDragEnter(const SourceDetails& details)
{
  if (on_drag_target) {
    on_drag_target(index_, WholePadDrag(details));
  }
}

void SampleSlot::itemDragMove(const SourceDetails& details)
{
  // Re-report so a command press/release mid-hover updates the highlight.
  if (on_drag_target) {
    on_drag_target(index_, WholePadDrag(details));
  }
}

void SampleSlot::itemDragExit(const SourceDetails&)
{
  if (on_drag_target) {
    on_drag_target(-1, false);
  }
}

void SampleSlot::itemDropped(const SourceDetails& details)
{
  if (on_drag_target) {
    on_drag_target(-1, false);
  }
  if (auto file = DraggedBrowserFile(details);
      file != juce::File() && on_drop)
  {
    on_drop(index_, file);
    return;
  }
  if (const int sample = DraggedDeviceSample(details);
      sample > 0 && on_drop_device)
  {
    on_drop_device(index_, sample);
    return;
  }
  const int from = DraggedSlotIndex(details);
  if (from >= 0 && from != index_ && on_slot_move) {
    // Option duplicates instead of moving; command acts on the whole pad
    // (both layers) instead of the single slot.
    const auto mods = juce::ModifierKeys::getCurrentModifiers();
    on_slot_move(from, index_, mods.isAltDown(), mods.isCommandDown());
  }
}

}  // namespace spdsx
