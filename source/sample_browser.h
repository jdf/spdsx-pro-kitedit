// Left panel: a directory tree showing folders and audio files, from
// which samples drag onto slots.
#ifndef SPDSX_PATCHEDIT_SOURCE_SAMPLE_BROWSER_H_
#define SPDSX_PATCHEDIT_SOURCE_SAMPLE_BROWSER_H_

#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

namespace spdsx {

class SampleBrowser : public juce::Component,
                      private juce::FileBrowserListener {
public:
  explicit SampleBrowser(juce::ApplicationProperties& settings);
  ~SampleBrowser() override;

  // Called with the selected audio file when browser autoplay is on.
  std::function<void(const juce::File&)> on_preview;

  void resized() override;
  void paint(juce::Graphics& g) override;

private:
  // FileBrowserListener: preview the selection when autoplay is enabled.
  void selectionChanged() override;
  void fileClicked(const juce::File&, const juce::MouseEvent&) override {}
  void fileDoubleClicked(const juce::File&) override {}
  void browserRootChanged(const juce::File&) override {}

  void SetRoot(const juce::File& root, bool persist);
  void ChooseRoot();

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

#endif  // SPDSX_PATCHEDIT_SOURCE_SAMPLE_BROWSER_H_
