// Whole-device persistence: the document is a FOLDER named *.spdsx
// (declared as a macOS package in the app's Info.plist, so Finder shows
// it as a single file) holding device.json — every kit on the device —
// plus, eventually, a samples/ cache of audio pulled from the hardware.
//
// The manifest is human-readable JSON: a format version and 200 kits,
// each a name and nine pads in the same shape the old single-kit .kit
// format used. Sample paths stay absolute references for now; pool
// indices join them when device sample extraction lands.
#ifndef SPDSX_PATCHEDIT_SOURCE_DEVICE_DOCUMENT_H_
#define SPDSX_PATCHEDIT_SOURCE_DEVICE_DOCUMENT_H_

#include <juce_gui_extra/juce_gui_extra.h>

#include "device_model.h"
#include "kit_model.h"

namespace spdsx {

// The device.json schema version. Bump when the schema changes;
// loadDocument refuses files stamped newer than current.
enum class DeviceFormat : int {
  kInitial = 1,

  kCurrent = kInitial,
};

class DeviceDocument : public juce::FileBasedDocument {
public:
  DeviceDocument(DeviceModel& device,
      KitModel& model,
      juce::UndoManager& undo,
      juce::ApplicationProperties& settings);

  juce::String getDocumentTitle() override;

  // Resets to a fresh untitled device (File > New, after any save
  // prompt).
  void ResetToUntitled();

  // Stashes the active kit and makes another one active. A pure view
  // change: the dirty flag is preserved; undo history clears (undoing
  // across kits would be nonsense).
  void SwitchKit(int index);

  // Copies the live KitModel back into the device store; called before
  // serializing or switching kits.
  void StashActiveKit();

  // Reads a legacy single-kit .kit file (v1..v4) into the active kit.
  juce::Result ImportKitFile(const juce::File& file);

  // Opens a device folder (or its device.json directly). All device
  // opening goes through here: FileBasedDocument::loadFrom insists the
  // target existsAsFile(), which a folder document never satisfies.
  juce::Result OpenDevice(const juce::File& file);

protected:
  juce::Result loadDocument(const juce::File& file) override;
  juce::Result saveDocument(const juce::File& file) override;
  juce::File getLastDocumentOpened() override;
  void setLastDocumentOpened(const juce::File& file) override;

private:
  void LoadActiveKitIntoModel();

  DeviceModel& device_;
  KitModel& model_;
  juce::UndoManager& undo_;
  juce::ApplicationProperties& settings_;
};

}  // namespace spdsx

#endif  // SPDSX_PATCHEDIT_SOURCE_DEVICE_DOCUMENT_H_
