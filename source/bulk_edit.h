// Bulk edits applied to the DOCUMENT's kits — the Bulk Edit tab's half
// of the operations spdutil runs against a device. Editing locally means
// the edits ride the normal path to hardware: the touched kits read
// dirty against the base snapshot, and Sync Changes with Device pushes
// them.
#ifndef SPDSX_PATCHEDIT_SOURCE_BULK_EDIT_H_
#define SPDSX_PATCHEDIT_SOURCE_BULK_EDIT_H_

#include <vector>

#include <juce_data_structures/juce_data_structures.h>

#include "device_document.h"
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

// One object's link-group change: a pad, or a trigger input.
struct LinkChange {
  int kit = 0;  // 1-based
  bool trigger = false;  // false: `index` is a pad 1-9; true: trigger 1-8
  int index = 0;
  int from = 0;  // the group it had (0 = unlinked)
};

// Which pads the request would change (those not already in the group).
std::vector<LinkChange> PlanPadLink(const std::vector<KitData>& kits,
                                    const ops::PadLinkRequest& request);

// The undoable form of a bulk layer-mode edit: perform sets every pad in
// the plan to the target, undo puts each back to what it was. Lands each
// touched kit through DeviceDocument::ApplyBulkKit, so the active kit's
// model reloads and the kits read dirty either way. Lives in the undo
// history of the kit that was active at apply time, like any other
// transaction.
class SetModeAction : public juce::UndoableAction {
public:
  SetModeAction(DeviceDocument& document,
                DeviceModel& device,
                std::vector<ops::ModeChange> plan,
                LayerMode target)
      : document_(document)
      , device_(device)
      , plan_(std::move(plan))
      , target_(target) {}

  bool perform() override;
  bool undo() override;

private:
  // Applies target (forward) or each change's `from` (backward) and
  // lands the touched kits.
  void ApplyModes(bool forward);

  DeviceDocument& document_;
  DeviceModel& device_;
  std::vector<ops::ModeChange> plan_;
  LayerMode target_;
};

// The undoable form of a bulk pad-link edit; same shape as SetModeAction.
class PadLinkAction : public juce::UndoableAction {
public:
  PadLinkAction(DeviceDocument& document,
                DeviceModel& device,
                std::vector<LinkChange> plan,
                int group)
      : document_(document)
      , device_(device)
      , plan_(std::move(plan))
      , group_(group) {}

  bool perform() override;
  bool undo() override;

private:
  void ApplyLinks(bool forward);

  DeviceDocument& document_;
  DeviceModel& device_;
  std::vector<LinkChange> plan_;
  int group_;
};

}  // namespace spdsx::bulk

#endif  // SPDSX_PATCHEDIT_SOURCE_BULK_EDIT_H_
