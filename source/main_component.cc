#include "main_component.h"

#include <cstdio>

#include "actions.h"
#include "commands.h"
#include "spectro.h"

namespace spdsx {

namespace {

constexpr int kHeaderHeight = 44;
constexpr int kBrowserWidth = 260;
constexpr int kGridPadding = 14;
constexpr int kGridSpacing = 14;
constexpr int kPadPadding = 8;
constexpr int kPadHeader = 20;
constexpr int kSlotSpacing = 8;

const juce::Colour kWindowBg(0xff12161b);
const juce::Colour kPadBg(0xff161b22);
const juce::Colour kPadBorder(0xff242d38);
const juce::Colour kPadLabel(0xff8a97a6);
const juce::Colour kKitName(0xffe6edf5);
const juce::Colour kDirtyDot(0xffd9a94f);

}  // namespace

juce::ApplicationProperties& MainComponent::ConfigureSettings()
{
  juce::PropertiesFile::Options options;
  options.applicationName = "spdsx-patchedit";
  options.filenameSuffix = ".settings";
  options.osxLibrarySubFolder = "Application Support";
  options.folderName = "spdsx-patchedit";
  options.millisecondsBeforeSaving = 500;
  settings_.setStorageParameters(options);
  return settings_;
}

MainComponent::MainComponent(juce::ApplicationCommandManager& commands)
    : commands_(commands)
    , browser_(ConfigureSettings())
{
  browser_visible_ =
      settings_.getUserSettings()->getBoolValue("browserVisible", true);
  browser_.setVisible(browser_visible_);
  addChildComponent(browser_);

  setWantsKeyboardFocus(true);
  for (int i = 0; i < kSlotCount; ++i) {
    slots_[static_cast<size_t>(i)] = std::make_unique<SampleSlot>(i);
    auto& slot = *slots_[static_cast<size_t>(i)];
    slot.on_drop = [this](int idx, const juce::File& file)
    { LoadSample(idx, file); };
    slot.on_click = [this](int idx) { TriggerSlot(idx); };
    slot.on_transport = [this](int idx, TransportAction action)
    { ApplyTransportAction(idx, action); };
    addAndMakeVisible(slot);
  }
  // The kit name, click-to-edit in place.
  name_label_.setText(model_.name(), juce::dontSendNotification);
  name_label_.setFont(juce::Font(juce::FontOptions(17.0f)).boldened());
  name_label_.setColour(juce::Label::textColourId, kKitName);
  name_label_.setJustificationType(juce::Justification::centred);
  name_label_.setEditable(true, false, false);
  name_label_.onTextChange = [this]
  {
    auto text = name_label_.getText().trim();
    if (text.isEmpty()) {
      name_label_.setText(model_.name(), juce::dontSendNotification);
    } else {
      model_.set_name(text);
    }
  };
  // Hand the keyboard back to the grid when editing ends, so the
  // spacebar keeps working.
  name_label_.onEditorHide = [this] { grabKeyboardFocus(); };
  addAndMakeVisible(name_label_);

  model_.AddListener(this);
  setSize(960, 720);
  // Drives the hover poll, the playhead, and end-of-sample detection.
  startTimerHz(30);
}

MainComponent::~MainComponent()
{
  model_.RemoveListener(this);
}

void MainComponent::LoadSample(int idx, const juce::File& file)
{
  if (idx < 0 || idx >= kSlotCount) {
    std::fprintf(
        stderr, "slot %d out of range (0..%d)\n", idx, kSlotCount - 1);
    return;
  }
  undo_.beginNewTransaction("Load " + file.getFileName());
  undo_.perform(new SetSampleAction(model_, idx / KitModel::kLayersPerPad,
      idx % KitModel::kLayersPerPad, file));
}

juce::ApplicationCommandTarget* MainComponent::getNextCommandTarget()
{
  return nullptr;
}

void MainComponent::getAllCommands(juce::Array<juce::CommandID>& ids)
{
  ids.addArray({commands::kUndo, commands::kRedo, commands::kFileNew,
      commands::kFileOpen, commands::kFileSave, commands::kFileSaveAs,
      commands::kToggleBrowser});
}

void MainComponent::getCommandInfo(
    juce::CommandID id, juce::ApplicationCommandInfo& info)
{
  switch (id) {
    case commands::kUndo:
      info.setInfo("Undo", "Undo the last change", "Edit", 0);
      info.addDefaultKeypress('z', juce::ModifierKeys::commandModifier);
      info.setActive(undo_.canUndo());
      break;
    case commands::kRedo:
      info.setInfo("Redo", "Redo the last undone change", "Edit", 0);
      info.addDefaultKeypress('z',
          juce::ModifierKeys::commandModifier
              | juce::ModifierKeys::shiftModifier);
      info.setActive(undo_.canRedo());
      break;
    case commands::kFileNew:
      info.setInfo("New Kit", "Start a fresh untitled kit", "File", 0);
      info.addDefaultKeypress('n', juce::ModifierKeys::commandModifier);
      break;
    case commands::kFileOpen:
      info.setInfo("Open...", "Open a .kit file", "File", 0);
      info.addDefaultKeypress('o', juce::ModifierKeys::commandModifier);
      break;
    case commands::kFileSave:
      info.setInfo("Save", "Save the kit", "File", 0);
      info.addDefaultKeypress('s', juce::ModifierKeys::commandModifier);
      break;
    case commands::kFileSaveAs:
      info.setInfo("Save As...", "Save the kit to a new file", "File", 0);
      info.addDefaultKeypress('s',
          juce::ModifierKeys::commandModifier
              | juce::ModifierKeys::shiftModifier);
      break;
    case commands::kToggleBrowser:
      info.setInfo("Sample Browser",
          "Show or hide the sample browser panel", "View", 0);
      info.addDefaultKeypress('b', juce::ModifierKeys::commandModifier);
      info.setTicked(browser_visible_);
      break;
    default:
      break;
  }
}

bool MainComponent::perform(const InvocationInfo& info)
{
  switch (info.commandID) {
    case commands::kUndo:
      return undo_.undo();
    case commands::kRedo:
      return undo_.redo();
    case commands::kFileNew:
      document_.saveIfNeededAndUserAgreesAsync(
          [this](juce::FileBasedDocument::SaveResult result)
          {
            if (result == juce::FileBasedDocument::savedOk) {
              document_.ResetToUntitled();
              RefreshDocumentState();
            }
          });
      return true;
    case commands::kFileOpen:
      document_.saveIfNeededAndUserAgreesAsync(
          [this](juce::FileBasedDocument::SaveResult result)
          {
            if (result == juce::FileBasedDocument::savedOk) {
              document_.loadFromUserSpecifiedFileAsync(
                  true, [this](juce::Result) { RefreshDocumentState(); });
            }
          });
      return true;
    case commands::kFileSave:
      document_.saveAsync(true, true,
          [this](juce::FileBasedDocument::SaveResult)
          { RefreshDocumentState(); });
      return true;
    case commands::kFileSaveAs:
      document_.saveAsInteractiveAsync(true,
          [this](juce::FileBasedDocument::SaveResult)
          { RefreshDocumentState(); });
      return true;
    case commands::kToggleBrowser:
      SetBrowserVisible(!browser_visible_);
      return true;
    default:
      return false;
  }
}

void MainComponent::SetBrowserVisible(bool visible)
{
  browser_visible_ = visible;
  browser_.setVisible(visible);
  settings_.getUserSettings()->setValue("browserVisible", visible);
  commands_.commandStatusChanged();  // menu tick
  resized();
  repaint();
}

// Window title carries the kit name and an Edited marker; the header
// dot repaints with it.
void MainComponent::RefreshDocumentState()
{
  shown_dirty_ = document_.hasChangedSinceSaved();
  if (auto* window =
          dynamic_cast<juce::DocumentWindow*>(getTopLevelComponent()))
  {
    window->setName(
        model_.name() + (shown_dirty_ ? " \xe2\x80\x94 Edited" : ""));
  }
  repaint(0, 0, getWidth(), kHeaderHeight);
}

void MainComponent::KitNameChanged()
{
  name_label_.setText(model_.name(), juce::dontSendNotification);
  document_.changed();
  RefreshDocumentState();
}

// The model is the source of truth: engine and slot display sync to it
// here, whether the change came from a user gesture, undo, or a loaded
// kit file. The pad-shaped model maps to the flat slot components as
// idx = pad * 2 + layer.
void MainComponent::SampleChanged(int pad, int layer)
{
  document_.changed();
  const int idx = pad * KitModel::kLayersPerPad + layer;
  const auto& file = model_.sample(pad, layer);
  auto& slot = *slots_[static_cast<size_t>(idx)];
  if (file == juce::File()) {
    engine_.Clear(idx);
    slot.ClearSample();
    return;
  }
  auto info = engine_.Load(idx, file);
  if (!info) {
    // Unreadable (moved, unmounted, not audio): keep the assignment
    // visible so it survives a save/load round trip.
    engine_.Clear(idx);
    slot.SetSampleMissing(file.getFileName());
    return;
  }
  // Too-short files play fine but render no spectrogram; the slot just
  // shows the info bar in that case.
  juce::Image image;
  if (auto png = render_spectrogram(
          file.getFullPathName().toStdString(), idx);
      !png.empty())
  {
    image = juce::ImageFileFormat::loadFrom(juce::File(png));
  }
  slot.SetSample(file.getFileName(), info->duration_seconds,
      info->sample_rate, image);
}

void MainComponent::paint(juce::Graphics& g)
{
  g.fillAll(kWindowBg);

  // Dirty indicator: a dot in the header (the title bar also carries an
  // "Edited" marker).
  if (document_.hasChangedSinceSaved()) {
    g.setColour(kDirtyDot);
    g.fillEllipse(static_cast<float>(getWidth()) - 26.0f,
        kHeaderHeight / 2.0f - 4.0f, 8.0f, 8.0f);
  }

  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) {
      auto pad = PadBounds(r, c);
      g.setColour(kPadBg);
      g.fillRoundedRectangle(pad.toFloat(), 10.0f);
      g.setColour(kPadBorder);
      g.drawRoundedRectangle(pad.toFloat().reduced(0.5f), 10.0f, 1.0f);
      g.setColour(kPadLabel);
      g.setFont(juce::Font(juce::FontOptions(13.0f)).boldened());
      g.drawText(juce::String(r * 3 + c + 1),
          pad.reduced(kPadPadding + 2, kPadPadding)
              .removeFromTop(kPadHeader),
          juce::Justification::centredLeft);
    }
  }
}

juce::Rectangle<int> MainComponent::GridArea() const
{
  auto area = getLocalBounds();
  area.removeFromTop(kHeaderHeight);
  if (browser_visible_) {
    area.removeFromLeft(kBrowserWidth);
  }
  return area;
}

juce::Rectangle<int> MainComponent::PadBounds(int row, int col) const
{
  const auto area = GridArea();
  const int cell_w =
      (area.getWidth() - 2 * kGridPadding - 2 * kGridSpacing) / 3;
  const int cell_h =
      (area.getHeight() - 2 * kGridPadding - 2 * kGridSpacing) / 3;
  return {area.getX() + kGridPadding + col * (cell_w + kGridSpacing),
      area.getY() + kGridPadding + row * (cell_h + kGridSpacing), cell_w,
      cell_h};
}

void MainComponent::resized()
{
  name_label_.setBounds(getLocalBounds()
          .removeFromTop(kHeaderHeight)
          .withSizeKeepingCentre(
              juce::jmin(420, getWidth() - 120), 26));
  browser_.setBounds(
      0, kHeaderHeight, kBrowserWidth, getHeight() - kHeaderHeight);
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) {
      auto inner = PadBounds(r, c).reduced(kPadPadding);
      inner.removeFromTop(kPadHeader);
      const int slot_h = (inner.getHeight() - kSlotSpacing) / 2;
      const auto pad = static_cast<size_t>((r * 3 + c) * 2);
      slots_[pad]->setBounds(inner.removeFromTop(slot_h));
      inner.removeFromTop(kSlotSpacing);
      slots_[pad + 1]->setBounds(inner.removeFromTop(slot_h));
    }
  }
}

bool MainComponent::keyPressed(const juce::KeyPress& key)
{
  if (key == juce::KeyPress::spaceKey) {
    if (hovered_ >= 0) {
      TriggerSlot(hovered_);
    }
    return true;
  }
  return false;
}

void MainComponent::ApplyTransportAction(int idx, TransportAction action)
{
  auto& slot = *slots_[static_cast<size_t>(idx)];
  if (!slot.is_playable()) {
    return;
  }
  switch (action) {
    case TransportAction::kPlay:
      // Play during playback retriggers from the top (drum-pad style);
      // from paused it resumes, from stopped it starts at the top.
      if (slot.play_state() == PlayState::kPlaying) {
        engine_.Stop(idx);
      }
      engine_.Play(idx);
      slot.set_play_state(PlayState::kPlaying);
      break;
    case TransportAction::kPause:
      if (slot.play_state() == PlayState::kPlaying) {
        engine_.Pause(idx);
        slot.set_play_state(PlayState::kPaused);
      }
      break;
    case TransportAction::kStop:
      engine_.Stop(idx);
      slot.set_play_state(PlayState::kStopped);
      break;
  }
}

void MainComponent::TriggerSlot(int idx)
{
  auto& slot = *slots_[static_cast<size_t>(idx)];
  if (slot.is_playable()) {
    ApplyTransportAction(idx, TransportAction::kPlay);
    slot.FlashTransportButton(TransportAction::kPlay);
  }
}

void MainComponent::timerCallback()
{
  // Keep menu enablement in step with the undo history.
  if (undo_.canUndo() != could_undo_ || undo_.canRedo() != could_redo_) {
    could_undo_ = undo_.canUndo();
    could_redo_ = undo_.canRedo();
    commands_.commandStatusChanged();
  }

  // Dirty state changes on async save completions too; poll it.
  if (document_.hasChangedSinceSaved() != shown_dirty_) {
    RefreshDocumentState();
  }

  // Focus follows the mouse. Polled rather than event-driven: the
  // transport buttons are child components, and enter/exit pairs across
  // parent/child boundaries are easy to get wrong.
  int hovered = -1;
  for (int i = 0; i < kSlotCount; ++i) {
    if (slots_[static_cast<size_t>(i)]->isMouseOver(true)) {
      hovered = i;
      break;
    }
  }
  hovered_ = hovered;
  for (int i = 0; i < kSlotCount; ++i) {
    slots_[static_cast<size_t>(i)]->set_hovered(i == hovered);
  }

  // Advance playheads; sounds end on the audio thread, so a slot that
  // thinks it's playing while its transport has stopped just ran out.
  for (int i = 0; i < kSlotCount; ++i) {
    auto& slot = *slots_[static_cast<size_t>(i)];
    if (slot.play_state() == PlayState::kPlaying) {
      if (engine_.IsPlaying(i)) {
        slot.set_position(engine_.PositionFraction(i));
      } else {
        engine_.Stop(i);
        slot.set_play_state(PlayState::kStopped);
      }
    }
  }
}

}  // namespace spdsx
