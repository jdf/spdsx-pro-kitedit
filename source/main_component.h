// The main window content: a 3x3 grid of pads labeled 1-9, each with a
// top and a bottom sample slot (18 slots, indexed (row * 3 + col) * 2,
// +1 for the bottom).
#ifndef SPDSX_PATCHEDIT_SOURCE_MAIN_COMPONENT_H_
#define SPDSX_PATCHEDIT_SOURCE_MAIN_COMPONENT_H_

#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "audio.h"
#include "kit_document.h"
#include "kit_model.h"
#include "sample_browser.h"
#include "sample_slot.h"

namespace spdsx {

class MainComponent : public juce::Component,
                      public juce::ApplicationCommandTarget,
                      public juce::DragAndDropContainer,
                      private juce::Timer,
                      private juce::MidiInputCallback,
                      private KitModel::Listener {
public:
  static constexpr int kSlotCount = KitModel::kSlotCount;

  explicit MainComponent(juce::ApplicationCommandManager& commands);
  ~MainComponent() override;

  // Assigns a sample to a slot, via the model. Shared by --load,
  // drag-and-drop, and the file dialog.
  void LoadSample(int idx, const juce::File& file);

  // The document, for app-level flows (quit interception).
  KitDocument& document() { return document_; }

  void paint(juce::Graphics& g) override;
  void resized() override;
  bool keyPressed(const juce::KeyPress& key) override;
  bool keyStateChanged(bool is_key_down) override;
  void mouseDown(const juce::MouseEvent& event) override;

  ApplicationCommandTarget* getNextCommandTarget() override;
  void getAllCommands(juce::Array<juce::CommandID>& ids) override;
  void getCommandInfo(juce::CommandID id,
      juce::ApplicationCommandInfo& info) override;
  bool perform(const InvocationInfo& info) override;

private:
  void SampleChanged(int pad, int layer) override;
  void KitNameChanged() override;
  void PadParamsChanged(int pad) override;

  // Sets the settings storage parameters; must run before any member
  // that reads settings in its constructor.
  juce::ApplicationProperties& ConfigureSettings();

  // Opens every available MIDI input so a physical pad hit triggers its
  // grid pad. The SPD-SX PRO sends note-on on channel 10, pads 1-9 on
  // notes 60-68.
  void OpenMidiInputs();
  void handleIncomingMidiMessage(
      juce::MidiInput* source, const juce::MidiMessage& message) override;

  void ApplyTransportAction(int idx, TransportAction action);
  // A velocity-aware hit on a whole pad (MIDI note-on, mouse/space at
  // cursor-height velocity, or keys 1-9 at the header velocity): plays
  // the layers the pad's layer mode selects, at the gains it computes,
  // flashes the pad in the velocity colour, and tints each sounding
  // layer with its adjusted velocity. pedal_down is the hi-hat pedal.
  void TriggerPad(int pad, int velocity, bool pedal_down);
  // The pad containing a point (in our coordinates), or -1.
  int PadAt(juce::Point<int> point) const;
  // Mouse-as-velocity: the cursor's height within the pad, bottom = 1
  // (softest) up to top = 127 (hardest).
  int VelocityForPointInPad(int pad, juce::Point<int> point) const;
  // The H key is the hi-hat pedal. A press is a foot-close on every
  // HI-HAT pad: it cuts the open layer and sounds the closed one (the
  // "chick"); a release is silent. While held, hits play closed.
  void SetHiHatKeyDown(bool down);
  // Pedal state from any source: the H key, or MIDI CC4 >= 64.
  bool HiHatPedalDown() const;
  // Pushes the pad's layer widgets into the model as one undo step.
  void ApplyLayerParams(int pad);
  // Syncs the pad's layer widgets (and their visibility) from the model.
  void UpdatePadWidgets(int pad);
  // The pad's "..." menu: dynamics on/off, dynamics curve, trigger
  // reserve — the shared hit-response properties that don't earn
  // permanent header space.
  void ShowPadMenu(int pad);
  // Moves (or, when copy=true, duplicates) a slot's sample to another
  // slot as a single undoable action.
  void MoveSample(int from, int to, bool copy);
  // Same, but both layers of a whole pad (command-drag).
  void MovePad(int from_pad, int to_pad, bool copy);
  // Highlights the drag drop target: the hovered slot, plus its pad
  // sibling when whole_pad is set. idx<0 clears.
  void SetDragTarget(int idx, bool whole_pad);
  void SetBrowserVisible(bool visible);
  void RefreshDocumentState();
  void timerCallback() override;
  juce::Rectangle<int> GridArea() const;
  juce::Rectangle<int> PadBounds(int row, int col) const;

  juce::ApplicationCommandManager& commands_;
  KitModel model_;
  juce::UndoManager undo_;
  juce::ApplicationProperties settings_;
  KitDocument document_ {model_, undo_, settings_};
  AudioEngine engine_ {kSlotCount};
  std::array<std::unique_ptr<SampleSlot>, kSlotCount> slots_;
  // Per-pad layer controls, living in each pad's header row.
  std::array<std::unique_ptr<juce::ComboBox>, KitModel::kPadCount>
      mode_boxes_;
  std::array<std::unique_ptr<juce::Slider>, KitModel::kPadCount>
      fade_point_sliders_;
  std::array<std::unique_ptr<juce::Slider>, KitModel::kPadCount>
      fade_end_sliders_;
  std::array<std::unique_ptr<juce::TextButton>, KitModel::kPadCount>
      pad_menu_buttons_;
  // ALTERNATE mode's per-pad flip-flop (false = layer A fires next);
  // runtime state, deliberately not persisted.
  std::array<bool, KitModel::kPadCount> alternate_flip_ {};
  // Velocity-coloured pad flash: velocity of the last hit (0 = idle)
  // and when it landed; the timer fades and expires it.
  std::array<int, KitModel::kPadCount> pad_flash_velocity_ {};
  std::array<juce::uint32, KitModel::kPadCount> pad_flash_ms_ {};
  // Last seen hi-hat pedal position (MIDI CC4), written on the MIDI
  // thread; >= 64 means pedal down (closed).
  std::atomic<int> hihat_cc_ {0};
  // Whether the H key (the keyboard's hi-hat pedal) is held.
  bool hihat_key_down_ = false;
  // Trigger keys currently held, so OS auto-repeat doesn't machine-gun
  // pads: keyPressed consumes repeats while a key is marked held, and
  // keyStateChanged clears the mark on release.
  std::array<bool, KitModel::kPadCount> held_pad_keys_ {};
  bool held_space_ = false;
  // Velocity used by keyboard pad triggers (keys 1-9).
  juce::Slider velocity_slider_;
  juce::Label velocity_caption_;
  juce::Label name_label_;
  SampleBrowser browser_;
  std::vector<std::unique_ptr<juce::MidiInput>> midi_inputs_;
  bool browser_visible_ = true;
  int hovered_ = -1;
  bool could_undo_ = false;
  bool could_redo_ = false;
  bool shown_dirty_ = false;
};

}  // namespace spdsx

#endif  // SPDSX_PATCHEDIT_SOURCE_MAIN_COMPONENT_H_
