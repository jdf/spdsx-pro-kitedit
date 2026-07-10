#include "main_component.hpp"

#include <cstdio>

#include "spectro.hpp"

namespace spdsx {

namespace {

constexpr int kGridPadding = 14;
constexpr int kGridSpacing = 14;
constexpr int kPadPadding = 8;
constexpr int kPadHeader = 20;
constexpr int kSlotSpacing = 8;

const juce::Colour kWindowBg(0xff12161b);
const juce::Colour kPadBg(0xff161b22);
const juce::Colour kPadBorder(0xff242d38);
const juce::Colour kPadLabel(0xff8a97a6);

}  // namespace

MainComponent::MainComponent()
{
  setWantsKeyboardFocus(true);
  for (int i = 0; i < kSlotCount; ++i) {
    slots_[static_cast<size_t>(i)] = std::make_unique<SampleSlot>(i);
    auto& slot = *slots_[static_cast<size_t>(i)];
    slot.on_drop = [this](int idx, const juce::File& file)
    { load_sample(idx, file); };
    slot.on_click = [this](int idx) { choose_sample(idx); };
    slot.on_transport = [this](int idx, TransportAction action)
    { transport_action(idx, action); };
    addAndMakeVisible(slot);
  }
  model_.add_listener(this);
  setSize(960, 720);
  // Drives the hover poll, the playhead, and end-of-sample detection.
  startTimerHz(30);
}

MainComponent::~MainComponent()
{
  model_.remove_listener(this);
}

void MainComponent::load_sample(int idx, const juce::File& file)
{
  if (idx < 0 || idx >= kSlotCount) {
    std::fprintf(
        stderr, "slot %d out of range (0..%d)\n", idx, kSlotCount - 1);
    return;
  }
  model_.set_slot(idx, file);
}

// The model is the source of truth: engine and slot display sync to it
// here, whether the change came from a user gesture, undo, or a loaded
// kit file.
void MainComponent::slot_changed(int idx)
{
  const auto& file = model_.slot(idx);
  auto& slot = *slots_[static_cast<size_t>(idx)];
  if (file == juce::File()) {
    engine_.clear(idx);
    slot.clear_sample();
    return;
  }
  auto info = engine_.load(idx, file);
  if (!info) {
    // Unreadable (moved, unmounted, not audio): keep the assignment
    // visible so it survives a save/load round trip.
    engine_.clear(idx);
    slot.set_sample_missing(file.getFileName());
    return;
  }
  // Too-short files play fine but render no spectrogram; the slot just
  // shows the info bar in that case.
  juce::Image image;
  if (auto png = render_spectrogram(
          file.getFullPathName().toStdString(), idx);
      !png.empty())
  {
    image = juce::ImageFileFormat::loadFrom(juce::File(png));
  }
  slot.set_sample(file.getFileName(), info->duration_seconds,
      info->sample_rate, image);
}

void MainComponent::paint(juce::Graphics& g)
{
  g.fillAll(kWindowBg);
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) {
      auto pad = pad_bounds(r, c);
      g.setColour(kPadBg);
      g.fillRoundedRectangle(pad.toFloat(), 10.0f);
      g.setColour(kPadBorder);
      g.drawRoundedRectangle(pad.toFloat().reduced(0.5f), 10.0f, 1.0f);
      g.setColour(kPadLabel);
      g.setFont(juce::Font(juce::FontOptions(13.0f)).boldened());
      g.drawText(juce::String(r * 3 + c + 1),
          pad.reduced(kPadPadding + 2, kPadPadding)
              .removeFromTop(kPadHeader),
          juce::Justification::centredLeft);
    }
  }
}

juce::Rectangle<int> MainComponent::pad_bounds(int row, int col) const
{
  const int cell_w = (getWidth() - 2 * kGridPadding - 2 * kGridSpacing) / 3;
  const int cell_h = (getHeight() - 2 * kGridPadding - 2 * kGridSpacing) / 3;
  return {kGridPadding + col * (cell_w + kGridSpacing),
      kGridPadding + row * (cell_h + kGridSpacing), cell_w, cell_h};
}

void MainComponent::resized()
{
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) {
      auto inner = pad_bounds(r, c).reduced(kPadPadding);
      inner.removeFromTop(kPadHeader);
      const int slot_h = (inner.getHeight() - kSlotSpacing) / 2;
      const auto pad = static_cast<size_t>((r * 3 + c) * 2);
      slots_[pad]->setBounds(inner.removeFromTop(slot_h));
      inner.removeFromTop(kSlotSpacing);
      slots_[pad + 1]->setBounds(inner.removeFromTop(slot_h));
    }
  }
}

bool MainComponent::keyPressed(const juce::KeyPress& key)
{
  if (key == juce::KeyPress::spaceKey) {
    if (hovered_ >= 0) {
      // Space triggers the slot under the mouse like a drum pad:
      // retrigger from the top while playing, resume while paused.
      auto& slot = *slots_[static_cast<size_t>(hovered_)];
      if (slot.is_playable()) {
        transport_action(hovered_, TransportAction::play);
        slot.flash_transport_button(TransportAction::play);
      }
    }
    return true;
  }
  return false;
}

void MainComponent::transport_action(int idx, TransportAction action)
{
  auto& slot = *slots_[static_cast<size_t>(idx)];
  if (!slot.is_playable()) {
    return;
  }
  switch (action) {
    case TransportAction::play:
      // Play during playback retriggers from the top (drum-pad style);
      // from paused it resumes, from stopped it starts at the top.
      if (slot.play_state() == PlayState::playing) {
        engine_.stop(idx);
      }
      engine_.play(idx);
      slot.set_play_state(PlayState::playing);
      break;
    case TransportAction::pause:
      if (slot.play_state() == PlayState::playing) {
        engine_.pause(idx);
        slot.set_play_state(PlayState::paused);
      }
      break;
    case TransportAction::stop:
      engine_.stop(idx);
      slot.set_play_state(PlayState::stopped);
      break;
  }
}

void MainComponent::choose_sample(int idx)
{
  // The chooser must outlive launchAsync, hence the member; opening a
  // new one abandons any dialog already up.
  chooser_ = std::make_unique<juce::FileChooser>(
      "Choose a sample", juce::File(), "*.wav");
  chooser_->launchAsync(juce::FileBrowserComponent::openMode
          | juce::FileBrowserComponent::canSelectFiles,
      [this, idx](const juce::FileChooser& fc)
      {
        if (auto file = fc.getResult(); file != juce::File()) {
          load_sample(idx, file);
        }
      });
}

void MainComponent::timerCallback()
{
  // Focus follows the mouse. Polled rather than event-driven: the
  // transport buttons are child components, and enter/exit pairs across
  // parent/child boundaries are easy to get wrong.
  int hovered = -1;
  for (int i = 0; i < kSlotCount; ++i) {
    if (slots_[static_cast<size_t>(i)]->isMouseOver(true)) {
      hovered = i;
      break;
    }
  }
  hovered_ = hovered;
  for (int i = 0; i < kSlotCount; ++i) {
    slots_[static_cast<size_t>(i)]->set_hovered(i == hovered);
  }

  // Advance playheads; sounds end on the audio thread, so a slot that
  // thinks it's playing while its transport has stopped just ran out.
  for (int i = 0; i < kSlotCount; ++i) {
    auto& slot = *slots_[static_cast<size_t>(i)];
    if (slot.play_state() == PlayState::playing) {
      if (engine_.is_playing(i)) {
        slot.set_position(engine_.position_fraction(i));
      } else {
        engine_.stop(i);
        slot.set_play_state(PlayState::stopped);
      }
    }
  }
}

}  // namespace spdsx
