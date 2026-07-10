// One sample slot: the top or bottom half of a pad in the 3x3 grid.
#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

namespace spdsx {

enum class PlayState { stopped, playing, paused };
enum class TransportAction { play, pause, stop };

class SampleSlot : public juce::Component,
                   public juce::FileDragAndDropTarget {
public:
  explicit SampleSlot(int index);

  int index() const { return index_; }

  // A file was dropped on this slot.
  std::function<void(int, const juce::File&)> on_drop;
  // The slot body was clicked (e.g. to browse for a sample).
  std::function<void(int)> on_click;
  // A transport button was pressed.
  std::function<void(int, TransportAction)> on_transport;

  void set_sample(const juce::String& name,
      double duration_seconds,
      double sample_rate,
      const juce::Image& image);
  bool has_sample() const { return sample_name_.isNotEmpty(); }

  void set_play_state(PlayState state);
  PlayState play_state() const { return play_state_; }
  // Playhead position, 0..1; drawn while playing or paused.
  void set_position(double fraction);

  // Hover is polled by the parent (child buttons make enter/exit
  // unreliable for this); drives the focus-follows-mouse highlight.
  void set_hovered(bool hovered);

  // Momentarily depresses the button for an action triggered from the
  // keyboard, so the spacebar visibly activates play/pause.
  void flash_transport_button(TransportAction action);

  void paint(juce::Graphics& g) override;
  void resized() override;
  void mouseUp(const juce::MouseEvent& event) override;

  bool isInterestedInFileDrag(const juce::StringArray& files) override;
  void fileDragEnter(const juce::StringArray&, int, int) override;
  void fileDragExit(const juce::StringArray&) override;
  void filesDropped(const juce::StringArray& files, int, int) override;

private:
  juce::ShapeButton* button_for(TransportAction action);
  void update_button_colours();
  juce::Rectangle<int> info_bar_bounds() const;

  int index_;
  juce::String sample_name_;
  juce::String sample_meta_;
  juce::Image image_;
  PlayState play_state_ = PlayState::stopped;
  double position_ = 0.0;
  bool hovered_ = false;
  bool drag_hover_ = false;

  juce::ShapeButton play_button_;
  juce::ShapeButton pause_button_;
  juce::ShapeButton stop_button_;
};

}  // namespace spdsx
