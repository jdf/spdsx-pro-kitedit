// The application menu's About box: icon, name, version, and the
// shout-outs and licenses for everything statically linked into the
// binary. Pure view.
#ifndef SPDSX_PATCHEDIT_SOURCE_ABOUT_DIALOG_H_
#define SPDSX_PATCHEDIT_SOURCE_ABOUT_DIALOG_H_

#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

namespace spdsx {

class AboutPanel : public juce::Component {
public:
  explicit AboutPanel(const juce::String& version);

  void paint(juce::Graphics& g) override;
  void resized() override;

  // Opens the About window (non-modal, native title bar, escape closes).
  // One at a time: invoking again fronts the open one.
  static void Show(const juce::String& version);

private:
  juce::Image icon_;
  juce::Label name_;
  juce::Label version_;
  juce::Label copyright_;
  juce::Label built_with_;
  // Parallel per-dependency rows: a link to the project, and its license.
  std::vector<std::unique_ptr<juce::HyperlinkButton>> links_;
  std::vector<std::unique_ptr<juce::Label>> licenses_;
};

}  // namespace spdsx

#endif  // SPDSX_PATCHEDIT_SOURCE_ABOUT_DIALOG_H_
