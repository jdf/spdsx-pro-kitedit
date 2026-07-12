// The whole-device mirror: every kit the hardware holds, as plain data.
// The active kit is edited live through the observable KitModel; the
// DeviceDocument stashes it back in here before serializing or switching
// kits.
#ifndef SPDSX_PATCHEDIT_SOURCE_DEVICE_MODEL_H_
#define SPDSX_PATCHEDIT_SOURCE_DEVICE_MODEL_H_

#include <array>
#include <vector>

#include "kit_model.h"

namespace spdsx {

// One kit as stored data.
struct KitData {
  KitData()
  {
    for (int i = 0; i < KitModel::kPadCount; ++i) {
      pads[static_cast<size_t>(i)].params = KitModel::DefaultParams(i);
    }
  }

  juce::String name {"USER KIT"};  // the device's own default kit name
  std::array<Pad, KitModel::kPadCount> pads;
};

class DeviceModel {
public:
  // The SPD-SX PRO holds 200 kits.
  static constexpr int kKitCount = 200;

  DeviceModel() : kits_(kKitCount) {}

  const KitData& kit(int idx) const
  {
    return kits_.at(static_cast<size_t>(idx));
  }
  KitData& kit(int idx) { return kits_.at(static_cast<size_t>(idx)); }

  // Which kit the KitModel is currently editing. View state, not
  // document content: switching kits doesn't dirty the document.
  int current_kit() const { return current_kit_; }
  void set_current_kit(int idx) { current_kit_ = idx; }

  void Reset()
  {
    kits_.assign(kKitCount, KitData());
    current_kit_ = 0;
  }

private:
  std::vector<KitData> kits_;
  int current_kit_ = 0;
};

}  // namespace spdsx

#endif  // SPDSX_PATCHEDIT_SOURCE_DEVICE_MODEL_H_
