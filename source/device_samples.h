// The left panel's "Device" tab: the device's wave pool directory as a
// draggable, filterable list. Rows drag onto pad slots with the
// description "spdsx-devsample:<pool index>" (kDeviceSampleDragPrefix).
// Metadata only for now — selection preview is plumbed (and follows the
// View > Autoplay setting like the file browser) but stays silent until
// the sample-transfer protocol delivers audio.
#ifndef SPDSX_PATCHEDIT_SOURCE_DEVICE_SAMPLES_H_
#define SPDSX_PATCHEDIT_SOURCE_DEVICE_SAMPLES_H_

#include <functional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "device_model.h"

namespace spdsx {

class DeviceSamplePanel : public juce::Component,
                          public juce::ListBoxModel {
public:
  DeviceSamplePanel(
      const DeviceModel& device, juce::ApplicationProperties& settings);

  // A row was selected while View > Autoplay is on (the same setting
  // the file browser honors). No audio exists locally yet; the handler
  // grows a real preview when the sample cache lands.
  std::function<void(const device::SampleRecord&)> on_preview;

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
  void selectedRowsChanged(int last_row_selected) override;

private:
  // Recomputes rows_ from the pool and the filter text.
  void RebuildRows();
  const device::SampleRecord* RecordForRow(int row) const;

  const DeviceModel& device_;
  juce::ApplicationProperties& settings_;
  juce::TextEditor filter_;
  // Positions into sample_pool() passing the current filter.
  std::vector<size_t> rows_;
  juce::ListBox list_;
  juce::String status_;
};

}  // namespace spdsx

#endif  // SPDSX_PATCHEDIT_SOURCE_DEVICE_SAMPLES_H_
