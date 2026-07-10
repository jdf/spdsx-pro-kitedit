// .kit file persistence via juce::FileBasedDocument, which supplies the
// dirty flag, save/open prompts, and "discard changes?" flows.
//
// The format is human-readable JSON: a kit name and an array of 9 pads,
// each with a two-entry samples array (top, bottom) of absolute sample
// paths or null. Samples are referenced, not copied. Kits saved in the
// earlier flat-"slots" format still load.
#ifndef SPDSX_PATCHEDIT_SOURCE_KIT_DOCUMENT_H_
#define SPDSX_PATCHEDIT_SOURCE_KIT_DOCUMENT_H_

#include <juce_gui_extra/juce_gui_extra.h>

#include "kit_model.h"

namespace spdsx {

// The .kit schema version, written to every saved file. Bump when the
// schema changes; loadDocument understands every older version and
// refuses files stamped newer than current.
enum class KitFormat : int {
  // The original flat 18-entry "slots" array. Files of this era carry
  // no version field on disk.
  kFlatSlots = 1,
  // Pad-shaped: nine {"samples": [top, bottom]} objects, ready for
  // per-pad properties. First version stamped on disk.
  kPads = 2,

  kCurrent = kPads,
};

class KitDocument : public juce::FileBasedDocument {
public:
  // settings persists "where kits live" across sessions, kept separate
  // from the sample chooser's directory memory.
  KitDocument(KitModel& model,
      juce::UndoManager& undo,
      juce::ApplicationProperties& settings);

  juce::String getDocumentTitle() override;

  // Resets to a fresh untitled kit (File > New, after any save prompt).
  void ResetToUntitled();

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

#endif  // SPDSX_PATCHEDIT_SOURCE_KIT_DOCUMENT_H_
