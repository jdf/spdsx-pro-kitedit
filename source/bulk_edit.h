// Bulk edits applied to the DOCUMENT's kits — the Bulk Edit tab's half
// of the operations spdutil runs against a device. Editing locally means
// the edits ride the normal path to hardware: the touched kits read
// dirty against the base snapshot, and Sync Changes with Device pushes
// them.
#ifndef SPDSX_PATCHEDIT_SOURCE_BULK_EDIT_H_
#define SPDSX_PATCHEDIT_SOURCE_BULK_EDIT_H_

#include <vector>

#include "device_model.h"  // KitData
#include "device_ops.h"

namespace spdsx::bulk {

// Which pads the request would change (kits[i] is kit number i+1).
std::vector<ops::ModeChange> PlanSetMode(const std::vector<KitData>& kits,
                                         const ops::SetModeRequest& request);

// Applies the plan the request produces; returns the changed pads.
// Touches nothing else about a pad.
std::vector<ops::ModeChange> ApplySetMode(std::vector<KitData>& kits,
                                          const ops::SetModeRequest& request);

}  // namespace spdsx::bulk

#endif  // SPDSX_PATCHEDIT_SOURCE_BULK_EDIT_H_
