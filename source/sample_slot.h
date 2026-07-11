// One sample slot: the top or bottom half of a pad in the 3x3 grid.
#ifndef SPDSX_PATCHEDIT_SOURCE_SAMPLE_SLOT_H_
#define SPDSX_PATCHEDIT_SOURCE_SAMPLE_SLOT_H_

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

namespace spdsx {

enum class PlayState { kStopped, kPlaying, kPaused };
enum class TransportAction { kPlay, kPause, kStop };

class SampleSlot : public juce::Component,
                   public juce::FileDragAndDropTarget,
                   public juce::DragAndDropTarget {
public:
  explicit SampleSlot(int index);

  int index() const { return index_; }

  // A file was dropped on this slot.
  std::function<void(int, const juce::File&)> on_drop;
  // The slot body was clicked; triggers playback like the spacebar.
  std::function<void(int)> on_click;
  // A transport button was pressed.
  std::function<void(int, TransportAction)> on_transport;
  // A sample was dragged from slot `from` onto this slot; copy=true
  // duplicates (option-drag), false moves.
  std::function<void(int from, int to, bool copy)> on_slot_move;

  void SetSample(const juce::String& name,
      double duration_seconds,
      double sample_rate,
      const juce::Image& image);
  // The slot is assigned a file that can't be read (e.g. a .kit whose
  // sample moved): shows the name flagged as missing, no transport.
  void SetSampleMissing(const juce::String& name);
  // Back to an empty slot.
  void ClearSample();

  // A file is assigned, readable or not.
  bool has_sample() const { return sample_name_.isNotEmpty(); }
  // A readable sample is loaded and can be played.
  bool is_playable() const { return has_sample() && playable_; }

  void set_play_state(PlayState state);
  PlayState play_state() const { return play_state_; }
  // Playhead position, 0..1; drawn while playing or paused.
  void set_position(double fraction);

  // Hover is polled by the parent (child buttons make enter/exit
  // unreliable for this); drives the focus-follows-mouse highlight.
  void set_hovered(bool hovered);

  // Momentarily depresses the button for an action triggered from the
  // keyboard, so the spacebar visibly activates play/pause.
  void FlashTransportButton(TransportAction action);

  void paint(juce::Graphics& g) override;
  void resized() override;
  void mouseUp(const juce::MouseEvent& event) override;
  void mouseDrag(const juce::MouseEvent& event) override;

  // External drags (Finder).
  bool isInterestedInFileDrag(const juce::StringArray& files) override;
  void fileDragEnter(const juce::StringArray&, int, int) override;
  void fileDragExit(const juce::StringArray&) override;
  void filesDropped(const juce::StringArray& files, int, int) override;

  // Internal drags (the sample browser panel).
  bool isInterestedInDragSource(const SourceDetails& details) override;
  void itemDragEnter(const SourceDetails&) override;
  void itemDragExit(const SourceDetails&) override;
  void itemDropped(const SourceDetails& details) override;

private:
  juce::ShapeButton* ButtonFor(TransportAction action);
  void UpdateButtonColours();
  juce::Rectangle<int> InfoBarBounds() const;

  int index_;
  juce::String sample_name_;
  juce::String sample_meta_;
  juce::Image image_;
  PlayState play_state_ = PlayState::kStopped;
  bool playable_ = false;
  double position_ = 0.0;
  bool hovered_ = false;
  bool drag_hover_ = false;

  juce::ShapeButton play_button_;
  juce::ShapeButton pause_button_;
  juce::ShapeButton stop_button_;
};

}  // namespace spdsx

#endif  // SPDSX_PATCHEDIT_SOURCE_SAMPLE_SLOT_H_
