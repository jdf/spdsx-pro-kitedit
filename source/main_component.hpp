// The main window content: a 3x3 grid of pads, each with a top and a
// bottom sample slot (18 slots, indexed (row * 3 + col) * 2, +1 for the
// bottom).
#pragma once

#include <array>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "sample_slot.hpp"

namespace spdsx {

class MainComponent : public juce::Component {
public:
  static constexpr int kSlotCount = 18;

  MainComponent();

  void paint(juce::Graphics& g) override;
  void resized() override;
  bool keyPressed(const juce::KeyPress& key) override;

private:
  void toggle_play(int idx);

  std::array<std::unique_ptr<SampleSlot>, kSlotCount> slots_;
  int hovered_ = -1;
};

}  // namespace spdsx
