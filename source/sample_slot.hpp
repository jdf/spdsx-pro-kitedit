// One sample slot: the top or bottom half of a pad in the 3x3 grid.
#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

namespace spdsx {

class SampleSlot : public juce::Component {
public:
  explicit SampleSlot(int index);

  int index() const { return index_; }

  // Focus follows the mouse: reports (index, entered) so the parent can
  // route the spacebar to the slot under the pointer.
  std::function<void(int, bool)> on_hover;

  void set_sample_name(const juce::String& name);
  // An invalid image just leaves the slot without a spectrogram (e.g.
  // files shorter than one FFT window).
  void set_image(const juce::Image& image);
  void set_playing(bool playing);
  bool has_sample() const { return sample_name_.isNotEmpty(); }

  void paint(juce::Graphics& g) override;
  void mouseEnter(const juce::MouseEvent&) override;
  void mouseExit(const juce::MouseEvent&) override;

private:
  int index_;
  juce::String sample_name_;
  juce::Image image_;
  bool playing_ = false;
  bool hovered_ = false;
};

}  // namespace spdsx
