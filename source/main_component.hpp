// The main window content: a 3x3 grid of pads labeled 1-9, each with a
// top and a bottom sample slot (18 slots, indexed (row * 3 + col) * 2,
// +1 for the bottom).
#pragma once

#include <array>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "audio.hpp"
#include "sample_slot.hpp"

namespace spdsx {

class MainComponent : public juce::Component, private juce::Timer {
public:
  static constexpr int kSlotCount = 18;

  MainComponent();

  // Assigns a sample to a slot: decodes it for playback and updates the
  // slot display. Shared by --load, drag-and-drop, and the file dialog.
  void load_sample(int idx, const juce::File& file);

  void paint(juce::Graphics& g) override;
  void resized() override;
  bool keyPressed(const juce::KeyPress& key) override;

private:
  void transport_action(int idx, TransportAction action);
  void choose_sample(int idx);
  void timerCallback() override;
  juce::Rectangle<int> pad_bounds(int row, int col) const;

  AudioEngine engine_ {kSlotCount};
  std::unique_ptr<juce::FileChooser> chooser_;
  std::array<std::unique_ptr<SampleSlot>, kSlotCount> slots_;
  int hovered_ = -1;
};

}  // namespace spdsx
