#include "bulk_edit.h"

#include <array>
#include <set>

namespace spdsx::bulk {
namespace {

std::vector<std::array<LayerMode, 9>> ModesOf(
    const std::vector<KitData>& kits) {
  std::vector<std::array<LayerMode, 9>> kit_modes;
  kit_modes.reserve(kits.size());
  for (const KitData& kit : kits) {
    std::array<LayerMode, 9> modes {};
    for (size_t pad = 0; pad < modes.size(); ++pad) {
      modes[pad] = kit.pads[pad].params.mode;
    }
    kit_modes.push_back(modes);
  }
  return kit_modes;
}

}  // namespace

std::vector<ops::ModeChange> PlanSetMode(const std::vector<KitData>& kits,
                                         const ops::SetModeRequest& request) {
  return ops::PlanModeChanges(ModesOf(kits), request);
}

std::vector<ops::ModeChange> ApplySetMode(std::vector<KitData>& kits,
                                          const ops::SetModeRequest& request) {
  const std::vector<ops::ModeChange> plan = PlanSetMode(kits, request);
  for (const ops::ModeChange& change : plan) {
    kits[static_cast<size_t>(change.kit - 1)]
        .pads[static_cast<size_t>(change.pad - 1)]
        .params.mode = request.target;
  }
  return plan;
}

bool SetModeAction::perform() {
  ApplyModes(true);
  return true;
}

bool SetModeAction::undo() {
  ApplyModes(false);
  return true;
}

void SetModeAction::ApplyModes(bool forward) {
  std::set<int> touched;
  for (const ops::ModeChange& change : plan_) {
    touched.insert(change.kit);
  }
  for (const int kit : touched) {
    KitData content = device_.kit(kit - 1);
    for (const ops::ModeChange& change : plan_) {
      if (change.kit == kit) {
        content.pads[static_cast<size_t>(change.pad - 1)].params.mode =
            forward ? target_ : change.from;
      }
    }
    document_.ApplyBulkKit(kit - 1, content);
  }
}

}  // namespace spdsx::bulk
