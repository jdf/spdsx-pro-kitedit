// The main window content: a 3x3 grid of pads labeled 1-9, each with a
// top and a bottom sample slot (18 slots, indexed (row * 3 + col) * 2,
// +1 for the bottom).
#ifndef SPDSX_PATCHEDIT_SOURCE_MAIN_COMPONENT_H_
#define SPDSX_PATCHEDIT_SOURCE_MAIN_COMPONENT_H_

#include <array>
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

  ApplicationCommandTarget* getNextCommandTarget() override;
  void getAllCommands(juce::Array<juce::CommandID>& ids) override;
  void getCommandInfo(juce::CommandID id,
      juce::ApplicationCommandInfo& info) override;
  bool perform(const InvocationInfo& info) override;

private:
  void SampleChanged(int pad, int layer) override;
  void KitNameChanged() override;

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
  // Drum-pad trigger (spacebar, slot-body clicks, MIDI): retrigger from
  // the top while playing, resume while paused, start when stopped.
  void TriggerSlot(int idx);
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
