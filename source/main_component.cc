#include "main_component.h"

#include <cmath>
#include <cstdio>

#include "actions.h"
#include "commands.h"
#include "spectro.h"

namespace spdsx {

namespace {

// The SPD-SX PRO sends pad hits as note-on on channel 10, pads 1-9 on
// notes 60-68.
constexpr int kMidiChannel = 10;
constexpr int kMidiNoteBase = 60;

constexpr int kHeaderHeight = 44;
constexpr int kBrowserWidth = 260;
constexpr int kGridPadding = 14;
constexpr int kGridSpacing = 14;
constexpr int kPadPadding = 8;
constexpr int kPadHeader = 20;
constexpr int kSlotSpacing = 8;
// How long a hit's velocity-coloured pad flash takes to fade.
constexpr juce::uint32 kPadFlashMs = 400;

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
    slot.on_click = [this](int idx)
    {
      // A click anywhere in a pad is a hit on the whole pad, at the
      // cursor-height velocity.
      const int pad = idx / KitModel::kLayersPerPad;
      const auto pos = getMouseXYRelative();
      TriggerPad(pad, VelocityForPointInPad(pad, pos), HiHatPedalDown());
    };
    slot.on_transport = [this](int idx, TransportAction action)
    { ApplyTransportAction(idx, action); };
    slot.on_slot_move = [this](int from, int to, bool copy, bool whole_pad)
    {
      if (whole_pad) {
        MovePad(from / KitModel::kLayersPerPad, to / KitModel::kLayersPerPad,
            copy);
      } else {
        MoveSample(from, to, copy);
      }
    };
    slot.on_drag_target = [this](int idx, bool whole_pad)
    { SetDragTarget(idx, whole_pad); };
    addAndMakeVisible(slot);
  }
  for (int pad = 0; pad < KitModel::kPadCount; ++pad) {
    const auto p = static_cast<size_t>(pad);
    mode_boxes_[p] = std::make_unique<juce::ComboBox>();
    for (int m = 0; m < kLayerModeCount; ++m) {
      const auto name = LayerModeName(static_cast<LayerMode>(m));
      // Item ids are mode + 1; 0 means "nothing selected" to ComboBox.
      mode_boxes_[p]->addItem(
          juce::String(name.data(), name.size()), m + 1);
    }
    mode_boxes_[p]->onChange = [this, pad] { ApplyLayerParams(pad); };
    addAndMakeVisible(*mode_boxes_[p]);
    auto make_fade_slider = [this, pad]
    {
      auto slider = std::make_unique<juce::Slider>(
          juce::Slider::LinearBar, juce::Slider::NoTextBox);
      slider->setRange(1, 127, 1);
      // One undo step per adjustment, not one per drag pixel.
      slider->setChangeNotificationOnlyOnRelease(true);
      slider->onValueChange = [this, pad] { ApplyLayerParams(pad); };
      addAndMakeVisible(*slider);
      return slider;
    };
    fade_point_sliders_[p] = make_fade_slider();
    fade_end_sliders_[p] = make_fade_slider();
    pad_menu_buttons_[p] =
        std::make_unique<juce::TextButton>(juce::String::fromUTF8("⋯"));
    pad_menu_buttons_[p]->onClick = [this, pad] { ShowPadMenu(pad); };
    addAndMakeVisible(*pad_menu_buttons_[p]);
    UpdatePadWidgets(pad);
  }
  // Keyboard pad hits (keys 1-9) carry this velocity; MIDI hits carry
  // their own. Low values audition the soft side of the fade modes.
  velocity_slider_.setSliderStyle(juce::Slider::LinearBar);
  velocity_slider_.setRange(1, 127, 1);
  velocity_slider_.setValue(
      settings_.getUserSettings()->getIntValue("uiVelocity", 100),
      juce::dontSendNotification);
  velocity_slider_.onValueChange = [this]
  {
    settings_.getUserSettings()->setValue(
        "uiVelocity", static_cast<int>(velocity_slider_.getValue()));
  };
  addAndMakeVisible(velocity_slider_);
  velocity_caption_.setText("VEL", juce::dontSendNotification);
  velocity_caption_.setFont(juce::Font(juce::FontOptions(12.0f)).boldened());
  velocity_caption_.setColour(juce::Label::textColourId, kPadLabel);
  velocity_caption_.setJustificationType(juce::Justification::centredRight);
  addAndMakeVisible(velocity_caption_);
  browser_.on_preview = [this](const juce::File& file)
  { engine_.PreviewFile(file); };
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
  OpenMidiInputs();
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
      commands::kToggleBrowser, commands::kToggleAutoplay});
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
    case commands::kToggleAutoplay:
      info.setInfo("Auto-play While Browsing",
          "Audition samples as you select them in the browser", "View", 0);
      info.setTicked(settings_.getUserSettings()->getBoolValue(
          "autoplayBrowsing", false));
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
    case commands::kToggleAutoplay: {
      auto* s = settings_.getUserSettings();
      const bool on = !s->getBoolValue("autoplayBrowsing", false);
      s->setValue("autoplayBrowsing", on);
      if (!on) {
        engine_.StopPreview();
      }
      commands_.commandStatusChanged();  // refresh the menu tick
      return true;
    }
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

  const auto now = juce::Time::getMillisecondCounter();
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) {
      auto pad = PadBounds(r, c);
      g.setColour(kPadBg);
      g.fillRoundedRectangle(pad.toFloat(), 10.0f);
      // A hit washes the pad in the velocity colour and fades out.
      const auto idx = static_cast<size_t>(r * 3 + c);
      const int flash_velocity = pad_flash_velocity_[idx];
      if (flash_velocity > 0) {
        const float age = static_cast<float>(now - pad_flash_ms_[idx])
            / static_cast<float>(kPadFlashMs);
        if (age < 1.0f) {
          const auto colour = VelocityColour(flash_velocity);
          g.setColour(colour.withAlpha(0.30f * (1.0f - age)));
          g.fillRoundedRectangle(pad.toFloat(), 10.0f);
          g.setColour(colour.withAlpha(0.9f * (1.0f - age)));
          g.drawRoundedRectangle(pad.toFloat().reduced(1.0f), 10.0f, 2.0f);
        }
      }
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
  // Velocity control at the header's right edge, clear of the dirty dot.
  auto vel = juce::Rectangle<int>(getWidth() - 178, 0, 140, kHeaderHeight)
                 .withSizeKeepingCentre(140, 20);
  velocity_caption_.setBounds(vel.removeFromLeft(34));
  velocity_slider_.setBounds(vel.reduced(2, 0));
  browser_.setBounds(
      0, kHeaderHeight, kBrowserWidth, getHeight() - kHeaderHeight);
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) {
      auto inner = PadBounds(r, c).reduced(kPadPadding);
      const int pad = r * 3 + c;
      // Layer controls share the header row with the painted pad number:
      // fade point, fade end, mode box, right-aligned in that order.
      auto header = inner.removeFromTop(kPadHeader).withTrimmedBottom(2);
      const auto p = static_cast<size_t>(pad);
      pad_menu_buttons_[p]->setBounds(header.removeFromRight(18));
      header.removeFromRight(4);
      mode_boxes_[p]->setBounds(header.removeFromRight(88));
      header.removeFromRight(4);
      fade_end_sliders_[p]->setBounds(header.removeFromRight(30));
      header.removeFromRight(4);
      fade_point_sliders_[p]->setBounds(header.removeFromRight(30));
      const int slot_h = (inner.getHeight() - kSlotSpacing) / 2;
      const auto slot = static_cast<size_t>(pad * 2);
      slots_[slot]->setBounds(inner.removeFromTop(slot_h));
      inner.removeFromTop(kSlotSpacing);
      slots_[slot + 1]->setBounds(inner.removeFromTop(slot_h));
    }
  }
}

bool MainComponent::keyPressed(const juce::KeyPress& key)
{
  if (key == juce::KeyPress::spaceKey) {
    // OS auto-repeat re-sends keyPressed while held; only the first
    // press of a trigger key plays (a drummer holding a stick down
    // doesn't roll). Same for pads below.
    if (!held_space_) {
      held_space_ = true;
      const auto pos = getMouseXYRelative();
      if (const int pad = PadAt(pos); pad >= 0) {
        TriggerPad(pad, VelocityForPointInPad(pad, pos), HiHatPedalDown());
      }
    }
    return true;
  }
  // Keys 1-9 hit the matching pad at the header velocity; the pedal is
  // down while H (or shift) is held, or the MIDI pedal is pressed.
  // Shifted digits can arrive as their punctuation characters, so match
  // those too.
  const int code = key.getKeyCode();
  int pad = -1;
  bool pedal_down = key.getModifiers().isShiftDown() || HiHatPedalDown();
  if (code >= '1' && code <= '9') {
    pad = code - '1';
  } else if (const auto pos = juce::String("!@#$%^&*(").indexOfChar(
                 key.getTextCharacter());
             pos >= 0)
  {
    pad = pos;
    pedal_down = true;
  }
  if (pad >= 0) {
    auto& held = held_pad_keys_[static_cast<size_t>(pad)];
    if (!held) {
      held = true;
      TriggerPad(
          pad, static_cast<int>(velocity_slider_.getValue()), pedal_down);
    }
    return true;
  }
  // The pedal itself is handled edge-triggered in keyStateChanged; consume
  // the press here anyway or macOS beeps about an unhandled key.
  if (code == 'H' || code == 'h') {
    return true;
  }
  return false;
}

// keyPressed only reports presses; releases arrive here, so anything
// that cares about key-up (the H pedal, the auto-repeat guards) polls
// the actual key state.
bool MainComponent::keyStateChanged(bool /*is_key_down*/)
{
  SetHiHatKeyDown(juce::KeyPress::isKeyCurrentlyDown('H')
      || juce::KeyPress::isKeyCurrentlyDown('h'));
  if (held_space_
      && !juce::KeyPress::isKeyCurrentlyDown(juce::KeyPress::spaceKey))
  {
    held_space_ = false;
  }
  for (int pad = 0; pad < KitModel::kPadCount; ++pad) {
    // A held digit can morph between '1' and '!' if shift changes
    // mid-hold; the key only counts as released when both are up.
    if (held_pad_keys_[static_cast<size_t>(pad)]
        && !juce::KeyPress::isKeyCurrentlyDown('1' + pad)
        && !juce::KeyPress::isKeyCurrentlyDown("!@#$%^&*("[pad]))
    {
      held_pad_keys_[static_cast<size_t>(pad)] = false;
    }
  }
  return false;
}

void MainComponent::SetHiHatKeyDown(bool down)
{
  if (down == hihat_key_down_) {
    return;  // auto-repeat, or a different key changed state
  }
  hihat_key_down_ = down;
  if (!down) {
    return;  // releasing the pedal makes no sound of its own
  }
  // Foot-close: the closing pedal cuts the open layer and sounds the
  // closed one.
  const int velocity = static_cast<int>(velocity_slider_.getValue());
  for (int pad = 0; pad < KitModel::kPadCount; ++pad) {
    if (model_.params(pad).mode != LayerMode::kHiHat) {
      continue;
    }
    const int open_idx = pad * KitModel::kLayersPerPad + 1;
    engine_.Stop(open_idx);
    slots_[static_cast<size_t>(open_idx)]->set_play_state(
        PlayState::kStopped);
    TriggerPad(pad, velocity, /*pedal_down=*/true);
  }
}

bool MainComponent::HiHatPedalDown() const
{
  return hihat_key_down_ || hihat_cc_.load() >= 64;
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
      // Slot-level auditioning is always full volume, undoing any gain a
      // pad-level layer trigger left behind.
      engine_.SetGain(idx, 1.0f);
      slot.set_velocity_highlight(127);
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

// Clicks on the pad surface itself (the header strip, the gaps between
// slots) land here; clicks on a slot body arrive via on_click. Both are
// whole-pad hits.
void MainComponent::mouseDown(const juce::MouseEvent& event)
{
  const auto pos = event.getPosition();
  if (const int pad = PadAt(pos); pad >= 0) {
    TriggerPad(pad, VelocityForPointInPad(pad, pos), HiHatPedalDown());
  }
}

int MainComponent::PadAt(juce::Point<int> point) const
{
  for (int pad = 0; pad < KitModel::kPadCount; ++pad) {
    if (PadBounds(pad / 3, pad % 3).contains(point)) {
      return pad;
    }
  }
  return -1;
}

int MainComponent::VelocityForPointInPad(int pad, juce::Point<int> point)
    const
{
  const auto bounds = PadBounds(pad / 3, pad % 3);
  const float height_fraction =
      static_cast<float>(bounds.getBottom() - point.y)
      / static_cast<float>(juce::jmax(1, bounds.getHeight()));
  return juce::jlimit(
      1, 127, static_cast<int>(std::lround(height_fraction * 127.0f)));
}

void MainComponent::TriggerPad(int pad, int velocity, bool pedal_down)
{
  if (pad < 0 || pad >= KitModel::kPadCount) {
    return;
  }
  const auto p = static_cast<size_t>(pad);
  // Flash the pad in the velocity colour; the timer fades it out.
  pad_flash_velocity_[p] = velocity;
  pad_flash_ms_[p] = juce::Time::getMillisecondCounter();
  repaint(PadBounds(pad / 3, pad % 3));
  const PadParams& params = model_.params(pad);
  const LayerWeights weights = ComputeLayerWeights(params.mode, velocity,
      params.fade_point, params.fade_end, alternate_flip_[p], pedal_down);
  // Layer selection follows the strike velocity; loudness follows the
  // dynamics settings (a fixed full level when dynamics is off).
  const float loudness =
      params.dynamics ? DynamicsGain(params.curve, velocity) : 1.0f;
  if (params.mode == LayerMode::kAlternate) {
    alternate_flip_[p] = !alternate_flip_[p];
  }
  if (weights.choke) {
    // SW(MONO): only one voice from this pad — silence both layers
    // before the new hit sounds.
    for (int layer = 0; layer < KitModel::kLayersPerPad; ++layer) {
      const int idx = pad * KitModel::kLayersPerPad + layer;
      engine_.Stop(idx);
      slots_[static_cast<size_t>(idx)]->set_play_state(PlayState::kStopped);
    }
  }
  for (int layer = 0; layer < KitModel::kLayersPerPad; ++layer) {
    const float gain =
        (layer == 0 ? weights.top : weights.bottom) * loudness;
    const int idx = pad * KitModel::kLayersPerPad + layer;
    auto& slot = *slots_[static_cast<size_t>(idx)];
    if (gain <= 0.0f || !slot.is_playable()) {
      continue;
    }
    engine_.SetGain(idx, gain);
    if (slot.play_state() == PlayState::kPlaying) {
      engine_.Stop(idx);  // retrigger from the top
    }
    engine_.Play(idx);
    slot.set_play_state(PlayState::kPlaying);
    // Tint the layer with the loudness the layer mode gave it.
    slot.set_velocity_highlight(juce::jlimit(
        1, 127, static_cast<int>(std::lround(gain * 127.0f))));
    slot.FlashTransportButton(TransportAction::kPlay);
  }
}

void MainComponent::ApplyLayerParams(int pad)
{
  const auto p = static_cast<size_t>(pad);
  PadParams params = model_.params(pad);
  params.mode = static_cast<LayerMode>(mode_boxes_[p]->getSelectedId() - 1);
  params.fade_point = static_cast<int>(fade_point_sliders_[p]->getValue());
  params.fade_end = static_cast<int>(fade_end_sliders_[p]->getValue());
  if (params == model_.params(pad)) {
    return;
  }
  undo_.beginNewTransaction(
      "Change pad " + juce::String(pad + 1) + " layers");
  undo_.perform(new SetPadParamsAction(model_, pad, params));
}

void MainComponent::ShowPadMenu(int pad)
{
  const PadParams& params = model_.params(pad);
  juce::PopupMenu curve_menu;
  for (int c = 0; c < kDynamicsCurveCount; ++c) {
    const auto name = DynamicsCurveName(static_cast<DynamicsCurve>(c));
    curve_menu.addItem(100 + c, juce::String(name.data(), name.size()),
        params.dynamics, params.curve == static_cast<DynamicsCurve>(c));
  }
  juce::PopupMenu menu;
  menu.addItem(1, "Dynamics", true, params.dynamics);
  menu.addSubMenu("Dynamics Curve", curve_menu, params.dynamics);
  menu.addItem(2, "Trigger Reserve", true, params.trigger_reserve);
  menu.showMenuAsync(
      juce::PopupMenu::Options().withTargetComponent(
          pad_menu_buttons_[static_cast<size_t>(pad)].get()),
      [this, pad](int result)
      {
        if (result == 0) {
          return;  // dismissed
        }
        PadParams changed = model_.params(pad);
        if (result == 1) {
          changed.dynamics = !changed.dynamics;
        } else if (result == 2) {
          changed.trigger_reserve = !changed.trigger_reserve;
        } else if (result >= 100 && result < 100 + kDynamicsCurveCount) {
          changed.curve = static_cast<DynamicsCurve>(result - 100);
        }
        undo_.beginNewTransaction(
            "Change pad " + juce::String(pad + 1) + " dynamics");
        undo_.perform(new SetPadParamsAction(model_, pad, changed));
      });
}

void MainComponent::UpdatePadWidgets(int pad)
{
  const auto p = static_cast<size_t>(pad);
  const PadParams& params = model_.params(pad);
  mode_boxes_[p]->setSelectedId(
      static_cast<int>(params.mode) + 1, juce::dontSendNotification);
  fade_point_sliders_[p]->setValue(
      params.fade_point, juce::dontSendNotification);
  fade_end_sliders_[p]->setValue(
      params.fade_end, juce::dontSendNotification);
  fade_point_sliders_[p]->setVisible(UsesFadePoint(params.mode));
  fade_end_sliders_[p]->setVisible(UsesFadeEnd(params.mode));
}

void MainComponent::PadParamsChanged(int pad)
{
  document_.changed();
  UpdatePadWidgets(pad);
}

void MainComponent::MoveSample(int from, int to, bool copy)
{
  if (from == to || from < 0 || to < 0) {
    return;
  }
  const auto& file = model_.sample(
      from / KitModel::kLayersPerPad, from % KitModel::kLayersPerPad);
  undo_.beginNewTransaction(copy ? "Duplicate sample" : "Move sample");
  undo_.perform(new SetSampleAction(model_, to / KitModel::kLayersPerPad,
      to % KitModel::kLayersPerPad, file));
  if (!copy) {
    undo_.perform(new SetSampleAction(model_, from / KitModel::kLayersPerPad,
        from % KitModel::kLayersPerPad, juce::File()));
  }
}

void MainComponent::MovePad(int from_pad, int to_pad, bool copy)
{
  if (from_pad == to_pad || from_pad < 0 || to_pad < 0) {
    return;
  }
  // Copy up front so the source-clears below can't invalidate them.
  const juce::File top = model_.sample(from_pad, 0);
  const juce::File bottom = model_.sample(from_pad, 1);
  undo_.beginNewTransaction(copy ? "Duplicate pad" : "Move pad");
  undo_.perform(new SetSampleAction(model_, to_pad, 0, top));
  undo_.perform(new SetSampleAction(model_, to_pad, 1, bottom));
  if (!copy) {
    undo_.perform(new SetSampleAction(model_, from_pad, 0, juce::File()));
    undo_.perform(new SetSampleAction(model_, from_pad, 1, juce::File()));
  }
}

void MainComponent::SetDragTarget(int idx, bool whole_pad)
{
  // idx ^ 1 flips the layer bit, giving the other slot of the same pad.
  for (int i = 0; i < kSlotCount; ++i) {
    const bool on =
        idx >= 0 && (i == idx || (whole_pad && i == (idx ^ 1)));
    slots_[static_cast<size_t>(i)]->set_drag_hover(on);
  }
}

void MainComponent::OpenMidiInputs()
{
  for (const auto& info : juce::MidiInput::getAvailableDevices()) {
    if (auto in = juce::MidiInput::openDevice(info.identifier, this)) {
      in->start();
      std::fprintf(stderr, "midi: listening on '%s'\n", info.name.toRawUTF8());
      midi_inputs_.push_back(std::move(in));
    }
  }
}

void MainComponent::handleIncomingMidiMessage(
    juce::MidiInput*, const juce::MidiMessage& message)
{
  // Runs on the MIDI thread; marshal to the message thread before touching
  // the audio engine or UI.
  // CC4 is the hi-hat pedal on the HH CTRL jack; remember its position
  // for the HI-HAT layer mode (any channel).
  if (message.isController() && message.getControllerNumber() == 4) {
    hihat_cc_ = message.getControllerValue();
    return;
  }
  if (!message.isNoteOn() || message.getChannel() != kMidiChannel) {
    return;
  }
  const int pad = message.getNoteNumber() - kMidiNoteBase;
  if (pad < 0 || pad >= KitModel::kPadCount) {
    return;
  }
  const int velocity = message.getVelocity();
  juce::Component::SafePointer<MainComponent> safe(this);
  juce::MessageManager::callAsync(
      [safe, pad, velocity]
      {
        if (safe != nullptr) {
          // A real hit: velocity-aware, through the pad's layer mode.
          safe->TriggerPad(pad, velocity, safe->HiHatPedalDown());
        }
      });
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

  // Animate the pad flashes: repaint while fading, drop when expired.
  const auto now = juce::Time::getMillisecondCounter();
  for (int pad = 0; pad < KitModel::kPadCount; ++pad) {
    const auto p = static_cast<size_t>(pad);
    if (pad_flash_velocity_[p] > 0) {
      if (now - pad_flash_ms_[p] >= kPadFlashMs) {
        pad_flash_velocity_[p] = 0;
      }
      repaint(PadBounds(pad / 3, pad % 3));
    }
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
