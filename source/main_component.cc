#include "main_component.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <thread>

#include "actions.h"
#include "commands.h"
#include "device/kit_image.h"
#include "device/spdsx_device.h"
#include "sample_upload.h"
#include "spectro.h"
#include "sync_dialog.h"

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
// Edits autosave once they've been quiet this long.
constexpr juce::uint32 kAutosaveQuietMs = 1000;

const juce::Colour kWindowBg(0xff12161b);
const juce::Colour kPadBg(0xff161b22);
const juce::Colour kPadBorder(0xff242d38);
const juce::Colour kPadLabel(0xff8a97a6);

}  // namespace

juce::ApplicationProperties& MainComponent::ConfigureSettings() {
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
    , browser_(ConfigureSettings()) {
  browser_visible_ =
      settings_.getUserSettings()->getBoolValue("browserVisible", true);
  // The left panel: sample browser and device wave pool as tabs.
  const juce::Colour tab_bg(0xff161b22);
  panel_tabs_.addTab("Files", tab_bg, &browser_, false);
  panel_tabs_.addTab("Device", tab_bg, &device_samples_, false);
  panel_tabs_.setOutline(0);
  panel_tabs_.setVisible(browser_visible_);
  addChildComponent(panel_tabs_);

  setWantsKeyboardFocus(true);
  for (int i = 0; i < kSlotCount; ++i) {
    slots_[static_cast<size_t>(i)] = std::make_unique<SampleSlot>(i);
    auto& slot = *slots_[static_cast<size_t>(i)];
    slot.on_drop = [this](int idx, const juce::File& file) {
      LoadSample(idx, file);
    };
    slot.on_drop_device = [this](int idx, int sample) {
      undo().beginNewTransaction("Assign device sample");
      undo().perform(new SetSampleAction(model_,
                                         idx / KitModel::kLayersPerPad,
                                         idx % KitModel::kLayersPerPad,
                                         LayerSample::DeviceWave(sample)));
    };
    slot.on_click = [this](int idx) {
      // A click anywhere in a pad is a hit on the whole pad, at the
      // cursor-height velocity.
      const int pad = idx / KitModel::kLayersPerPad;
      const auto pos = getMouseXYRelative();
      TriggerPad(pad, VelocityForPointInPad(pad, pos), HiHatPedalDown());
    };
    slot.on_clear = [this](int idx) {
      undo().beginNewTransaction("Clear layer");
      undo().perform(new SetSampleAction(model_,
                                         idx / KitModel::kLayersPerPad,
                                         idx % KitModel::kLayersPerPad,
                                         LayerSample()));
    };
    slot.on_transport = [this](int idx, TransportAction action) {
      ApplyTransportAction(idx, action);
    };
    slot.on_slot_move = [this](int from, int to, bool copy, bool whole_pad) {
      if (whole_pad) {
        MovePad(
            from / KitModel::kLayersPerPad, to / KitModel::kLayersPerPad, copy);
      } else {
        MoveSample(from, to, copy);
      }
    };
    slot.on_drag_target = [this](int idx, bool whole_pad) {
      SetDragTarget(idx, whole_pad);
    };
    addAndMakeVisible(slot);
  }
  for (int pad = 0; pad < KitModel::kPadCount; ++pad) {
    const auto p = static_cast<size_t>(pad);
    mode_boxes_[p] = std::make_unique<juce::ComboBox>();
    for (int m = 0; m < kLayerModeCount; ++m) {
      const auto name = LayerModeName(static_cast<LayerMode>(m));
      // Item ids are mode + 1; 0 means "nothing selected" to ComboBox.
      mode_boxes_[p]->addItem(juce::String(name.data(), name.size()), m + 1);
    }
    mode_boxes_[p]->onChange = [this, pad] { ApplyLayerParams(pad); };
    addAndMakeVisible(*mode_boxes_[p]);
    auto make_fade_slider = [this, pad] {
      auto slider = std::make_unique<juce::Slider>(juce::Slider::LinearBar,
                                                   juce::Slider::NoTextBox);
      slider->setRange(1, 127, 1);
      // One undo step per adjustment, not one per drag pixel.
      slider->setChangeNotificationOnlyOnRelease(true);
      // No room for a permanent readout; a bubble shows the value
      // while dragging.
      slider->setPopupDisplayEnabled(true, false, this);
      slider->onValueChange = [this, pad] { ApplyLayerParams(pad); };
      addAndMakeVisible(*slider);
      return slider;
    };
    fade_point_sliders_[p] = make_fade_slider();
    fade_end_sliders_[p] = make_fade_slider();
    pad_menu_buttons_[p] =
        std::make_unique<juce::TextButton>(juce::String::fromUTF8("⋯"));
    pad_menu_buttons_[p]->onClick = [this, pad] { ShowPadSettings(pad); };
    addAndMakeVisible(*pad_menu_buttons_[p]);
    UpdatePadWidgets(pad);
  }
  // Keyboard pad hits (keys 1-9) carry this velocity; MIDI hits carry
  // their own. Low values audition the soft side of the fade modes. A
  // compact knob; click its value to type one in.
  velocity_slider_.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
  velocity_slider_.setRange(1, 127, 1);
  velocity_slider_.setValue(
      settings_.getUserSettings()->getIntValue("uiVelocity", 100),
      juce::dontSendNotification);
  velocity_slider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 30, 18);
  velocity_slider_.setTextBoxIsEditable(true);
  // Tint the dial with the same blue->amber->red velocity colour the
  // fade bars use.
  auto tint_velocity_slider = [this] {
    velocity_slider_.setColour(
        juce::Slider::rotarySliderFillColourId,
        VelocityColour(static_cast<int>(velocity_slider_.getValue())));
  };
  tint_velocity_slider();
  velocity_slider_.onValueChange = [this, tint_velocity_slider] {
    settings_.getUserSettings()->setValue(
        "uiVelocity", static_cast<int>(velocity_slider_.getValue()));
    tint_velocity_slider();
  };
  addAndMakeVisible(velocity_slider_);
  velocity_caption_.setText("VEL", juce::dontSendNotification);
  velocity_caption_.setFont(juce::Font(juce::FontOptions(12.0f)).boldened());
  velocity_caption_.setColour(juce::Label::textColourId, kPadLabel);
  velocity_caption_.setJustificationType(juce::Justification::centredRight);
  addAndMakeVisible(velocity_caption_);

  // Appears in the header only when the active kit references device
  // waves not yet in the local cache; one click downloads them.
  transfer_button_.onClick = [this] { DownloadKitSamples(); };
  transfer_button_.setColour(juce::TextButton::buttonColourId,
                             juce::Colour(0xff2f6a4f));
  addChildComponent(transfer_button_);
  addAndMakeVisible(connection_dot_);
  // The primary sync action: appears when the active kit has edits not
  // yet pushed to the device.
  save_button_.onClick = [this] { SaveChangesToDevice(); };
  save_button_.setColour(juce::TextButton::buttonColourId,
                         juce::Colour(0xffb5761f));
  addChildComponent(save_button_);
  // The unified kit control: arrows and menu switch kits (stashing the
  // old one), the pencil renames in place.
  kit_chooser_.kit_name = [this](int i) {
    return i == device_.current_kit() ? model_.name() : device_.kit(i).name;
  };
  kit_chooser_.on_select = [this](int index) {
    if (index != device_.current_kit()) {
      document_.SwitchKit(index);
      MarkEdited();  // persists the new current kit
      RefreshKitSelector();
      RefreshDocumentState();
      UpdateSaveButton();  // reflect the newly-active kit's dirty state
      SyncDeviceKit();  // the connected unit follows to this kit
    }
  };
  kit_chooser_.on_rename = [this](const juce::String& name) {
    undo().beginNewTransaction("Rename kit");
    undo().perform(new SetKitNameAction(model_, name));
  };
  addAndMakeVisible(kit_chooser_);
  browser_.on_preview = [this](const juce::File& file) {
    engine_.PreviewFile(file);
  };
  device_samples_.on_preview = [this](const device::SampleRecord& rec) {
    // Autoplay-gated selection preview, mirroring the file browser:
    // audition a device wave once it's in the local cache.
    const juce::File cached = document_.CachedWaveFile(rec.index);
    if (cached != juce::File()) {
      engine_.PreviewFile(cached);
    }
  };

  model_.AddListener(this);
  document_.on_history_reset = [this] {
    for (auto& u : undos_) {
      if (u != nullptr) {
        u->clearUndoHistory();
      }
    }
    // A wholesale content replacement can move the dirty-vs-base line.
    UpdateSaveButton();
  };
  document_.on_model_reload = [this](bool loading) {
    model_loading_ = loading;
  };
  // Start as a fresh untitled device, so the model reflects kit 1 and
  // every header widget agrees with it.
  document_.ResetToUntitled();
  RefreshKitSelector();
  OpenMidiInputs();
  setSize(960, 720);
  // Drives the hover poll, the playhead, and end-of-sample detection.
  startTimerHz(30);
}

MainComponent::~MainComponent() {
  HideProgress();  // don't leave a modal window behind on quit
  model_.RemoveListener(this);
}

void MainComponent::LoadSample(int idx, const juce::File& file) {
  if (idx < 0 || idx >= kSlotCount) {
    std::fprintf(stderr, "slot %d out of range (0..%d)\n", idx, kSlotCount - 1);
    return;
  }
  undo().beginNewTransaction("Load " + file.getFileName());
  undo().perform(new SetSampleAction(model_,
                                     idx / KitModel::kLayersPerPad,
                                     idx % KitModel::kLayersPerPad,
                                     LayerSample(file)));
}

void MainComponent::OpenLastDocument() {
  const juce::File last(
      settings_.getUserSettings()->getValue("lastDeviceFile"));
  if (last.existsAsFile() && document_.OpenDevice(last).wasOk()) {
    RefreshKitSelector();
    RefreshDocumentState();
    return;
  }
  // First launch (or the document moved): autosave needs a target from
  // the very first edit, so live in a default document.
  const auto fallback =
      juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
          .getChildFile("SPD-SX PRO.spdsx");
  if (!(fallback.existsAsFile() && document_.OpenDevice(fallback).wasOk())) {
    document_.CreateNew(fallback);
  }
  RefreshKitSelector();
  RefreshDocumentState();
}

void MainComponent::OpenDocument(const juce::File& file) {
  if (const auto r = document_.OpenDevice(file); r.failed()) {
    juce::AlertWindow::showMessageBoxAsync(
        juce::MessageBoxIconType::WarningIcon,
        "Open a device",
        r.getErrorMessage());
  }
  RefreshKitSelector();
  RefreshDocumentState();
}

juce::ApplicationCommandTarget* MainComponent::getNextCommandTarget() {
  return nullptr;
}

void MainComponent::getAllCommands(juce::Array<juce::CommandID>& ids) {
  ids.addArray({commands::kUndo,
                commands::kRedo,
                commands::kFileNew,
                commands::kFileOpen,
                commands::kFileSaveAs,
                commands::kImportKit,
                commands::kLoadDeviceState,
                commands::kDownloadKitSamples,
                commands::kSaveToDevice,
                commands::kToggleBrowser,
                commands::kToggleAutoplay});
}

void MainComponent::getCommandInfo(juce::CommandID id,
                                   juce::ApplicationCommandInfo& info) {
  switch (id) {
    case commands::kUndo:
      info.setInfo("Undo", "Undo the last change", "Edit", 0);
      info.addDefaultKeypress('z', juce::ModifierKeys::commandModifier);
      info.setActive(undo().canUndo());
      break;
    case commands::kRedo:
      info.setInfo("Redo", "Redo the last undone change", "Edit", 0);
      info.addDefaultKeypress('z',
                              juce::ModifierKeys::commandModifier
                                  | juce::ModifierKeys::shiftModifier);
      info.setActive(undo().canRedo());
      break;
    case commands::kFileNew:
      info.setInfo(
          "New Device...", "Create a fresh device document", "File", 0);
      info.addDefaultKeypress('n', juce::ModifierKeys::commandModifier);
      break;
    case commands::kFileOpen:
      info.setInfo("Open...", "Open a device document", "File", 0);
      info.addDefaultKeypress('o', juce::ModifierKeys::commandModifier);
      break;
    case commands::kFileSaveAs:
      info.setInfo("Save As...",
                   "Move the device document; autosaves follow it",
                   "File",
                   0);
      info.addDefaultKeypress('s',
                              juce::ModifierKeys::commandModifier
                                  | juce::ModifierKeys::shiftModifier);
      break;
    case commands::kImportKit:
      info.setInfo("Import Kit...",
                   "Load a legacy single-kit .kit file into the current kit",
                   "File",
                   0);
      break;
    case commands::kLoadDeviceState:
      info.setInfo(
          "Load Device State...",
          "Replace this whole document with the device's current state",
          "File",
          0);
      info.setActive(!device_fetching_ && DeviceConnected());
      break;
    case commands::kSaveToDevice: {
      const int dirty = static_cast<int>(document_.DirtyKits().size());
      info.setInfo(dirty > 1 ? "Save Changes to Device (" + juce::String(dirty)
                           + " kits)"
                             : juce::String("Save Changes to Device"),
                   "Push this document's edits to the connected device",
                   "File",
                   0);
      // The document autosaves locally, so plain Save is free and means
      // the one save that matters here: committing edits to the hardware.
      info.addDefaultKeypress('s', juce::ModifierKeys::commandModifier);
      info.setActive(dirty > 0 && !device_fetching_ && DeviceConnected());
      break;
    }
    case commands::kDownloadKitSamples:
      info.setInfo("Download Kit Samples",
                   "Fetch this kit's device waves into the local cache so they "
                   "play",
                   "File",
                   0);
      info.setActive(!device_fetching_ && DeviceConnected());
      break;
    case commands::kToggleBrowser:
      info.setInfo(
          "Sample Browser", "Show or hide the sample browser panel", "View", 0);
      info.addDefaultKeypress('b', juce::ModifierKeys::commandModifier);
      info.setTicked(browser_visible_);
      break;
    case commands::kToggleAutoplay:
      info.setInfo("Auto-play While Browsing",
                   "Audition samples as you select them in the browser",
                   "View",
                   0);
      info.setTicked(
          settings_.getUserSettings()->getBoolValue("autoplayBrowsing", false));
      break;
    default:
      break;
  }
}

bool MainComponent::perform(const InvocationInfo& info) {
  switch (info.commandID) {
    case commands::kUndo:
      return undo().undo();
    case commands::kRedo:
      return undo().redo();
    case commands::kFileNew:
      // Nothing to prompt about — the current document is autosaved.
      // A new device needs a home up front so autosave has a target.
      document_.Autosave();
      open_chooser_ = std::make_unique<juce::FileChooser>(
          "Create a device",
          juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
              .getChildFile("Untitled Device.spdsx"),
          "*.spdsx");
      open_chooser_->launchAsync(
          juce::FileBrowserComponent::saveMode
              | juce::FileBrowserComponent::canSelectFiles
              | juce::FileBrowserComponent::warnAboutOverwriting,
          [this](const juce::FileChooser& fc) {
            auto file = fc.getResult();
            if (file == juce::File()) {
              return;
            }
            if (auto r = document_.CreateNew(file.withFileExtension(".spdsx"));
                r.failed()) {
              juce::AlertWindow::showMessageBoxAsync(
                  juce::MessageBoxIconType::WarningIcon,
                  "Create a device",
                  r.getErrorMessage());
            }
            RefreshKitSelector();
            RefreshDocumentState();
          });
      return true;
    case commands::kFileOpen:
      document_.Autosave();
      open_chooser_ = std::make_unique<juce::FileChooser>(
          "Open a device",
          juce::File(settings_.getUserSettings()->getValue("lastDeviceFile"))
              .getParentDirectory(),
          "*.spdsx");
      open_chooser_->launchAsync(
          juce::FileBrowserComponent::openMode
              | juce::FileBrowserComponent::canSelectFiles,
          [this](const juce::FileChooser& fc) {
            if (const auto file = fc.getResult(); file != juce::File()) {
              OpenDocument(file);
            }
          });
      return true;
    case commands::kImportKit:
      import_chooser_ = std::make_unique<juce::FileChooser>(
          "Import a kit into kit " + juce::String(device_.current_kit() + 1),
          juce::File(),
          "*.kit");
      import_chooser_->launchAsync(
          juce::FileBrowserComponent::openMode
              | juce::FileBrowserComponent::canSelectFiles,
          [this](const juce::FileChooser& fc) {
            if (auto file = fc.getResult(); file.existsAsFile()) {
              if (auto result = document_.ImportKitFile(file);
                  result.failed()) {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::WarningIcon,
                    "Import Kit",
                    result.getErrorMessage());
              }
              RefreshKitSelector();
              RefreshDocumentState();
            }
          });
      return true;
    case commands::kFileSaveAs:
      document_.saveAsInteractiveAsync(
          true, [this](juce::FileBasedDocument::SaveResult) {
            RefreshDocumentState();
          });
      return true;
    case commands::kLoadDeviceState:
      LoadDeviceState();
      return true;
    case commands::kDownloadKitSamples:
      DownloadKitSamples();
      return true;
    case commands::kSaveToDevice:
      SaveChangesToDevice();
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

void MainComponent::SetBrowserVisible(bool visible) {
  browser_visible_ = visible;
  panel_tabs_.setVisible(visible);
  settings_.getUserSettings()->setValue("browserVisible", visible);
  commands_.commandStatusChanged();  // menu tick
  resized();
  repaint();
}

// Window title carries the device and the active kit. No dirty state:
// every edit autosaves.
void MainComponent::RefreshDocumentState() {
  if (auto* window =
          dynamic_cast<juce::DocumentWindow*>(getTopLevelComponent())) {
    window->setName(
        document_.getDocumentTitle() + juce::String::fromUTF8(" \xe2\x80\x94 ")
        + juce::String(device_.current_kit() + 1) + ": " + model_.name());
  }
  // Loads and kit switches can change every widget in the header.
  kit_chooser_.SetCurrent(device_.current_kit(), model_.name());
  repaint(0, 0, getWidth(), kHeaderHeight);
  // Open/new/import can also swap the wave pool out from under the
  // device tab and any device-wave slots.
  RefreshDeviceSamples();
  UpdateTransferButton();
}

void MainComponent::LoadDeviceState() {
  if (device_fetching_) {
    return;
  }
  juce::Component::SafePointer<MainComponent> safe(this);
  juce::AlertWindow::showAsync(
      juce::MessageBoxOptions()
          .withIconType(juce::MessageBoxIconType::WarningIcon)
          .withTitle("Load Device State")
          .withMessage(juce::String::fromUTF8(
              "This replaces EVERYTHING in this document with the "
              "device's current state: all 200 kits — names, sample "
              "assignments, layer parameters — and the wave pool.\n\n"
              "Your local edits will be lost. This cannot be undone."))
          .withButton("Replace Everything")
          .withButton("Cancel"),
      [safe](int result) {
        // The first button returns 1 (classic OK/cancel mapping).
        if (result == 1 && safe != nullptr) {
          safe->StartDeviceStateFetch();
        }
      });
}

void MainComponent::ShowProgress(const juce::String& title,
                                 const juce::String& message) {
  HideProgress();  // never stack two
  ProgressDialog* dialog = nullptr;
  progress_win_ = ProgressDialog::Show(title, message, &dialog);
  progress_dialog_ = dialog;
}

void MainComponent::HideProgress() {
  if (progress_win_ != nullptr) {
    // Exiting the modal state closes and deletes the window (and its
    // owned ProgressDialog); the SafePointers then read null.
    progress_win_->exitModalState(0);
  }
  progress_win_ = nullptr;
  progress_dialog_ = nullptr;
}

void MainComponent::StartDeviceStateFetch() {
  if (device_fetching_.exchange(true)) {
    return;  // a fetch is already running
  }
  commands_.commandStatusChanged();  // grey the menu item
  auto blocks = std::make_shared<std::atomic<int>>(0);
  fetch_blocks_ = blocks;
  ShowProgress("Load Device State",
               juce::String::fromUTF8("Connecting\xe2\x80\xa6"));
  // The dumps are megabytes over a serial link; a detached worker owns
  // the port and reports back through the message thread. It shares
  // only the counter (by shared_ptr) and a SafePointer checked on the
  // message thread, so quitting mid-fetch can't dangle.
  juce::Component::SafePointer<MainComponent> safe(this);
  std::thread([safe, blocks] {
    std::vector<device::KitRecord> kits;
    std::vector<device::SampleRecord> pool;
    juce::String error;
    try {
      const std::unique_ptr<device::SerialPort> serial =
          device::PlatformPorts().Open(device::FindDevicePort());
      device::SpdsxDevice dev(serial.get());
      const auto count = [&blocks](const device::Bytes&) { ++*blocks; };
      kits = device::ParseKits(
          device::CleanBulkImage(dev.DumpBank(device::kBankKits, count)));
      pool = device::ParseSampleDir(
          device::CleanBulkImage(dev.DumpBank(device::kBankSamples, count)));
      if (kits.empty() || pool.empty()) {
        error =
            "the device's reply was incomplete (kits or samples "
            "missing)";
      }
    } catch (const std::exception& e) {
      error = e.what();
    }
    juce::MessageManager::callAsync([safe,
                                     kits = std::move(kits),
                                     pool = std::move(pool),
                                     error]() mutable {
      if (safe != nullptr) {
        safe->FinishDeviceFetch(std::move(kits), std::move(pool), error);
      }
    });
  }).detach();
}

void MainComponent::FinishDeviceFetch(std::vector<device::KitRecord> kits,
                                      std::vector<device::SampleRecord> pool,
                                      const juce::String& error) {
  device_fetching_ = false;
  fetch_blocks_.reset();
  HideProgress();
  commands_.commandStatusChanged();
  if (error.isNotEmpty()) {
    juce::AlertWindow::showMessageBoxAsync(
        juce::MessageBoxIconType::WarningIcon, "Load Device State", error);
    return;
  }
  document_.ReplaceWithDeviceState(kits, std::move(pool));
  MarkEdited();
  RefreshKitSelector();
  RefreshDocumentState();
}

void MainComponent::DownloadKitSamples() {
  if (device_fetching_) {
    return;
  }
  // The active kit's device waves whose audio isn't cached in the
  // document yet (computed here on the message thread).
  std::vector<int> want;
  for (int pad = 0; pad < KitModel::kPadCount; ++pad) {
    for (int layer = 0; layer < KitModel::kLayersPerPad; ++layer) {
      const LayerSample& s = model_.sample(pad, layer);
      if (!s.is_device() || document_.HasCachedAudio(s.device_index)) {
        continue;
      }
      if (std::find(want.begin(), want.end(), s.device_index) == want.end()) {
        want.push_back(s.device_index);
      }
    }
  }
  if (want.empty()) {
    device_samples_.SetStatus("kit samples already cached");
    juce::Timer::callAfterDelay(
        1500, [safe = juce::Component::SafePointer<MainComponent>(this)] {
          if (safe != nullptr) {
            safe->device_samples_.SetStatus({});
          }
        });
    return;
  }
  device_fetching_ = true;
  commands_.commandStatusChanged();
  UpdateTransferButton();  // hide while the fetch runs
  // Which waves this run covers, and the shared progress the worker
  // publishes: the wave currently transferring and its permille. The
  // timer reads these to drive each slot's throbber/ring.
  download_indices_ = want;
  download_current_ = std::make_shared<std::atomic<int>>(0);
  download_permille_ = std::make_shared<std::atomic<int>>(0);
  UpdateDownloadIndicators();
  device_samples_.SetStatus(
      juce::String::fromUTF8("downloading samples\xe2\x80\xa6"));
  juce::Component::SafePointer<MainComponent> safe(this);
  auto current = download_current_;
  auto permille = download_permille_;
  std::thread([safe, want, current, permille] {
    juce::String error;
    int done = 0;
    int failed = 0;
    try {
      const std::unique_ptr<device::SerialPort> serial =
          device::PlatformPorts().Open(device::FindDevicePort());
      device::SpdsxDevice dev(serial.get());
      for (const int index : want) {
        permille->store(0);
        current->store(index);
        // Each wave stands alone: one that can't be read (some factory
        // preloads have no exportable file — the device 7a-errors the
        // STAT) is skipped, not fatal to the rest of the kit.
        device::Bytes wav;
        try {
          const device::Bytes smp =
              dev.ReadRemoteWave(index, [permille](size_t got, size_t total) {
                permille->store(total > 0 ? static_cast<int>(got * 1000 / total)
                                          : 0);
              });
          wav = device::RfwvToWav(smp);
        } catch (const std::exception&) {
          ++failed;
          current->store(0);
          continue;
        }
        current->store(0);
        if (wav.empty()) {
          ++failed;  // registered but didn't convert (bad/empty RFWV)
          continue;
        }
        ++done;
        // Store the blob + refresh slots on the message thread (the DB is
        // only touched there).
        juce::MessageManager::callAsync([safe, index, wav = std::move(wav)] {
          if (safe != nullptr) {
            safe->OnWaveDownloaded(index, wav);
          }
        });
      }
    } catch (const std::exception& e) {
      error = e.what();  // couldn't open the port at all — fatal to the batch
    }
    juce::MessageManager::callAsync([safe, error, done, failed] {
      if (safe != nullptr) {
        safe->FinishKitSampleDownload(error, done, failed);
      }
    });
  }).detach();
}

int MainComponent::UncachedDeviceWaveCount() const {
  std::vector<int> seen;
  for (int pad = 0; pad < KitModel::kPadCount; ++pad) {
    for (int layer = 0; layer < KitModel::kLayersPerPad; ++layer) {
      const LayerSample& s = model_.sample(pad, layer);
      if (!s.is_device()) {
        continue;
      }
      if (!document_.HasCachedAudio(s.device_index)
          && std::find(seen.begin(), seen.end(), s.device_index)
              == seen.end()) {
        seen.push_back(s.device_index);
      }
    }
  }
  return static_cast<int>(seen.size());
}

void MainComponent::UpdateTransferButton() {
  // Hidden while a fetch runs (its progress shows in the slots and the
  // Device tab) and when nothing is missing.
  const int n = device_fetching_ ? 0 : UncachedDeviceWaveCount();
  transfer_button_.setButtonText(juce::String::fromUTF8("\xe2\x86\x93 ")
                                 + juce::String(n)
                                 + (n == 1 ? " sample" : " samples"));
  const bool show = n > 0;
  // Greyed when the device isn't connected — there's nothing to download
  // from, but keep it visible so the pending count still shows.
  transfer_button_.setEnabled(DeviceConnected());
  if (show != transfer_button_.isVisible()) {
    transfer_button_.setVisible(show);
    resized();  // reclaim/space the header
  }
}

void MainComponent::SyncDeviceKit() {
  // Only when a device is present and no larger op holds the port; the
  // kit-select is cheap but still opens the port for the round trip.
  if (!DeviceConnected() || device_fetching_) {
    return;
  }
  pending_select_kit_->store(device_.current_kit() + 1);  // device is 1-based
  if (kit_select_running_->exchange(true)) {
    return;  // a worker is already running; it will pick up the new pending
  }
  auto pending = pending_select_kit_;
  auto running = kit_select_running_;
  std::thread([pending, running] {
    try {
      const std::unique_ptr<device::SerialPort> serial =
          device::PlatformPorts().Open(device::FindDevicePort());
      device::SpdsxDevice dev(serial.get());
      // Coalesce: send the latest pending kit, and keep sending while the
      // user switches again mid-flight, so we never leave the unit stale.
      int sent = -1;
      for (int want = pending->load(); want != sent; want = pending->load()) {
        sent = want;
        dev.SelectKit(want);
      }
    } catch (const std::exception&) {
      // The device went away, or the port was busy: the unit just doesn't
      // follow this time. The next switch (or a reconnect) tries again.
    }
    running->store(false);
  }).detach();
}

void MainComponent::PollConnection() {
  // One probe at a time, throttled; skip while a device operation holds
  // the port (we're plainly connected then, and a second open would
  // clash).
  constexpr juce::uint32 kPollIntervalMs = 2000;
  if (device_fetching_ || conn_check_running_.load()
      || kit_select_running_->load()) {
    return;
  }
  const juce::uint32 now = juce::Time::getMillisecondCounter();
  if (last_conn_check_ms_ != 0 && now - last_conn_check_ms_ < kPollIntervalMs) {
    return;
  }
  last_conn_check_ms_ = now;
  conn_check_running_ = true;
  juce::Component::SafePointer<MainComponent> safe(this);
  std::thread([safe] {
    bool connected = false;
    try {
      connected = !device::FindDevicePort().empty();
    } catch (const std::exception&) {
      connected = false;  // no node, or nothing answered
    }
    juce::MessageManager::callAsync([safe, connected] {
      if (safe == nullptr) {
        return;
      }
      safe->conn_check_running_ = false;
      if (connected != safe->device_connected_.load()) {
        safe->device_connected_ = connected;
        safe->connection_dot_.SetConnected(connected);
        safe->commands_.commandStatusChanged();  // re-enable device menu items
        safe->UpdateTransferButton();
        safe->UpdateSaveButton();  // enable/disable with connection
      }
    });
  }).detach();
}

void MainComponent::UpdateSaveButton() {
  const int dirty = static_cast<int>(document_.DirtyKits().size());
  const juce::String label = "Save Changes to Device";
  const juce::String text =
      dirty > 1 ? label + " (" + juce::String(dirty) + " kits)" : label;
  save_button_.setEnabled(DeviceConnected() && !device_fetching_);
  const bool show = dirty > 0;
  if (text != save_button_.getButtonText()
      || show != save_button_.isVisible()) {
    save_button_.setButtonText(text);
    save_button_.setVisible(show);
    resized();  // reclaim/space the header for the (possibly wider) label
  }
}

void MainComponent::SaveChangesToDevice() {
  if (device_fetching_.exchange(true)) {
    return;  // the port is busy (a fetch, a download, or another sync)
  }
  document_.StashActiveKit();
  const std::vector<int> dirty = document_.DirtyKits();
  if (dirty.empty() || !DeviceConnected()) {
    device_fetching_ = false;
    UpdateSaveButton();
    return;
  }
  // Uploads need a trustworthy picture of which pool indices are free,
  // so a sync that will upload also re-reads the pool directory.
  bool need_pool = false;
  for (const int kit : dirty) {
    const KitData content = document_.KitContent(kit);
    for (const auto& pad : content.pads) {
      need_pool |= pad.samples.first.is_file() || pad.samples.second.is_file();
    }
  }
  commands_.commandStatusChanged();
  UpdateSaveButton();  // disabled while the sync runs
  device_samples_.SetStatus(
      juce::String::fromUTF8("sync: reading device state\xe2\x80\xa6"));
  juce::Component::SafePointer<MainComponent> safe(this);
  std::thread([safe, need_pool] {
    std::vector<device::KitRecord> kits;
    std::vector<device::SampleRecord> pool;
    juce::String error;
    try {
      const std::unique_ptr<device::SerialPort> serial =
          device::PlatformPorts().Open(device::FindDevicePort());
      device::SpdsxDevice dev(serial.get());
      kits = device::ParseKits(
          device::CleanBulkImage(dev.DumpBank(device::kBankKits)));
      if (kits.empty()) {
        error = "the device's kit data came back empty";
      } else if (need_pool) {
        pool = device::ParseSampleDir(
            device::CleanBulkImage(dev.DumpBank(device::kBankSamples)));
        if (pool.empty()) {
          error = "the device's sample directory came back empty";
        }
      }
    } catch (const std::exception& e) {
      error = e.what();
    }
    juce::MessageManager::callAsync([safe,
                                     kits = std::move(kits),
                                     pool = std::move(pool),
                                     error]() mutable {
      if (safe != nullptr) {
        safe->FinishSyncFetch(std::move(kits), std::move(pool), error);
      }
    });
  }).detach();
}

void MainComponent::FinishSyncFetch(std::vector<device::KitRecord> kits,
                                    std::vector<device::SampleRecord> pool,
                                    const juce::String& error) {
  if (error.isNotEmpty()) {
    device_samples_.SetStatus({});
    juce::AlertWindow::showMessageBoxAsync(
        juce::MessageBoxIconType::WarningIcon, "Save Changes to Device", error);
    CancelSync();
    return;
  }
  sync_ = std::make_unique<SyncSession>();
  sync_->theirs.reserve(DeviceModel::kKitCount);
  for (int i = 0; i < DeviceModel::kKitCount; ++i) {
    sync_->theirs.push_back(
        i < static_cast<int>(kits.size())
            ? KitDataFromDevice(kits[static_cast<size_t>(i)])
            : KitData());
  }
  if (!pool.empty()) {
    // The fresh directory is simply newer truth; audio blobs for
    // surviving indices are kept.
    document_.UpdateSamplePool(std::move(pool));
    RefreshDeviceSamples();
  }
  for (int i = 0; i < DeviceModel::kKitCount; ++i) {
    const auto found = FindKitConflicts(i,
                                        document_.KitContent(i),
                                        document_.BaseKit(i),
                                        sync_->theirs[static_cast<size_t>(i)]);
    sync_->conflicts.insert(sync_->conflicts.end(), found.begin(), found.end());
  }
  if (sync_->conflicts.empty()) {
    RunSyncPush({});
    return;
  }
  device_samples_.SetStatus(
      juce::String::fromUTF8("sync: resolving conflicts\xe2\x80\xa6"));
  juce::Component::SafePointer<MainComponent> safe(this);
  SyncConflictPanel::Show(
      sync_->conflicts,
      [safe](std::vector<SyncResolution> resolutions) {
        if (safe != nullptr) {
          safe->RunSyncPush(resolutions);
        }
      },
      [safe] {
        if (safe != nullptr) {
          safe->device_samples_.SetStatus({});
          safe->CancelSync();
        }
      });
}

void MainComponent::RunSyncPush(
    const std::vector<SyncResolution>& resolutions) {
  jassert(sync_ != nullptr);

  // Fold the dialog's answers (parallel to sync_->conflicts) back into
  // per-kit resolution tables; everything unconflicted merges cleanly
  // whatever the table says.
  struct KitResolutions {
    SyncResolution name = SyncResolution::kMine;
    std::array<SyncResolution, KitModel::kPadCount> pads;

    KitResolutions() { pads.fill(SyncResolution::kMine); }
  };

  std::map<int, KitResolutions> by_kit;
  for (size_t i = 0; i < sync_->conflicts.size() && i < resolutions.size();
       ++i) {
    const SyncConflict& conflict = sync_->conflicts[i];
    auto& kit = by_kit[conflict.kit];
    if (conflict.pad < 0) {
      kit.name = resolutions[i];
    } else {
      kit.pads[static_cast<size_t>(conflict.pad)] = resolutions[i];
    }
  }

  for (int i = 0; i < DeviceModel::kKitCount; ++i) {
    const KitData current = document_.KitContent(i);
    const KitData& base = document_.BaseKit(i);
    const KitData& theirs = sync_->theirs[static_cast<size_t>(i)];
    if (current == base && theirs == base) {
      continue;
    }
    const KitResolutions res =
        by_kit.count(i) != 0 ? by_kit[i] : KitResolutions();
    KitSyncPlan plan = PlanKitSync(current, base, theirs, res.name, res.pads);
    const bool relevant = plan.WritesDevice() || plan.new_current != current
        || plan.new_base != base;
    if (!relevant) {
      continue;
    }
    sync_->pulled |= plan.new_current != current;
    sync_->plans.emplace_back(i, std::move(plan));
  }
  if (sync_->plans.empty()) {
    FinishSyncPush({}, true);  // conflicts all skipped; nothing to do
    return;
  }

  sync_->uploads = PlanUploads(sync_->plans, device_.sample_pool());
  SubstituteUploads(sync_->plans, sync_->uploads);
  std::vector<KitWrite> writes;
  for (const auto& [kit, plan] : sync_->plans) {
    KitWrite write = BuildKitWrite(kit, plan);
    if (write.name || !write.pads.empty()) {
      writes.push_back(std::move(write));
    }
  }

  device_samples_.SetStatus(
      juce::String::fromUTF8("sync: saving to device\xe2\x80\xa6"));
  juce::Component::SafePointer<MainComponent> safe(this);
  std::thread([safe, uploads = sync_->uploads, writes = std::move(writes)] {
    juce::String error;
    bool committed = false;
    try {
      // Convert every file before touching the port, so a bad file
      // aborts the push with the device untouched.
      std::vector<SmpUpload> smp_uploads;
      for (const UploadPlan& plan : uploads) {
        juce::String convert_error;
        SmpUpload upload;
        upload.index = plan.index;
        upload.smp = SmpFromAudioFile(plan.file, convert_error);
        upload.wavename = plan.wavename;
        upload.filename = plan.filename;
        if (upload.smp.empty()) {
          throw std::runtime_error(convert_error.toStdString());
        }
        smp_uploads.push_back(std::move(upload));
      }
      const std::unique_ptr<device::SerialPort> serial =
          device::PlatformPorts().Open(device::FindDevicePort());
      device::SpdsxDevice dev(serial.get());
      committed = ExecutePush(
          dev, smp_uploads, writes, [safe, &uploads](const SmpUpload& done) {
            // This upload is durable (UploadWave commits); tell the
            // document even if a later step fails.
            const auto plan = std::find_if(
                uploads.begin(), uploads.end(), [&done](const UploadPlan& p) {
                  return p.index == done.index;
                });
            if (plan == uploads.end()) {
              return;
            }
            const device::Bytes wav_bytes = device::RfwvToWav(done.smp);
            const device::RfwvHeader header = device::ParseRfwvHeader(done.smp);
            const int frames = header.channels > 0
                ? static_cast<int>(header.data_bytes / (2u * header.channels))
                : 0;
            juce::MemoryBlock wav(wav_bytes.data(), wav_bytes.size());
            juce::MessageManager::callAsync(
                [safe, plan = *plan, wav = std::move(wav), frames]() mutable {
                  if (safe != nullptr) {
                    safe->OnWaveUploaded(plan, std::move(wav), frames);
                  }
                });
          });
    } catch (const std::exception& e) {
      error = e.what();
    }
    juce::MessageManager::callAsync([safe, error, committed] {
      if (safe != nullptr) {
        safe->FinishSyncPush(error, committed);
      }
    });
  }).detach();
}

void MainComponent::CancelSync() {
  sync_.reset();
  device_fetching_ = false;
  commands_.commandStatusChanged();
  UpdateSaveButton();
}

void MainComponent::OnWaveUploaded(UploadPlan plan,
                                   juce::MemoryBlock wav,
                                   int frames) {
  device::SampleRecord record;
  record.index = plan.index;
  record.wavename = plan.wavename;
  record.filename = plan.filename;
  record.frames = static_cast<uint32_t>(frames);
  document_.AddPoolRecord(record);
  document_.StoreWaveAudio(plan.index, wav);
  // The file has become a device wave everywhere it was assigned; the
  // active kit's slots follow through the model listeners.
  document_.ReplaceFileLayers(plan.file, plan.index);
  MarkEdited();
  RefreshDeviceSamples();
}

void MainComponent::FinishSyncPush(const juce::String& error, bool committed) {
  device_samples_.SetStatus({});
  if (sync_ == nullptr) {
    CancelSync();
    return;
  }
  if (error.isNotEmpty() || !committed) {
    juce::AlertWindow::showMessageBoxAsync(
        juce::MessageBoxIconType::WarningIcon,
        "Save Changes to Device",
        error.isNotEmpty()
            ? error
            : juce::String("the device did not confirm the flash commit"));
    // Nothing advanced: the kits stay dirty and the next sync re-diffs
    // against a fresh device read. Completed uploads were recorded as
    // they landed, so a retry won't re-send them.
    CancelSync();
    return;
  }
  for (const auto& [kit, plan] : sync_->plans) {
    document_.ApplySyncedKit(kit, plan.new_current, plan.new_base);
  }
  document_.PersistSync();
  if (sync_->pulled) {
    // Device-side changes replaced local content outside the undo
    // system; stale histories would undo into nonsense.
    for (auto& u : undos_) {
      if (u != nullptr) {
        u->clearUndoHistory();
      }
    }
  }
  const bool skipped = std::any_of(
      sync_->plans.begin(), sync_->plans.end(), [](const auto& entry) {
        return entry.second.skipped;
      });
  sync_.reset();
  device_fetching_ = false;
  commands_.commandStatusChanged();
  RefreshKitSelector();
  RefreshDocumentState();
  UpdateSaveButton();
  device_samples_.SetStatus(skipped ? "synced (skipped conflicts remain)"
                                    : "synced with device");
  juce::Timer::callAfterDelay(
      2000, [safe = juce::Component::SafePointer<MainComponent>(this)] {
        if (safe != nullptr) {
          safe->device_samples_.SetStatus({});
        }
      });
}

void MainComponent::UpdateDownloadIndicators() {
  if (download_indices_.empty()) {
    return;
  }
  const int current =
      download_current_ != nullptr ? download_current_->load() : 0;
  const float progress = download_permille_ != nullptr
      ? download_permille_->load() / 1000.0f
      : 0.0f;
  for (int pad = 0; pad < KitModel::kPadCount; ++pad) {
    for (int layer = 0; layer < KitModel::kLayersPerPad; ++layer) {
      const LayerSample& s = model_.sample(pad, layer);
      if (!s.is_device()
          || std::find(download_indices_.begin(),
                       download_indices_.end(),
                       s.device_index)
              == download_indices_.end()) {
        continue;
      }
      auto& slot =
          *slots_[static_cast<size_t>(pad * KitModel::kLayersPerPad + layer)];
      if (s.device_index == current) {
        slot.SetDownloadState(SampleSlot::DownloadState::kActive, progress);
      } else {
        slot.SetDownloadState(SampleSlot::DownloadState::kPending);
      }
    }
  }
}

void MainComponent::OnWaveDownloaded(int sample_index,
                                     const device::Bytes& wav) {
  // On the message thread: store the blob in the document, then refresh.
  document_.StoreWaveAudio(sample_index,
                           juce::MemoryBlock(wav.data(), wav.size()));
  OnWaveCached(sample_index);
}

void MainComponent::OnWaveCached(int sample_index) {
  // Refresh any slot in the active kit now backed by this cached wave.
  for (int pad = 0; pad < KitModel::kPadCount; ++pad) {
    for (int layer = 0; layer < KitModel::kLayersPerPad; ++layer) {
      const LayerSample& s = model_.sample(pad, layer);
      if (s.is_device() && s.device_index == sample_index) {
        SyncSlotFromModel(pad, layer);
      }
    }
  }
}

void MainComponent::FinishKitSampleDownload(const juce::String& error,
                                            int done,
                                            int failed) {
  device_fetching_ = false;
  device_samples_.SetStatus({});
  commands_.commandStatusChanged();
  // Clear indicators and settle every affected slot to its final state
  // (playable if cached, "on device" if it was skipped or failed).
  download_indices_.clear();
  download_current_.reset();
  download_permille_.reset();
  for (int pad = 0; pad < KitModel::kPadCount; ++pad) {
    for (int layer = 0; layer < KitModel::kLayersPerPad; ++layer) {
      if (model_.sample(pad, layer).is_device()) {
        SyncSlotFromModel(pad, layer);
      }
    }
  }
  UpdateTransferButton();
  // Couldn't even open the port: nothing downloaded, so surface it.
  if (error.isNotEmpty() && done == 0) {
    juce::AlertWindow::showMessageBoxAsync(
        juce::MessageBoxIconType::WarningIcon, "Download Kit Samples", error);
    return;
  }
  // Some waves can't be read (factory preloads without an exportable
  // file); note it quietly rather than alarming — the rest are cached.
  if (failed > 0) {
    const juce::String note = juce::String(failed)
        + (failed == 1 ? " sample couldn't be downloaded"
                       : " samples couldn't be downloaded");
    device_samples_.SetStatus(note);
    juce::Timer::callAfterDelay(
        3000, [safe = juce::Component::SafePointer<MainComponent>(this)] {
          if (safe != nullptr) {
            safe->device_samples_.SetStatus({});
          }
        });
  }
}

void MainComponent::RefreshDeviceSamples() {
  device_samples_.Refresh();
  // Re-resolve device-wave slot displays against the (new) pool.
  for (int pad = 0; pad < KitModel::kPadCount; ++pad) {
    for (int layer = 0; layer < KitModel::kLayersPerPad; ++layer) {
      if (model_.sample(pad, layer).is_device()) {
        SyncSlotFromModel(pad, layer);
      }
    }
  }
}

void MainComponent::RefreshKitSelector() {
  kit_chooser_.SetCurrent(device_.current_kit(), model_.name());
}

juce::UndoManager& MainComponent::undo() {
  auto& u = undos_[static_cast<size_t>(device_.current_kit())];
  if (u == nullptr) {
    u = std::make_unique<juce::UndoManager>();
  }
  return *u;
}

void MainComponent::MarkEdited() {
  last_edit_ms_ = juce::Time::getMillisecondCounter();
  document_.changed();
}

void MainComponent::KitNameChanged() {
  kit_chooser_.SetCurrent(device_.current_kit(), model_.name());
  MarkEdited();
  UpdateSaveButton();
  RefreshDocumentState();
}

// The model is the source of truth: engine and slot display sync to it
// here, whether the change came from a user gesture, undo, or a loaded
// kit file. The pad-shaped model maps to the flat slot components as
// idx = pad * 2 + layer.
void MainComponent::SampleChanged(int pad, int layer) {
  MarkEdited();
  UpdateSaveButton();
  SyncSlotFromModel(pad, layer);
}

void MainComponent::SyncSlotFromModel(int pad, int layer) {
  const int idx = pad * KitModel::kLayersPerPad + layer;
  const LayerSample& sample = model_.sample(pad, layer);
  auto& slot = *slots_[static_cast<size_t>(idx)];
  if (sample.empty()) {
    engine_.Clear(idx);
    slot.ClearSample();
    return;
  }
  if (sample.is_device()) {
    const auto* rec = device_.FindSample(sample.device_index);
    const juce::String name = rec != nullptr
        ? juce::String(rec->wavename)
        : "#" + juce::String(sample.device_index);
    // Cached device waves play and render like any file; uncached ones
    // show a placeholder until the user downloads them.
    const juce::File cached = document_.CachedWaveFile(sample.device_index);
    if (cached != juce::File()) {
      LoadAudioIntoSlot(idx, cached, name);
    } else {
      engine_.Clear(idx);
      slot.SetDeviceSample(
          name,
          rec != nullptr ? static_cast<double>(rec->frames) / 48000.0 : 0.0);
    }
    return;
  }
  LoadAudioIntoSlot(idx, sample.file, sample.file.getFileName());
}

void MainComponent::LoadAudioIntoSlot(int idx,
                                      const juce::File& file,
                                      const juce::String& display_name) {
  auto& slot = *slots_[static_cast<size_t>(idx)];
  auto info = engine_.Load(idx, file);
  if (!info) {
    // Unreadable (moved, unmounted, not audio): keep the assignment
    // visible so it survives a save/load round trip.
    engine_.Clear(idx);
    slot.SetSampleMissing(display_name);
    return;
  }
  // Too-short files play fine but render no spectrogram; the slot just
  // shows the info bar in that case.
  juce::Image image;
  if (auto png = render_spectrogram(file.getFullPathName().toStdString(), idx);
      !png.empty()) {
    image = juce::ImageFileFormat::loadFrom(juce::File(png));
  }
  slot.SetSample(
      display_name, info->duration_seconds, info->sample_rate, image);
}

void MainComponent::paint(juce::Graphics& g) {
  g.fillAll(kWindowBg);

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
      g.drawText(
          juce::String(r * 3 + c + 1),
          pad.reduced(kPadPadding + 2, kPadPadding).removeFromTop(kPadHeader),
          juce::Justification::centredLeft);
    }
  }
}

juce::Rectangle<int> MainComponent::GridArea() const {
  auto area = getLocalBounds();
  area.removeFromTop(kHeaderHeight);
  if (browser_visible_) {
    area.removeFromLeft(kBrowserWidth);
  }
  return area;
}

juce::Rectangle<int> MainComponent::PadBounds(int row, int col) const {
  const auto area = GridArea();
  const int cell_w =
      (area.getWidth() - 2 * kGridPadding - 2 * kGridSpacing) / 3;
  const int cell_h =
      (area.getHeight() - 2 * kGridPadding - 2 * kGridSpacing) / 3;
  return {area.getX() + kGridPadding + col * (cell_w + kGridSpacing),
          area.getY() + kGridPadding + row * (cell_h + kGridSpacing),
          cell_w,
          cell_h};
}

void MainComponent::resized() {
  // Right edge of the header, laid out right-to-left: the compact
  // velocity knob, then (when visible) the transfer button.
  auto header = getLocalBounds().removeFromTop(kHeaderHeight);
  // Connection light at the far left of the header.
  connection_dot_.setBounds(header.removeFromLeft(24));
  header.removeFromRight(10);
  auto vel = header.removeFromRight(96);
  velocity_slider_.setBounds(
      vel.removeFromRight(62).withSizeKeepingCentre(62, kHeaderHeight - 8));
  velocity_caption_.setBounds(vel);
  if (transfer_button_.isVisible()) {
    header.removeFromRight(8);
    transfer_button_.setBounds(
        header.removeFromRight(120).withSizeKeepingCentre(120, 26));
  }
  if (save_button_.isVisible()) {
    header.removeFromRight(8);
    // Wide enough for the "(N kits)" suffix when several kits are dirty.
    const int w = juce::jmax(180, save_button_.getBestWidthForHeight(26));
    save_button_.setBounds(
        header.removeFromRight(w).withSizeKeepingCentre(w, 26));
  }
  // The kit chooser owns what's LEFT of the header — `header` has been
  // whittled down by everything laid out above, so sizing within it is
  // what keeps the chooser clear of the save/transfer buttons.
  kit_chooser_.setBounds(header.withSizeKeepingCentre(
      juce::jmin(500, header.getWidth() - 16), 28));
  panel_tabs_.setBounds(
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

bool MainComponent::keyPressed(const juce::KeyPress& key) {
  // Delete/Backspace clears the layer under the cursor (same edit as the
  // slot's right-click Clear). The 30 Hz hover poll keeps hovered_ current.
  if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) {
    if (hovered_ >= 0 && hovered_ < kSlotCount
        && slots_[static_cast<size_t>(hovered_)]->has_sample()) {
      undo().beginNewTransaction("Clear layer");
      undo().perform(new SetSampleAction(model_,
                                         hovered_ / KitModel::kLayersPerPad,
                                         hovered_ % KitModel::kLayersPerPad,
                                         LayerSample()));
    }
    return true;
  }
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
  } else if (const auto pos =
                 juce::String("!@#$%^&*(").indexOfChar(key.getTextCharacter());
             pos >= 0) {
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
bool MainComponent::keyStateChanged(bool /*is_key_down*/) {
  SetHiHatKeyDown(juce::KeyPress::isKeyCurrentlyDown('H')
                  || juce::KeyPress::isKeyCurrentlyDown('h'));
  if (held_space_
      && !juce::KeyPress::isKeyCurrentlyDown(juce::KeyPress::spaceKey)) {
    held_space_ = false;
  }
  for (int pad = 0; pad < KitModel::kPadCount; ++pad) {
    // A held digit can morph between '1' and '!' if shift changes
    // mid-hold; the key only counts as released when both are up.
    if (held_pad_keys_[static_cast<size_t>(pad)]
        && !juce::KeyPress::isKeyCurrentlyDown('1' + pad)
        && !juce::KeyPress::isKeyCurrentlyDown("!@#$%^&*("[pad])) {
      held_pad_keys_[static_cast<size_t>(pad)] = false;
    }
  }
  return false;
}

void MainComponent::SetHiHatKeyDown(bool down) {
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
    slots_[static_cast<size_t>(open_idx)]->set_play_state(PlayState::kStopped);
    TriggerPad(pad, velocity, /*pedal_down=*/true);
  }
}

bool MainComponent::HiHatPedalDown() const {
  return hihat_key_down_ || hihat_cc_.load() >= 64;
}

void MainComponent::ApplyTransportAction(int idx, TransportAction action) {
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
void MainComponent::mouseDown(const juce::MouseEvent& event) {
  const auto pos = event.getPosition();
  if (const int pad = PadAt(pos); pad >= 0) {
    TriggerPad(pad, VelocityForPointInPad(pad, pos), HiHatPedalDown());
  }
}

int MainComponent::PadAt(juce::Point<int> point) const {
  for (int pad = 0; pad < KitModel::kPadCount; ++pad) {
    if (PadBounds(pad / 3, pad % 3).contains(point)) {
      return pad;
    }
  }
  return -1;
}

int MainComponent::VelocityForPointInPad(int pad,
                                         juce::Point<int> point) const {
  const auto bounds = PadBounds(pad / 3, pad % 3);
  const float height_fraction = static_cast<float>(bounds.getBottom() - point.y)
      / static_cast<float>(juce::jmax(1, bounds.getHeight()));
  return juce::jlimit(
      1, 127, static_cast<int>(std::lround(height_fraction * 127.0f)));
}

void MainComponent::TriggerPad(int pad, int velocity, bool pedal_down) {
  if (pad < 0 || pad >= KitModel::kPadCount) {
    return;
  }
  const auto p = static_cast<size_t>(pad);
  // Flash the pad in the velocity colour; the timer fades it out.
  pad_flash_velocity_[p] = velocity;
  pad_flash_ms_[p] = juce::Time::getMillisecondCounter();
  repaint(PadBounds(pad / 3, pad % 3));
  const PadParams& params = model_.params(pad);
  LayerWeights weights = ComputeLayerWeights(params.mode,
                                             velocity,
                                             params.fade_point,
                                             params.fade_end,
                                             alternate_flip_[p],
                                             pedal_down);
  if (params.mode == LayerMode::kHiHat) {
    // Closed-pedal volume shapes the closed (top) layer. Its fade
    // in/decay siblings need engine envelopes and stay device-only.
    weights.top *= static_cast<float>(params.hi_hat_volume) / 127.0f;
  }
  // Layer selection follows the strike velocity; loudness follows the
  // dynamics settings (dynamics off = every hit at the pad's fixed
  // velocity level).
  const float loudness = params.dynamics
      ? DynamicsGain(params.curve, velocity)
      : DynamicsGain(DynamicsCurve::kLinear, params.fixed_velocity);
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
    const float gain = (layer == 0 ? weights.top : weights.bottom) * loudness;
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
    slot.set_velocity_highlight(
        juce::jlimit(1, 127, static_cast<int>(std::lround(gain * 127.0f))));
    slot.FlashTransportButton(TransportAction::kPlay);
  }
}

void MainComponent::ApplyLayerParams(int pad) {
  const auto p = static_cast<size_t>(pad);
  PadParams params = model_.params(pad);
  params.mode = static_cast<LayerMode>(mode_boxes_[p]->getSelectedId() - 1);
  params.fade_point = static_cast<int>(fade_point_sliders_[p]->getValue());
  params.fade_end = static_cast<int>(fade_end_sliders_[p]->getValue());
  // Fade end is constrained to >= fade point: pushing the point past
  // the end drags the end along (the end slider's minimum tracks the
  // point, so it can't be dragged below on its own).
  params.fade_end = juce::jmax(params.fade_end, params.fade_point);
  if (params == model_.params(pad)) {
    return;
  }
  undo().beginNewTransaction("Change pad " + juce::String(pad + 1) + " layers");
  undo().perform(new SetPadParamsAction(model_, pad, params));
}

void MainComponent::ShowPadSettings(int pad) {
  auto panel = std::make_unique<PadSettingsPanel>();
  panel->SetParams(model_.params(pad));
  panel->on_change = [this, pad](const PadParams& edited) {
    // The panel only owns these four fields; the pad's mode and fade
    // values may be edited in the header while the panel is open.
    PadParams changed = model_.params(pad);
    changed.dynamics = edited.dynamics;
    changed.curve = edited.curve;
    changed.fixed_velocity = edited.fixed_velocity;
    changed.trigger_reserve = edited.trigger_reserve;
    changed.hi_hat_volume = edited.hi_hat_volume;
    changed.hi_hat_fade_in = edited.hi_hat_fade_in;
    changed.hi_hat_decay = edited.hi_hat_decay;
    if (changed == model_.params(pad)) {
      return;
    }
    undo().beginNewTransaction("Change pad " + juce::String(pad + 1)
                               + " settings");
    undo().perform(new SetPadParamsAction(model_, pad, changed));
  };
  pad_settings_panel_ = panel.get();
  pad_settings_pad_ = pad;
  juce::CallOutBox::launchAsynchronously(
      std::move(panel),
      pad_menu_buttons_[static_cast<size_t>(pad)]->getScreenBounds(),
      nullptr);
}

void MainComponent::UpdatePadWidgets(int pad) {
  const auto p = static_cast<size_t>(pad);
  const PadParams& params = model_.params(pad);
  mode_boxes_[p]->setSelectedId(static_cast<int>(params.mode) + 1,
                                juce::dontSendNotification);
  fade_point_sliders_[p]->setValue(params.fade_point,
                                   juce::dontSendNotification);
  // The end can't go below the point; keep the constraint in the
  // slider's own range so drags stop there instead of snapping back.
  fade_end_sliders_[p]->setRange(params.fade_point, 127, 1);
  fade_end_sliders_[p]->setValue(params.fade_end, juce::dontSendNotification);
  // The default LookAndFeel's bar fill is indistinguishable from our
  // background; paint each bar in its value's velocity colour (the
  // same blue->amber->red language the pad flashes use).
  fade_point_sliders_[p]->setColour(
      juce::Slider::trackColourId,
      VelocityColour(params.fade_point).withAlpha(0.5f));
  fade_end_sliders_[p]->setColour(
      juce::Slider::trackColourId,
      VelocityColour(params.fade_end).withAlpha(0.5f));
  fade_point_sliders_[p]->setVisible(UsesFadePoint(params.mode));
  fade_end_sliders_[p]->setVisible(UsesFadeEnd(params.mode));
}

void MainComponent::PadParamsChanged(int pad) {
  MarkEdited();
  UpdateSaveButton();
  UpdatePadWidgets(pad);
  // Keep an open settings panel honest when undo/redo (or anything
  // else) changes the pad underneath it.
  if (pad_settings_panel_ != nullptr && pad_settings_pad_ == pad) {
    pad_settings_panel_->SetParams(model_.params(pad));
  }
}

void MainComponent::MoveSample(int from, int to, bool copy) {
  if (from == to || from < 0 || to < 0) {
    return;
  }
  const LayerSample sample = model_.sample(from / KitModel::kLayersPerPad,
                                           from % KitModel::kLayersPerPad);
  undo().beginNewTransaction(copy ? "Duplicate sample" : "Move sample");
  undo().perform(new SetSampleAction(model_,
                                     to / KitModel::kLayersPerPad,
                                     to % KitModel::kLayersPerPad,
                                     sample));
  if (!copy) {
    undo().perform(new SetSampleAction(model_,
                                       from / KitModel::kLayersPerPad,
                                       from % KitModel::kLayersPerPad,
                                       LayerSample()));
  }
}

void MainComponent::MovePad(int from_pad, int to_pad, bool copy) {
  if (from_pad == to_pad || from_pad < 0 || to_pad < 0) {
    return;
  }
  // Copy up front so the source-clears below can't invalidate them.
  const LayerSample top = model_.sample(from_pad, 0);
  const LayerSample bottom = model_.sample(from_pad, 1);
  undo().beginNewTransaction(copy ? "Duplicate pad" : "Move pad");
  undo().perform(new SetSampleAction(model_, to_pad, 0, top));
  undo().perform(new SetSampleAction(model_, to_pad, 1, bottom));
  if (!copy) {
    undo().perform(new SetSampleAction(model_, from_pad, 0, LayerSample()));
    undo().perform(new SetSampleAction(model_, from_pad, 1, LayerSample()));
  }
}

void MainComponent::SetDragTarget(int idx, bool whole_pad) {
  // idx ^ 1 flips the layer bit, giving the other slot of the same pad.
  for (int i = 0; i < kSlotCount; ++i) {
    const bool on = idx >= 0 && (i == idx || (whole_pad && i == (idx ^ 1)));
    slots_[static_cast<size_t>(i)]->set_drag_hover(on);
  }
}

void MainComponent::OpenMidiInputs() {
  for (const auto& info : juce::MidiInput::getAvailableDevices()) {
    if (auto in = juce::MidiInput::openDevice(info.identifier, this)) {
      in->start();
      std::fprintf(stderr, "midi: listening on '%s'\n", info.name.toRawUTF8());
      midi_inputs_.push_back(std::move(in));
    }
  }
}

void MainComponent::handleIncomingMidiMessage(
    juce::MidiInput*, const juce::MidiMessage& message) {
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
  juce::MessageManager::callAsync([safe, pad, velocity] {
    if (safe != nullptr) {
      // A real hit: velocity-aware, through the pad's layer mode.
      safe->TriggerPad(pad, velocity, safe->HiHatPedalDown());
    }
  });
}

void MainComponent::timerCallback() {
  // Keep menu enablement in step with the undo history.
  if (undo().canUndo() != could_undo_ || undo().canRedo() != could_redo_) {
    could_undo_ = undo().canUndo();
    could_redo_ = undo().canRedo();
    commands_.commandStatusChanged();
  }

  PollConnection();

  // Live detail in the progress dialog while a device fetch streams blocks.
  if (device_fetching_ && fetch_blocks_ != nullptr
      && progress_dialog_ != nullptr) {
    const int n = fetch_blocks_->load();
    progress_dialog_->SetMessage(
        juce::String::fromUTF8("Reading device state\xe2\x80\xa6\n")
        + juce::String(n) + (n == 1 ? " block" : " blocks"));
  }
  // Animate the per-slot download throbbers/rings while fetching waves.
  UpdateDownloadIndicators();

  // Autosave once edits have gone quiet; every mutation is persisted,
  // there is no explicit save.
  if (document_.hasChangedSinceSaved()
      && juce::Time::getMillisecondCounter() - last_edit_ms_
          >= kAutosaveQuietMs) {
    document_.Autosave();
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
