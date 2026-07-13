#include "device_samples.h"

#include "audio_files.h"

namespace spdsx {

namespace {

const juce::Colour kName(0xffe6edf5);
const juce::Colour kMeta(0xff8a97a6);
const juce::Colour kRowSelected(0xff2b3642);
const juce::Colour kFilterBg(0xff1b212a);

constexpr int kRowHeight = 22;
constexpr int kFilterHeight = 24;

}  // namespace

DeviceSamplePanel::DeviceSamplePanel(
    const DeviceModel& device, juce::ApplicationProperties& settings)
    : device_(device)
    , settings_(settings)
{
  filter_.setTextToShowWhenEmpty(
      juce::String::fromUTF8("filter\xe2\x80\xa6"), kMeta);
  filter_.setColour(juce::TextEditor::backgroundColourId, kFilterBg);
  filter_.onTextChange = [this]
  {
    RebuildRows();
    list_.updateContent();
    repaint();
  };
  addAndMakeVisible(filter_);

  list_.setModel(this);
  list_.setRowHeight(kRowHeight);
  list_.setMultipleSelectionEnabled(false);
  list_.setColour(
      juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
  addAndMakeVisible(list_);
}

void DeviceSamplePanel::Refresh()
{
  RebuildRows();
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

void DeviceSamplePanel::RebuildRows()
{
  const auto& pool = device_.sample_pool();
  const juce::String query = filter_.getText().trim();
  rows_.clear();
  rows_.reserve(pool.size());
  for (size_t i = 0; i < pool.size(); ++i) {
    if (query.isEmpty()
        || juce::String(pool[i].wavename).containsIgnoreCase(query)
        || juce::String(pool[i].filename).containsIgnoreCase(query))
    {
      rows_.push_back(i);
    }
  }
}

const device::SampleRecord* DeviceSamplePanel::RecordForRow(int row) const
{
  if (row < 0 || row >= static_cast<int>(rows_.size())) {
    return nullptr;
  }
  return &device_.sample_pool()[rows_[static_cast<size_t>(row)]];
}

void DeviceSamplePanel::paint(juce::Graphics& g)
{
  g.setColour(kMeta);
  g.setFont(13.0f);
  if (status_.isNotEmpty()) {
    g.drawFittedText(status_, getLocalBounds().reduced(12),
        juce::Justification::centred, 4);
  } else if (device_.sample_pool().empty()) {
    g.drawFittedText(
        "no device samples\n\nFile > Load Samples from Device",
        getLocalBounds().reduced(12), juce::Justification::centred, 6);
  } else if (rows_.empty()) {
    g.drawText("no matches", getLocalBounds().reduced(12),
        juce::Justification::centred);
  }
}

void DeviceSamplePanel::resized()
{
  auto area = getLocalBounds();
  filter_.setBounds(area.removeFromTop(kFilterHeight).reduced(4, 2));
  list_.setBounds(area);
}

int DeviceSamplePanel::getNumRows()
{
  return static_cast<int>(rows_.size());
}

void DeviceSamplePanel::paintListBoxItem(
    int row, juce::Graphics& g, int width, int height, bool selected)
{
  const auto* rec = RecordForRow(row);
  if (rec == nullptr) {
    return;
  }
  if (selected) {
    g.fillAll(kRowSelected);
  }
  auto area = juce::Rectangle<int>(0, 0, width, height).reduced(8, 0);
  g.setFont(12.0f);
  g.setColour(kMeta);
  const auto cat = device::SampleCategoryName(rec->category);
  g.drawText(juce::String(cat.data(), cat.size()),
      area.removeFromRight(width / 3), juce::Justification::centredRight);
  // Factory preloads read dimmer than user imports, a simple at-a-
  // glance distinction (both are cacheable/playable once downloaded).
  g.setColour(rec->is_preload() ? kMeta : kName);
  g.drawText(juce::String(rec->wavename), area,
      juce::Justification::centredLeft);
}

juce::var DeviceSamplePanel::getDragSourceDescription(
    const juce::SparseSet<int>& selected_rows)
{
  if (selected_rows.size() != 1) {
    return {};
  }
  const auto* rec = RecordForRow(selected_rows[0]);
  if (rec == nullptr) {
    return {};
  }
  return juce::String(kDeviceSampleDragPrefix) + juce::String(rec->index);
}

void DeviceSamplePanel::selectedRowsChanged(int last_row_selected)
{
  if (!settings_.getUserSettings()->getBoolValue("autoplayBrowsing", false))
  {
    return;
  }
  if (const auto* rec = RecordForRow(last_row_selected);
      rec != nullptr && on_preview)
  {
    on_preview(*rec);
  }
}

}  // namespace spdsx
