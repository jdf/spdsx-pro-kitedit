#include "about_dialog.h"

#include <memory>

#include "BinaryData.h"

namespace spdsx {
namespace {

struct Credit {
  const char* name;
  const char* license;
  const char* url;
};

// Everything statically linked into the shipping binary. gtest is
// test-only and never ships, so it is not listed.
constexpr Credit kCredits[] = {
    {"JUCE", "AGPLv3", "https://juce.com"},
    {"specgram", "MIT", "https://github.com/jdf/specgram"},
    {"SQLite", "Public Domain", "https://sqlite.org"},
    {"Abseil", "Apache 2.0", "https://abseil.io"},
    {"KFR", "GPLv2", "https://kfr.dev"},
    {"libsndfile", "LGPL 2.1", "https://libsndfile.github.io/libsndfile/"},
    {"cairo", "LGPL 2.1 / MPL 1.1", "https://cairographics.org"},
};

constexpr int kWidth = 380;
constexpr int kIconSize = 96;
constexpr int kRowHeight = 22;
constexpr int kMargin = 20;

}  // namespace

AboutPanel::AboutPanel(const juce::String& version) {
  icon_ = juce::ImageFileFormat::loadFrom(BinaryData::icon_png,
                                          BinaryData::icon_pngSize);

  name_.setText("SPD-SX PROgram", juce::dontSendNotification);
  name_.setFont(juce::FontOptions(22.0f, juce::Font::bold));
  name_.setJustificationType(juce::Justification::centred);
  addAndMakeVisible(name_);

  version_.setText("Version " + version, juce::dontSendNotification);
  version_.setJustificationType(juce::Justification::centred);
  version_.setColour(juce::Label::textColourId,
                     findColour(juce::Label::textColourId).withAlpha(0.7f));
  addAndMakeVisible(version_);

  copyright_.setText(juce::String::fromUTF8("© 2026 Jonathan Feinberg — MIT"),
                     juce::dontSendNotification);
  copyright_.setJustificationType(juce::Justification::centred);
  addAndMakeVisible(copyright_);

  built_with_.setText("Built with:", juce::dontSendNotification);
  built_with_.setFont(juce::FontOptions(15.0f, juce::Font::bold));
  addAndMakeVisible(built_with_);

  for (const auto& credit : kCredits) {
    auto link = std::make_unique<juce::HyperlinkButton>(
        juce::String(credit.name), juce::URL(credit.url));
    link->setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(*link);
    links_.push_back(std::move(link));

    auto license =
        std::make_unique<juce::Label>(juce::String(), credit.license);
    license->setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(*license);
    licenses_.push_back(std::move(license));
  }

  const int rows = static_cast<int>(std::size(kCredits));
  setSize(kWidth,
          kMargin + kIconSize + 8 + 28 + 20 + 20 + 16 + 26 + rows * kRowHeight
              + kMargin);
}

void AboutPanel::paint(juce::Graphics& g) {
  g.fillAll(findColour(juce::ResizableWindow::backgroundColourId));
  if (icon_.isValid()) {
    const int x = (getWidth() - kIconSize) / 2;
    g.drawImage(
        icon_,
        juce::Rectangle<int>(x, kMargin, kIconSize, kIconSize).toFloat());
  }
}

void AboutPanel::resized() {
  auto area = getLocalBounds().reduced(kMargin, 0);
  area.removeFromTop(kMargin + kIconSize + 8);
  name_.setBounds(area.removeFromTop(28));
  version_.setBounds(area.removeFromTop(20));
  copyright_.setBounds(area.removeFromTop(20));
  area.removeFromTop(16);
  built_with_.setBounds(area.removeFromTop(26));
  for (size_t i = 0; i < links_.size(); ++i) {
    auto row = area.removeFromTop(kRowHeight);
    links_[i]->setBounds(row.removeFromLeft(row.getWidth() / 2));
    licenses_[i]->setBounds(row);
  }
}

void AboutPanel::Show(const juce::String& version) {
  static juce::Component::SafePointer<juce::DialogWindow> open;
  if (open != nullptr) {
    open->toFront(true);
    return;
  }
  juce::DialogWindow::LaunchOptions options;
  options.content.setOwned(new AboutPanel(version));
  options.dialogTitle = "About SPD-SX PROgram";
  options.escapeKeyTriggersCloseButton = true;
  options.useNativeTitleBar = true;
  options.resizable = false;
  options.dialogBackgroundColour =
      options.content->findColour(juce::ResizableWindow::backgroundColourId);
  open = options.launchAsync();
}

}  // namespace spdsx
