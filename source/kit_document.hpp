// .kit file persistence via juce::FileBasedDocument, which supplies the
// dirty flag, save/open prompts, and "discard changes?" flows.
//
// The format is human-readable JSON: a kit name and an array of 18 slot
// entries (interleaved top/bottom in slot-index order), each an absolute
// sample path or null. Samples are referenced, not copied.
#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include "kit_model.hpp"

namespace spdsx {

class KitDocument : public juce::FileBasedDocument {
public:
  // settings persists "where kits live" across sessions, kept separate
  // from the sample chooser's directory memory.
  KitDocument(KitModel& model,
      juce::UndoManager& undo,
      juce::ApplicationProperties& settings);

  juce::String getDocumentTitle() override;

  // Resets to a fresh untitled kit (File > New, after any save prompt).
  void reset_to_untitled();

protected:
  juce::Result loadDocument(const juce::File& file) override;
  juce::Result saveDocument(const juce::File& file) override;
  juce::File getLastDocumentOpened() override;
  void setLastDocumentOpened(const juce::File& file) override;

private:
  KitModel& model_;
  juce::UndoManager& undo_;
  juce::ApplicationProperties& settings_;
};

}  // namespace spdsx
