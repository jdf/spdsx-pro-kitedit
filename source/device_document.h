// Whole-device persistence: the document is a single `*.spdsx` file, a
// SQLite database (see device_db.h) holding every kit + the sample-pool
// directory with cached audio as BLOBs. The in-memory model
// (DeviceModel/KitModel) is unchanged; this class bridges it to the DB.
#ifndef SPDSX_PATCHEDIT_SOURCE_DEVICE_DOCUMENT_H_
#define SPDSX_PATCHEDIT_SOURCE_DEVICE_DOCUMENT_H_

#include <memory>
#include <vector>

#include <juce_gui_extra/juce_gui_extra.h>

#include "device/kit_image.h"
#include "device_db.h"
#include "device_model.h"
#include "kit_model.h"

namespace spdsx {

class DeviceDocument : public juce::FileBasedDocument {
public:
  DeviceDocument(DeviceModel& device,
                 KitModel& model,
                 juce::ApplicationProperties& settings);

  // Fired when every undo history must go (open/new/import replaced the
  // content wholesale). Kit switches deliberately do NOT fire it: undo
  // histories are per-kit and survive switching.
  std::function<void()> on_history_reset;

  // Fired with true just before and false just after the model is
  // reloaded from stored kit data (kit switch, open, load, reset). The UI
  // uses it to tell a load's change-listener storm apart from real user
  // edits (e.g. so it doesn't mark a kit dirty-vs-device on load).
  std::function<void(bool loading)> on_model_reload;

  juce::String getDocumentTitle() override;

  // Resets to a fresh untitled device (File > New, after any save
  // prompt).
  void ResetToUntitled();

  // Stashes the active kit and makes another one active. A pure view
  // change: the dirty flag is preserved, and per-kit undo histories
  // are untouched.
  void SwitchKit(int index);

  // Copies the live KitModel back into the device store; called before
  // serializing or switching kits.
  void StashActiveKit();

  // Whether the document holds cached audio for a pool wave.
  bool HasCachedAudio(int sample_index) const;
  // A playable WAV file for a cached pool wave, extracted on demand from
  // the DB blob into a temp cache. Invalid File if not cached.
  juce::File CachedWaveFile(int sample_index);
  // Stores a downloaded wave's WAV image as the pool wave's audio blob.
  void StoreWaveAudio(int sample_index, const juce::MemoryBlock& wav);

  // Reads a legacy single-kit .kit file (v1..v4) into the active kit.
  juce::Result ImportKitFile(const juce::File& file);

  // Replaces the WHOLE document content — all 200 kits (names, wave
  // assignments, layer params) and the sample pool — with a freshly
  // fetched device state. Deliberately not undoable: histories are
  // cleared. The caller confirms with the user first.
  void ReplaceWithDeviceState(const std::vector<device::KitRecord>& kits,
                              std::vector<device::SampleRecord> pool);

  // ---- Three-way device sync ----
  // A kit's current content; the live model when the kit is active.
  KitData KitContent(int index) const;
  // The last-synced base copy of a kit — what the device held after the
  // last Load Device State or successful push.
  const KitData& BaseKit(int index) const;
  // True when a kit's content differs from its base: it has changes not
  // yet pushed to the device (or pulled from it).
  bool KitDirtyVsBase(int index) const;
  // Every kit index where that's true.
  std::vector<int> DirtyKits() const;
  // Lands one kit's sync outcome: the merged content and the base it
  // advances to. Reloads the model when the kit is active (a load, not
  // a user edit). PersistSync writes the batch.
  void ApplySyncedKit(int index, const KitData& current, const KitData& base);
  // Writes both snapshots to the DB after a batch of ApplySyncedKit.
  void PersistSync();
  // Replaces every current-snapshot layer holding `file` with the pool
  // wave it became. An upload is durable the moment it lands; recording
  // it here keeps a sync that fails later from re-uploading the file.
  void ReplaceFileLayers(const juce::File& file, int device_index);
  // Replaces the pool directory metadata (a fresh device read) and
  // persists it; cached audio blobs for surviving indices are kept.
  void UpdateSamplePool(std::vector<device::SampleRecord> pool);
  // Adds (or overwrites) one pool record — a just-registered upload —
  // and persists the pool.
  void AddPoolRecord(const device::SampleRecord& record);

  // Opens a device folder (or its device.json directly). All device
  // opening goes through here: FileBasedDocument::loadFrom insists the
  // target existsAsFile(), which a folder document never satisfies.
  juce::Result OpenDevice(const juce::File& file);

  // Creates a fresh device document at the given folder and saves it
  // immediately, so autosave has a target from the first edit.
  juce::Result CreateNew(const juce::File& folder);

  // Writes straight back to the document's folder. There is no explicit
  // save: callers flush after edits (debounced) and at quit. No-op
  // while untitled (only --load test runs get into that state).
  void Autosave();

protected:
  juce::Result loadDocument(const juce::File& file) override;
  juce::Result saveDocument(const juce::File& file) override;
  juce::File getLastDocumentOpened() override;
  void setLastDocumentOpened(const juce::File& file) override;

private:
  void LoadActiveKitIntoModel();
  void ResetHistory();
  // Opens (creating if new) the DB at `file`, replacing db_.
  juce::Result OpenDb(const juce::File& file);

  DeviceModel& device_;
  KitModel& model_;
  juce::ApplicationProperties& settings_;
  // The open document's database (null while untitled).
  std::unique_ptr<DeviceDb> db_;
  // The in-memory copy of the base snapshot (kits only; the pool is
  // device-owned and has no base). Mirrors the DB's base rows so dirty
  // checks don't hit SQLite on every edit.
  DeviceModel base_;
};

}  // namespace spdsx

#endif  // SPDSX_PATCHEDIT_SOURCE_DEVICE_DOCUMENT_H_
