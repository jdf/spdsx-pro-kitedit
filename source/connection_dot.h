// A small status light: green when the device is connected, gray when
// not. Fed by the main window's connection poller — the one place that
// touches the port for status, so windows never race each other over it.
#ifndef SPDSX_PATCHEDIT_SOURCE_CONNECTION_DOT_H_
#define SPDSX_PATCHEDIT_SOURCE_CONNECTION_DOT_H_

#include <juce_gui_basics/juce_gui_basics.h>

namespace spdsx {

class ConnectionDot
    : public juce::Component
    , public juce::SettableTooltipClient {
public:
  ConnectionDot() { setTooltip("No device connected"); }

  void SetConnected(bool connected) {
    if (connected != connected_) {
      connected_ = connected;
      setTooltip(connected ? "SPD-SX PRO connected" : "No device connected");
      repaint();
    }
  }

  void paint(juce::Graphics& g) override {
    const auto dot = getLocalBounds().toFloat().withSizeKeepingCentre(10, 10);
    g.setColour(connected_ ? juce::Colour(0xff35c65a)
                           : juce::Colour(0xff6b6b6b));
    g.fillEllipse(dot);
    g.setColour(juce::Colours::black.withAlpha(0.25f));
    g.drawEllipse(dot, 1.0f);
  }

private:
  bool connected_ = false;
};

}  // namespace spdsx

#endif  // SPDSX_PATCHEDIT_SOURCE_CONNECTION_DOT_H_
