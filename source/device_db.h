// The document store: a SQLite database holding the whole-device mirror.
//
// One `*.spdsx` file is a database with kits + pads (kept as a 'current'
// snapshot and, for sync, a 'base' last-synced snapshot) and the sample
// pool directory with cached audio as BLOBs. This replaces the old
// folder-package + device.json + samples/ layout. The in-memory model
// (DeviceModel/KitModel) is unchanged; this is only persistence.
#ifndef SPDSX_PATCHEDIT_SOURCE_DEVICE_DB_H_
#define SPDSX_PATCHEDIT_SOURCE_DEVICE_DB_H_

#include <memory>

#include <juce_core/juce_core.h>

#include "device_model.h"

struct sqlite3;

namespace spdsx {

// Which stored copy of the kits: the working document, or the last-synced
// base used for three-way conflict detection.
enum class Snapshot { kCurrent, kBase };

class DeviceDb {
public:
  ~DeviceDb();
  DeviceDb(const DeviceDb&) = delete;
  DeviceDb& operator=(const DeviceDb&) = delete;

  // Opens (creating and initializing the schema if new) the database at
  // `path`. Returns null and fills `error` on failure.
  static std::unique_ptr<DeviceDb> Open(const juce::File& path,
      juce::String& error);

  // Reads a snapshot's 200 kits (names + pads) into the model's kit
  // array. kCurrent also loads the current-kit index and the sample pool.
  void ReadKits(DeviceModel& model, Snapshot snapshot = Snapshot::kCurrent);

  // Writes the model's kits + current-kit index into a snapshot, in one
  // transaction. Does not touch the sample pool or audio blobs.
  void WriteKits(const DeviceModel& model,
      Snapshot snapshot = Snapshot::kCurrent);

  // Replaces the stored sample-pool directory metadata with the model's,
  // preserving any audio blobs already cached for those indices.
  void WritePool(const DeviceModel& model);

  // Cached sample audio (a WAV image), keyed by pool index.
  bool HasAudio(int sample_index);
  juce::MemoryBlock GetAudio(int sample_index);  // empty if none
  void PutAudio(int sample_index, const void* data, size_t bytes);

  // The schema/document version stored in the meta table (0 if absent).
  // The loader can refuse to open a file newer than kCurrentSchemaVersion.
  static constexpr int kCurrentSchemaVersion = 1;
  int SchemaVersion();

  // Copies the current snapshot onto the base snapshot — the clean point
  // a sync establishes (after Load Device State or a successful push).
  void CaptureBase();

private:
  explicit DeviceDb(sqlite3* db) : db_(db) {}
  sqlite3* db_ = nullptr;
};

}  // namespace spdsx

#endif  // SPDSX_PATCHEDIT_SOURCE_DEVICE_DB_H_
