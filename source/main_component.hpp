// The main window content: a 3x3 grid of pads labeled 1-9, each with a
// top and a bottom sample slot (18 slots, indexed (row * 3 + col) * 2,
// +1 for the bottom).
#pragma once

#include <array>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "audio.hpp"
#include "kit_document.hpp"
#include "kit_model.hpp"
#include "sample_browser.hpp"
#include "sample_slot.hpp"

namespace spdsx {

class MainComponent : public juce::Component,
                      public juce::ApplicationCommandTarget,
                      public juce::DragAndDropContainer,
                      private juce::Timer,
                      private KitModel::Listener {
public:
  static constexpr int kSlotCount = KitModel::kSlotCount;

  explicit MainComponent(juce::ApplicationCommandManager& commands);
  ~MainComponent() override;

  // Assigns a sample to a slot, via the model. Shared by --load,
  // drag-and-drop, and the file dialog.
  void load_sample(int idx, const juce::File& file);

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
  void slot_changed(int idx) override;
  void kit_name_changed() override;

  // Sets the settings storage parameters; must run before any member
  // that reads settings in its constructor.
  juce::ApplicationProperties& configure_settings();

  void transport_action(int idx, TransportAction action);
  void choose_sample(int idx);
  void set_browser_visible(bool visible);
  void refresh_document_state();
  void timerCallback() override;
  juce::Rectangle<int> grid_area() const;
  juce::Rectangle<int> pad_bounds(int row, int col) const;

  juce::ApplicationCommandManager& commands_;
  KitModel model_;
  juce::UndoManager undo_;
  juce::ApplicationProperties settings_;
  KitDocument document_ {model_, undo_, settings_};
  AudioEngine engine_ {kSlotCount};
  std::unique_ptr<juce::FileChooser> chooser_;
  std::array<std::unique_ptr<SampleSlot>, kSlotCount> slots_;
  juce::Label name_label_;
  SampleBrowser browser_;
  bool browser_visible_ = true;
  juce::File last_sample_dir_;
  int hovered_ = -1;
  bool could_undo_ = false;
  bool could_redo_ = false;
  bool shown_dirty_ = false;
};

}  // namespace spdsx
