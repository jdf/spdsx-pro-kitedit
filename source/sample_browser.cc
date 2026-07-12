#include "sample_browser.h"

#include "audio_files.h"

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
  tree_->addListener(this);
  addAndMakeVisible(*tree_);

  root_label_.setFont(juce::Font(juce::FontOptions(12.0f)));
  root_label_.setColour(juce::Label::textColourId, kRootText);
  root_label_.setMinimumHorizontalScale(0.7f);
  addAndMakeVisible(root_label_);

  root_button_.setTooltip("Choose the sample folder");
  root_button_.onClick = [this] { ChooseRoot(); };
  addAndMakeVisible(root_button_);

  scan_thread_.startThread();
  auto saved = juce::File(
      settings_.getUserSettings()->getValue("sampleBrowserRoot"));
  SetRoot(saved.isDirectory()
          ? saved
          : juce::File::getSpecialLocation(juce::File::userMusicDirectory),
      false);
  pending_tree_state_ =
      settings_.getUserSettings()->getXmlValue("sampleBrowserTree");
  if (pending_tree_state_ != nullptr) {
    startTimer(100);
  }
}

SampleBrowser::~SampleBrowser()
{
  SaveTreeState();
  tree_->removeListener(this);
  // The scan thread walks contents_; stop it before members go away.
  scan_thread_.stopThread(2000);
}

void SampleBrowser::SaveTreeState()
{
  if (auto xml = tree_->getOpennessState(true)) {
    settings_.getUserSettings()->setValue("sampleBrowserTree", xml.get());
  }
}

void SampleBrowser::timerCallback()
{
  if (pending_tree_state_ == nullptr || ++restore_attempts_ > 50) {
    stopTimer();
    pending_tree_state_.reset();
    return;
  }
  // Restoring selection fires selectionChanged; don't let a relaunch
  // autoplay whatever was selected last time.
  restoring_ = true;
  tree_->restoreOpennessState(*pending_tree_state_, true);
  restoring_ = false;
  // Done once the tree reproduces the saved state (folders that no
  // longer exist can never match; the attempt cap covers those).
  if (auto now = tree_->getOpennessState(true);
      now != nullptr && now->isEquivalentTo(pending_tree_state_.get(), true))
  {
    stopTimer();
    pending_tree_state_.reset();
  }
}

void SampleBrowser::selectionChanged()
{
  if (restoring_
      || !settings_.getUserSettings()->getBoolValue(
          "autoplayBrowsing", false))
  {
    return;
  }
  const juce::File file = tree_->getSelectedFile();
  if (file.existsAsFile() && LooksLikeAudio(file.getFullPathName())
      && on_preview) {
    on_preview(file);
  }
}

void SampleBrowser::SetRoot(const juce::File& root, bool persist)
{
  contents_->setDirectory(root, true, true);
  root_label_.setText(root.getFileName(), juce::dontSendNotification);
  root_label_.setTooltip(root.getFullPathName());
  if (persist) {
    settings_.getUserSettings()->setValue(
        "sampleBrowserRoot", root.getFullPathName());
    // A saved tree state describes the old root; stop chasing it.
    stopTimer();
    pending_tree_state_.reset();
  }
}

void SampleBrowser::ChooseRoot()
{
  chooser_ = std::make_unique<juce::FileChooser>(
      "Choose the sample folder", contents_->getDirectory());
  chooser_->launchAsync(juce::FileBrowserComponent::openMode
          | juce::FileBrowserComponent::canSelectDirectories,
      [this](const juce::FileChooser& fc)
      {
        if (auto dir = fc.getResult(); dir.isDirectory()) {
          SetRoot(dir, true);
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
