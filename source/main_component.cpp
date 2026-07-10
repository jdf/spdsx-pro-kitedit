#include "main_component.hpp"

#include <cstdio>

#include "spectro.hpp"

namespace spdsx {

namespace {

constexpr int kGridPadding = 14;
constexpr int kGridSpacing = 14;
constexpr int kSlotSpacing = 8;

}  // namespace

MainComponent::MainComponent()
{
  setWantsKeyboardFocus(true);
  for (int i = 0; i < kSlotCount; ++i) {
    slots_[static_cast<size_t>(i)] = std::make_unique<SampleSlot>(i);
    auto& slot = *slots_[static_cast<size_t>(i)];
    slot.on_hover = [this](int idx, bool entered)
    {
      if (entered) {
        hovered_ = idx;
      } else if (hovered_ == idx) {
        hovered_ = -1;
      }
    };
    addAndMakeVisible(slot);
  }
  setSize(960, 720);
  // Sounds end on the audio thread; poll so the playing highlight clears
  // when a sample runs out on its own.
  startTimerHz(10);
}

void MainComponent::load_sample(int idx, const juce::File& file)
{
  if (idx < 0 || idx >= kSlotCount) {
    std::fprintf(
        stderr, "slot %d out of range (0..%d)\n", idx, kSlotCount - 1);
    return;
  }
  if (!engine_.load(idx, file)) {
    return;
  }
  auto& slot = *slots_[static_cast<size_t>(idx)];
  slot.set_sample_name(file.getFileName());
  // Too-short files play fine but render no spectrogram; the slot just
  // shows the name in that case.
  juce::Image image;
  if (auto png = render_spectrogram(
          file.getFullPathName().toStdString(), idx);
      !png.empty())
  {
    image = juce::ImageFileFormat::loadFrom(juce::File(png));
  }
  slot.set_image(image);
  slot.set_playing(false);
}

void MainComponent::timerCallback()
{
  for (int i = 0; i < kSlotCount; ++i) {
    slots_[static_cast<size_t>(i)]->set_playing(engine_.is_playing(i));
  }
}

void MainComponent::paint(juce::Graphics& g)
{
  g.fillAll(juce::Colour(0xff12161b));
}

void MainComponent::resized()
{
  const int cell_w = (getWidth() - 2 * kGridPadding - 2 * kGridSpacing) / 3;
  const int cell_h = (getHeight() - 2 * kGridPadding - 2 * kGridSpacing) / 3;
  const int slot_h = (cell_h - kSlotSpacing) / 2;
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) {
      const int x = kGridPadding + c * (cell_w + kGridSpacing);
      const int y = kGridPadding + r * (cell_h + kGridSpacing);
      const auto pad = static_cast<size_t>((r * 3 + c) * 2);
      slots_[pad]->setBounds(x, y, cell_w, slot_h);
      slots_[pad + 1]->setBounds(
          x, y + slot_h + kSlotSpacing, cell_w, slot_h);
    }
  }
}

bool MainComponent::keyPressed(const juce::KeyPress& key)
{
  if (key == juce::KeyPress::spaceKey) {
    if (hovered_ >= 0) {
      toggle_play(hovered_);
    }
    return true;
  }
  return false;
}

void MainComponent::toggle_play(int idx)
{
  auto& slot = *slots_[static_cast<size_t>(idx)];
  if (!slot.has_sample()) {
    return;
  }
  if (engine_.is_playing(idx)) {
    engine_.stop(idx);
    slot.set_playing(false);
  } else {
    engine_.start(idx);
    slot.set_playing(true);
  }
}

}  // namespace spdsx
