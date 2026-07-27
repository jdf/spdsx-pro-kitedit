// The device operations that both front ends drive: spdutil's commands
// and the bulk-operations window run these, so a fix or a guard lands in
// both at once. Each takes a request struct, reports progress, and can be
// asked to stop; each can also render the spdutil command line it is
// equivalent to, which is what lets the window show you the command it is
// about to be.
//
// JUCE-free, so the planning half is testable without a device and the
// executing half against a fake port.
#ifndef SPDSX_PATCHEDIT_SOURCE_DEVICE_OPS_H_
#define SPDSX_PATCHEDIT_SOURCE_DEVICE_OPS_H_

#include <array>
#include <functional>
#include <string>
#include <vector>

#include "device/kit_image.h"
#include "device/spdsx_device.h"
#include "layers.h"
#include "spdutil_args.h"  // KitRange

namespace spdsx::ops {

// How far along a long operation is. `total` is 0 while it is still
// working that out.
struct Progress {
  int done = 0;
  int total = 0;
  std::string note;
};

using ProgressFn = std::function<void(const Progress&)>;
using AbortFn = std::function<bool()>;

inline void IgnoreProgress(const Progress&) {}

inline bool NeverAbort() {
  return false;
}

// ---- info ----

struct InfoRequest {};  // no options; exists so CommandLine has a request

struct InfoResult {
  std::string version;  // e.g. "2.00"; empty if the unit did not answer
  std::string build;  // e.g. "0094"
};

// Asks the unit its firmware version and build. Read-only.
InfoResult Info(device::SpdsxDevice& dev, ProgressFn progress = IgnoreProgress);

// ---- setmode ----

struct SetModeRequest {
  std::vector<spdutil::KitRange> kits;  // required: never implied
  std::vector<int> pads;  // 1-9; empty = all nine
  LayerMode target = LayerMode::kMix;
  bool has_if_mode = false;
  LayerMode if_mode = LayerMode::kMix;
  bool commit = false;
  bool dry_run = false;
};

// One pad the request would change.
struct ModeChange {
  int kit = 0;  // 1-based
  int pad = 0;  // 1-based
  LayerMode from = LayerMode::kMix;
};

// Which pads a request would change, given each kit's current modes
// (kits[i] is kit number i+1). Pure: no device, no I/O — the CLI, the
// Bulk Edit tab, and every dry run agree because they all come through
// here.
std::vector<ModeChange> PlanModeChanges(
    const std::vector<std::array<LayerMode, 9>>& kit_modes,
    const SetModeRequest& request);

// The same plan, read from parsed device records.
std::vector<ModeChange> PlanSetMode(const std::vector<device::KitRecord>& kits,
                                    const SetModeRequest& request);

struct SetModeResult {
  int changed = 0;  // pads that needed changing
  bool wrote = false;
  bool committed = false;
  bool aborted = false;
};

// Reads the kits bank, plans, and writes the pads that need it. A dry run
// stops after planning.
SetModeResult SetMode(device::SpdsxDevice& dev,
                      const SetModeRequest& request,
                      ProgressFn progress = IgnoreProgress,
                      AbortFn should_abort = NeverAbort);

// ---- setname ----

struct SetNameRequest {
  int kit = 0;  // 1-200
  std::string name;  // trimmed to the device's 16-character field
  bool commit = false;
  bool dry_run = false;
};

struct SetNameResult {
  bool wrote = false;
  bool committed = false;
};

SetNameResult SetName(device::SpdsxDevice& dev,
                      const SetNameRequest& request,
                      ProgressFn progress = IgnoreProgress,
                      AbortFn should_abort = NeverAbort);

// ---- the equivalent command line ----
//
// Rendered from the same request the operation runs, so what the window
// shows is what it does.
std::string CommandLine(const InfoRequest& request);
std::string CommandLine(const SetModeRequest& request);
std::string CommandLine(const SetNameRequest& request);

// A --kits spec rendered back from parsed ranges ("108", "1,5,10-20").
std::string KitSpecText(const std::vector<spdutil::KitRange>& kits);

}  // namespace spdsx::ops

#endif  // SPDSX_PATCHEDIT_SOURCE_DEVICE_OPS_H_
