#include "device_samples.h"

#include "audio_files.h"

namespace spdsx {

namespace {

const juce::Colour kName(0xffe6edf5);
const juce::Colour kMeta(0xff8a97a6);
const juce::Colour kRowSelected(0xff2b3642);

constexpr int kRowHeight = 22;

}  // namespace

DeviceSamplePanel::DeviceSamplePanel(const DeviceModel& device)
    : device_(device)
{
  list_.setModel(this);
  list_.setRowHeight(kRowHeight);
  list_.setMultipleSelectionEnabled(false);
  list_.setColour(
      juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
  addAndMakeVisible(list_);
}

void DeviceSamplePanel::Refresh()
{
  list_.updateContent();
  repaint();
}

void DeviceSamplePanel::SetStatus(const juce::String& status)
{
  if (status_ != status) {
    status_ = status;
    repaint();
  }
}

void DeviceSamplePanel::paint(juce::Graphics& g)
{
  if (status_.isNotEmpty()) {
    g.setColour(kMeta);
    g.setFont(13.0f);
    g.drawFittedText(status_, getLocalBounds().reduced(12),
        juce::Justification::centred, 4);
  } else if (device_.sample_pool().empty()) {
    g.setColour(kMeta);
    g.setFont(13.0f);
    g.drawFittedText(
        "no device samples\n\nFile > Load Samples from Device",
        getLocalBounds().reduced(12), juce::Justification::centred, 6);
  }
}

void DeviceSamplePanel::resized()
{
  list_.setBounds(getLocalBounds());
}

int DeviceSamplePanel::getNumRows()
{
  return static_cast<int>(device_.sample_pool().size());
}

void DeviceSamplePanel::paintListBoxItem(
    int row, juce::Graphics& g, int width, int height, bool selected)
{
  const auto& pool = device_.sample_pool();
  if (row < 0 || row >= static_cast<int>(pool.size())) {
    return;
  }
  const auto& rec = pool[static_cast<size_t>(row)];
  if (selected) {
    g.fillAll(kRowSelected);
  }
  auto area = juce::Rectangle<int>(0, 0, width, height).reduced(8, 0);
  g.setFont(12.0f);
  g.setColour(kMeta);
  const auto cat = device::SampleCategoryName(rec.category);
  g.drawText(juce::String(cat.data(), cat.size()),
      area.removeFromRight(width / 3), juce::Justification::centredRight);
  g.setColour(kName);
  g.drawText(juce::String(rec.wavename), area,
      juce::Justification::centredLeft);
}

juce::var DeviceSamplePanel::getDragSourceDescription(
    const juce::SparseSet<int>& selected_rows)
{
  const auto& pool = device_.sample_pool();
  if (selected_rows.size() != 1) {
    return {};
  }
  const int row = selected_rows[0];
  if (row < 0 || row >= static_cast<int>(pool.size())) {
    return {};
  }
  return juce::String(kDeviceSampleDragPrefix)
      + juce::String(pool[static_cast<size_t>(row)].index);
}

}  // namespace spdsx
