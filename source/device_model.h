// The whole-device mirror: every kit the hardware holds, as plain data.
// The active kit is edited live through the observable KitModel; the
// DeviceDocument stashes it back in here before serializing or switching
// kits.
#ifndef SPDSX_PATCHEDIT_SOURCE_DEVICE_MODEL_H_
#define SPDSX_PATCHEDIT_SOURCE_DEVICE_MODEL_H_

#include <algorithm>
#include <array>
#include <vector>

#include "device/sample_image.h"
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

  // The device's wave pool directory (metadata only — audio transfer
  // is not cracked yet). Sorted by index; what pads' device_index
  // values refer to.
  const std::vector<device::SampleRecord>& sample_pool() const
  {
    return sample_pool_;
  }
  void set_sample_pool(std::vector<device::SampleRecord> pool)
  {
    sample_pool_ = std::move(pool);
  }
  const device::SampleRecord* FindSample(int index) const
  {
    const auto it = std::lower_bound(sample_pool_.begin(),
        sample_pool_.end(), index,
        [](const device::SampleRecord& r, int i) { return r.index < i; });
    return it != sample_pool_.end() && it->index == index ? &*it : nullptr;
  }

  void Reset()
  {
    kits_.assign(kKitCount, KitData());
    sample_pool_.clear();
    current_kit_ = 0;
  }

private:
  std::vector<KitData> kits_;
  std::vector<device::SampleRecord> sample_pool_;
  int current_kit_ = 0;
};

}  // namespace spdsx

#endif  // SPDSX_PATCHEDIT_SOURCE_DEVICE_MODEL_H_
