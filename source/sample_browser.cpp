#include "sample_browser.hpp"

#include "audio_files.hpp"

namespace spdsx {

namespace {

const juce::Colour kPanelBg(0xff161b22);
const juce::Colour kPanelBorder(0xff242d38);
const juce::Colour kRootText(0xff9aa7b4);
const juce::Colour kTreeText(0xffcfd8e3);

constexpr int kHeaderHeight = 28;

}  // namespace

SampleBrowser::SampleBrowser(juce::ApplicationProperties& settings)
    : settings_(settings)
{
  filter_ = std::make_unique<juce::WildcardFileFilter>(
      kAudioFileWildcard, "*", "Audio files");
  contents_ =
      std::make_unique<juce::DirectoryContentsList>(filter_.get(), scan_thread_);
  tree_ = std::make_unique<juce::FileTreeComponent>(*contents_);
  tree_->setDragAndDropDescription(kSampleDragId);
  tree_->setColour(juce::TreeView::backgroundColourId, kPanelBg);
  tree_->setColour(
      juce::DirectoryContentsDisplayComponent::textColourId, kTreeText);
  tree_->setColour(
      juce::DirectoryContentsDisplayComponent::highlightColourId,
      juce::Colour(0xff31517a));
  addAndMakeVisible(*tree_);

  root_label_.setFont(juce::Font(juce::FontOptions(12.0f)));
  root_label_.setColour(juce::Label::textColourId, kRootText);
  root_label_.setMinimumHorizontalScale(0.7f);
  addAndMakeVisible(root_label_);

  root_button_.setTooltip("Choose the sample folder");
  root_button_.onClick = [this] { choose_root(); };
  addAndMakeVisible(root_button_);

  scan_thread_.startThread();
  auto saved = juce::File(
      settings_.getUserSettings()->getValue("sampleBrowserRoot"));
  set_root(saved.isDirectory()
          ? saved
          : juce::File::getSpecialLocation(juce::File::userMusicDirectory),
      false);
}

SampleBrowser::~SampleBrowser()
{
  // The scan thread walks contents_; stop it before members go away.
  scan_thread_.stopThread(2000);
}

void SampleBrowser::set_root(const juce::File& root, bool persist)
{
  contents_->setDirectory(root, true, true);
  root_label_.setText(root.getFileName(), juce::dontSendNotification);
  root_label_.setTooltip(root.getFullPathName());
  if (persist) {
    settings_.getUserSettings()->setValue(
        "sampleBrowserRoot", root.getFullPathName());
  }
}

void SampleBrowser::choose_root()
{
  chooser_ = std::make_unique<juce::FileChooser>(
      "Choose the sample folder", contents_->getDirectory());
  chooser_->launchAsync(juce::FileBrowserComponent::openMode
          | juce::FileBrowserComponent::canSelectDirectories,
      [this](const juce::FileChooser& fc)
      {
        if (auto dir = fc.getResult(); dir.isDirectory()) {
          set_root(dir, true);
        }
      });
}

void SampleBrowser::paint(juce::Graphics& g)
{
  g.fillAll(kPanelBg);
  g.setColour(kPanelBorder);
  g.fillRect(getWidth() - 1, 0, 1, getHeight());
  g.fillRect(0, kHeaderHeight - 1, getWidth(), 1);
}

void SampleBrowser::resized()
{
  auto header = getLocalBounds().removeFromTop(kHeaderHeight);
  header.removeFromRight(1);  // panel border
  root_button_.setBounds(
      header.removeFromRight(kHeaderHeight).reduced(4));
  root_label_.setBounds(header.reduced(8, 0));
  auto tree_area = getLocalBounds();
  tree_area.removeFromTop(kHeaderHeight);
  tree_area.removeFromRight(1);
  tree_->setBounds(tree_area);
}

}  // namespace spdsx
