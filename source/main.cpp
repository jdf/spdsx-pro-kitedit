#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "main_component.hpp"

namespace spdsx {

class MainWindow : public juce::DocumentWindow {
public:
  MainWindow()
      : juce::DocumentWindow("SPD-SX Patch Edit",
            juce::Colour(0xff12161b),
            juce::DocumentWindow::allButtons)
  {
    setUsingNativeTitleBar(true);
    setContentOwned(new MainComponent(), true);
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
    window = std::make_unique<MainWindow>();
  }
  void shutdown() override { window.reset(); }

private:
  std::unique_ptr<MainWindow> window;
};

}  // namespace spdsx

START_JUCE_APPLICATION(spdsx::App)
