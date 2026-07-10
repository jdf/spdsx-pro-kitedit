// Left panel: a directory tree showing folders and audio files, from
// which samples drag onto slots.
#pragma once

#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

namespace spdsx {

class SampleBrowser : public juce::Component {
public:
  explicit SampleBrowser(juce::ApplicationProperties& settings);
  ~SampleBrowser() override;

  void resized() override;
  void paint(juce::Graphics& g) override;

private:
  void set_root(const juce::File& root, bool persist);
  void choose_root();

  juce::ApplicationProperties& settings_;
  juce::TimeSliceThread scan_thread_ {"sample-browser-scan"};
  std::unique_ptr<juce::WildcardFileFilter> filter_;
  std::unique_ptr<juce::DirectoryContentsList> contents_;
  std::unique_ptr<juce::FileTreeComponent> tree_;
  juce::Label root_label_;
  juce::TextButton root_button_ {"..."};
  std::unique_ptr<juce::FileChooser> chooser_;
};

}  // namespace spdsx
