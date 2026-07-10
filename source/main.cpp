#include <cstdio>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "main_component.hpp"

namespace spdsx {

class MainWindow : public juce::DocumentWindow {
public:
  // Takes ownership of content.
  explicit MainWindow(MainComponent* content)
      : juce::DocumentWindow("SPD-SX Patch Edit",
            juce::Colour(0xff12161b),
            juce::DocumentWindow::allButtons)
  {
    setUsingNativeTitleBar(true);
    setContentOwned(content, true);
    setResizable(true, true);
    centreWithSize(getWidth(), getHeight());
    setVisible(true);
  }

  void closeButtonPressed() override
  {
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
  }

  // The grid handles the spacebar; hand it the keyboard whenever the
  // window becomes active.
  void activeWindowStatusChanged() override
  {
    if (isActiveWindow() && getContentComponent() != nullptr) {
      getContentComponent()->grabKeyboardFocus();
    }
  }
};

class App : public juce::JUCEApplication {
public:
  const juce::String getApplicationName() override
  {
    return "spdsx-patchedit";
  }
  const juce::String getApplicationVersion() override { return "0.1.0"; }

  void initialise(const juce::String&) override
  {
    auto* content = new MainComponent();
    // --load <slot> <wav> pre-fills a slot; useful for testing without
    // drag and drop. May be repeated. Slot indices are
    // (row * 3 + col) * 2, +1 for the bottom slot.
    auto args = getCommandLineParameterArray();
    for (int i = 0; i < args.size(); ++i) {
      if (args[i] == "--load" && i + 2 < args.size()) {
        content->load_sample(args[i + 1].getIntValue(),
            juce::File::getCurrentWorkingDirectory().getChildFile(
                args[i + 2]));
        i += 2;
      } else {
        std::fprintf(stderr, "unrecognized argument '%s'\n"
                             "usage: spdsx-patchedit [--load <slot> <file.wav>]...\n",
            args[i].toRawUTF8());
      }
    }
    window = std::make_unique<MainWindow>(content);
  }
  void shutdown() override { window.reset(); }

private:
  std::unique_ptr<MainWindow> window;
};

}  // namespace spdsx

START_JUCE_APPLICATION(spdsx::App)
