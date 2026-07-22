// The three-way device sync behind "Save Changes to Device" — pure
// logic, no I/O, so the whole flow is testable without hardware.
//
// A sync compares three copies of every kit: `current` (the document as
// edited), `base` (the last-synced snapshot), and `theirs` (a fresh read
// off the hardware). Each pad merges FIELD-wise: a field changed on one
// side takes that side's value; changed on both sides to the same value
// it converges; changed to different values it is a conflict. Conflicts
// are resolved per PAD (the kit name is its own one-field item): my copy
// wins, device wins, or do nothing — where "do nothing" leaves the pad
// and its base untouched, so it re-flags on the next sync.
//
// The push side: local-file layers upload to free pool indices first
// (becoming device waves), then the kit writes go out (name, wave
// assignments, layer params), then one flash Commit; the caller advances
// the base snapshots only after that Commit succeeds.
#ifndef SPDSX_PATCHEDIT_SOURCE_DEVICE_SYNC_H_
#define SPDSX_PATCHEDIT_SOURCE_DEVICE_SYNC_H_

#include <array>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "device/kit_image.h"
#include "device/sample_image.h"
#include "device/spdsx_device.h"
#include "device_model.h"
#include "kit_model.h"

namespace spdsx {

// The user's answer for one conflicted item. kSkip = "do nothing": leave
// the two sides different and the base unadvanced.
enum class SyncResolution {
  kMine,
  kTheirs,
  kSkip
};

// Maps a parsed device kit record onto the app model (shared with Load
// Device State).
KitData KitDataFromDevice(const device::KitRecord& rec);

// A pad's params and wave numbers as the device write ops want them.
// Layers still holding a local file map to wave 0 — uploads substitute
// pool indices before any write is built.
device::PadDeviceParams DeviceParamsFromPad(const Pad& pad);

// Field-wise three-way merge of one pad. A conflicted field takes
// `current` when mine_wins, else `theirs`; when `conflicts` is given,
// each conflicted field appends a "field (yours X, device Y)" line.
Pad MergePad(const Pad& current,
             const Pad& base,
             const Pad& theirs,
             bool mine_wins,
             juce::StringArray* conflicts = nullptr);

// The conflicted fields of one pad; empty = it merges cleanly.
juce::StringArray PadConflicts(const Pad& current,
                               const Pad& base,
                               const Pad& theirs);

// One item for the resolution dialog: a pad, or the kit name (pad == -1).
struct SyncConflict {
  int kit = 0;  // 0-based kit index
  int pad = -1;  // 0-based pad, or -1 for the kit name
  juce::String description;  // dialog-ready, both sides' values included
};

// Every conflict in one kit, dialog-ready. `kit` is the 0-based index
// (only used to label and address the conflicts).
std::vector<SyncConflict> FindKitConflicts(int kit,
                                           const KitData& current,
                                           const KitData& base,
                                           const KitData& theirs);

// What one kit's sync does, once resolutions are in.
struct KitSyncPlan {
  KitData new_current;  // document content after the sync
  KitData new_base;  // base after the sync (skipped items keep the old base)
  bool write_name = false;
  std::array<bool, KitModel::kPadCount> write_params {};
  std::array<std::array<bool, KitModel::kLayersPerPad>, KitModel::kPadCount>
      write_wave {};
  bool skipped = false;  // some conflict was left unresolved ("do nothing")

  // Whether the device needs any write for this kit.
  bool WritesDevice() const;
};

// Plans one kit's sync. Resolutions matter only for conflicted items;
// clean merges ignore them.
KitSyncPlan PlanKitSync(
    const KitData& current,
    const KitData& base,
    const KitData& theirs,
    SyncResolution name_resolution,
    const std::array<SyncResolution, KitModel::kPadCount>& pad_resolutions);

// A local file scheduled to become a pool wave.
struct UploadPlan {
  juce::File file;
  int index = 0;  // the free pool index it will occupy
  std::string wavename;  // 16-char display name (basename, sanitized)
  std::string filename;  // directory-record file name (basename, sanitized)
};

// The lowest pool index above `after` with no directory record (the pool
// must be in index order, as ParseSampleDir returns it). 0 = pool full.
int NextFreeSampleIndex(const std::vector<device::SampleRecord>& pool,
                        int after = 0);

// The distinct local files referenced by layers the plans will write,
// each assigned a free pool index (in file-path order).
std::vector<UploadPlan> PlanUploads(
    const std::vector<std::pair<int, KitSyncPlan>>& plans,
    const std::vector<device::SampleRecord>& pool);

// Replaces planned files with their pool waves throughout the plans'
// snapshots, so every layer the push writes refers to a device index.
void SubstituteUploads(std::vector<std::pair<int, KitSyncPlan>>& plans,
                       const std::vector<UploadPlan>& uploads);

// ---- The push (drivable against a fake serial port) ----

// One wave ready to send: the converted .SMP image plus its directory
// names, bound for `index`.
struct SmpUpload {
  int index = 0;
  device::Bytes smp;
  std::string wavename;
  std::string filename;
};

// The wire-level writes for one kit.
struct PadWrite {
  int pad = 0;  // 1-based
  bool params = false;  // SetPadLayerParams needed
  std::array<bool, KitModel::kLayersPerPad> wave {};  // SetPadWave per layer
  device::PadDeviceParams dp;  // params + wave numbers to write
};

struct KitWrite {
  int kit = 0;  // 1-based device kit number
  bool name = false;
  std::string kit_name;
  std::vector<PadWrite> pads;
};

// Builds a kit's wire-level writes from its plan (0-based kit index).
// Call after SubstituteUploads so wave numbers are pool indices.
KitWrite BuildKitWrite(int kit_index, const KitSyncPlan& plan);

// Default no-op for ExecutePush's on_uploaded (a named function is a safe
// FunctionRef default).
inline void IgnoreUpload(const SmpUpload&) {}

// Drives the push: primes the batch, uploads each wave into working state
// (on_uploaded fires after each lands), writes every kit's name/wave/params,
// then ONE flash Commit to make the whole batch durable. Returns the Commit
// verdict (true when there was nothing to commit); throws what the port
// throws. should_abort lets a caller stop waiting on the commit (a user
// Abort) — a false return then makes no claim it finished.
bool ExecutePush(
    device::SpdsxDevice& dev,
    const std::vector<SmpUpload>& uploads,
    const std::vector<KitWrite>& kits,
    absl::FunctionRef<void(const SmpUpload&)> on_uploaded = IgnoreUpload,
    double pace_seconds = 0.02,
    absl::FunctionRef<bool()> should_abort = device::NeverAbort);

}  // namespace spdsx

#endif  // SPDSX_PATCHEDIT_SOURCE_DEVICE_SYNC_H_
