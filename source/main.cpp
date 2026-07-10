#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

namespace spdsx {

// Placeholder until the pad grid lands.
class PlaceholderComponent : public juce::Component {
public:
  PlaceholderComponent() { setSize(960, 720); }

  void paint(juce::Graphics& g) override
  {
    g.fillAll(juce::Colour(0xff12161b));
    g.setColour(juce::Colour(0xff4c5866));
    g.setFont(24.0f);
    g.drawText(
        "spdsx-patchedit", getLocalBounds(), juce::Justification::centred);
  }
};

class MainWindow : public juce::DocumentWindow {
public:
  MainWindow()
      : juce::DocumentWindow("SPD-SX Patch Edit",
            juce::Colour(0xff12161b),
            juce::DocumentWindow::allButtons)
  {
    setUsingNativeTitleBar(true);
    setContentOwned(new PlaceholderComponent(), true);
    setResizable(true, true);
    centreWithSize(getWidth(), getHeight());
    setVisible(true);
  }

  void closeButtonPressed() override
  {
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
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
