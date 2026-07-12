// The left panel's "Device" tab: the device's wave pool directory as a
// draggable list. Rows drag onto pad slots with the description
// "spdsx-devsample:<pool index>" (kDeviceSampleDragPrefix). Metadata
// only for now — audio preview arrives with the sample-transfer
// protocol.
#ifndef SPDSX_PATCHEDIT_SOURCE_DEVICE_SAMPLES_H_
#define SPDSX_PATCHEDIT_SOURCE_DEVICE_SAMPLES_H_

#include <juce_gui_basics/juce_gui_basics.h>

#include "device_model.h"

namespace spdsx {

class DeviceSamplePanel : public juce::Component,
                          public juce::ListBoxModel {
public:
  explicit DeviceSamplePanel(const DeviceModel& device);

  // Call after the pool changes (device fetch, open, new).
  void Refresh();

  // A transient status line ("connecting…", "loading… N blocks") shown
  // in place of the empty-pool hint; empty clears it.
  void SetStatus(const juce::String& status);

  void paint(juce::Graphics& g) override;
  void resized() override;

  // ListBoxModel:
  int getNumRows() override;
  void paintListBoxItem(int row, juce::Graphics& g, int width, int height,
      bool selected) override;
  juce::var getDragSourceDescription(
      const juce::SparseSet<int>& selected_rows) override;

private:
  const DeviceModel& device_;
  juce::ListBox list_;
  juce::String status_;
};

}  // namespace spdsx

#endif  // SPDSX_PATCHEDIT_SOURCE_DEVICE_SAMPLES_H_
