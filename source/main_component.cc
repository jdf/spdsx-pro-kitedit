#include "main_component.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <thread>

#include "about_dialog.h"
#include "actions.h"
#include "app_log.h"
#include "bulk_edit.h"
#include "cli_install.h"
#include "commands.h"
#include "device/kit_image.h"
#include "device/spdsx_device.h"
#include "disclaimer_dialog.h"
#include "feedback_dialog.h"
#include "sample_upload.h"
#include "spectro.h"
#include "sync_dialog.h"

namespace spdsx {

namespace {

// The SPD-SX PRO sends pad hits as note-on on channel 10, pads 1-9 on
// notes 60-68.
constexpr int kMidiChannel = 10;
constexpr int kMidiNoteBase = 60;

constexpr int kHeaderHeight = 44;  // the kit-editor row (chooser, VEL)
constexpr int kGlobalBarHeight = 36;  // connection dot, status, sync button
constexpr int kTabBarHeight = 32;
// Digit-key hits (and the keyboard hi-hat chick) play at this velocity;
// mouse clicks carry cursor height and MIDI carries its own.
constexpr int kKeyVelocity = 100;
constexpr int kSurfaceBarHeight = 30;
// The left panel's width limits; the width itself is a dragged,
// persisted member.
constexpr int kBrowserMinWidth = 200;
constexpr int kBrowserMaxWidth = 520;
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
    , browser_(ConfigureSettings())
    , bulk_panel_(BulkEditPanel::Handlers {
          .set_mode =
              [this](const ops::SetModeRequest& request) {
                return ApplyBulkSetMode(request);
              },
          .pad_link =
              [this](const ops::PadLinkRequest& request) {
                return ApplyBulkPadLink(request);
              }}) {
  browser_visible_ =
      settings_.getUserSettings()->getBoolValue("browserVisible", true);
  // The left panel: sample browser and device wave pool as tabs.
  const juce::Colour tab_bg(0xff161b22);
  panel_tabs_.addTab("Files", tab_bg, &browser_, false);
  panel_tabs_.addTab("Device", tab_bg, &device_samples_, false);
  panel_tabs_.addTab("Properties", tab_bg, &properties_tab_, false);
  properties_tab_.panel.on_change = [this](const PadParams& edited) {
    // The panel edits the whole PadParams; fields it has no control
    // for (trigger reserve) ride through unchanged.
    if (edited == model_.params(selected_)) {
      return;
    }
    const juce::String which = KitModel::IsTrigger(selected_)
        ? "trigger " + juce::String(KitModel::TriggerOf(selected_) + 1)
        : "pad " + juce::String(selected_ + 1);
    undo().beginNewTransaction("change " + which + " settings");
    undo().perform(new SetPadParamsAction(model_, selected_, edited));
  };
  panel_tabs_.setOutline(0);
  panel_tabs_.setVisible(browser_visible_);
  browser_width_ = juce::jlimit(
      kBrowserMinWidth,
      kBrowserMaxWidth,
      settings_.getUserSettings()->getIntValue("browserWidth", 260));
  panel_divider_.width_at_drag_start = [this] { return browser_width_; };
  panel_divider_.on_drag = [this](int width) {
    const int clamped =
        juce::jlimit(kBrowserMinWidth,
                     juce::jmin(kBrowserMaxWidth, getWidth() - 300),
                     width);
    if (clamped != browser_width_) {
      browser_width_ = clamped;
      settings_.getUserSettings()->setValue("browserWidth", clamped);
      resized();
      repaint();
    }
  };
  addChildComponent(panel_divider_);
  panel_tabs_.on_change = [this](int index) {
    settings_.getUserSettings()->setValue("uiPanelTab", index);
  };
  addChildComponent(panel_tabs_);

  // A quiet strip along the bottom: device and sync narration, where it
  // stays visible whatever panel or tab is showing.
  status_bar_.setFont(juce::FontOptions(12.0f));
  status_bar_.setColour(juce::Label::textColourId, juce::Colour(0xff8b949e));
  status_bar_.setJustificationType(juce::Justification::centredLeft);
  status_bar_.setInterceptsMouseClicks(false, false);
  addAndMakeVisible(status_bar_);

  setWantsKeyboardFocus(true);
  for (int i = 0; i < kSlotCount; ++i) {
    slots_[static_cast<size_t>(i)] = std::make_unique<SampleSlot>(i);
    auto& slot = *slots_[static_cast<size_t>(i)];
    slot.on_drop = [this](int idx, const juce::File& file) {
      LoadSample(idx, file);
    };
    slot.on_drop_device = [this](int idx, int sample) {
      undo().beginNewTransaction("assign device sample");
      undo().perform(new SetSampleAction(model_,
                                         idx / KitModel::kLayersPerPad,
                                         idx % KitModel::kLayersPerPad,
                                         LayerSample::DeviceWave(sample)));
    };
    slot.on_click = [this](int idx) {
      // A click anywhere in a pad is a hit on the whole pad, at the
      // cursor-height velocity — and it selects the pad.
      const int pad = idx / KitModel::kLayersPerPad;
      SelectObject(pad);
      const auto pos = getMouseXYRelative();
      TriggerPad(pad, VelocityForPointInPad(pad, pos), HiHatPedalDown());
    };
    slot.on_clear = [this](int idx) {
      undo().beginNewTransaction("clear layer");
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
  for (int pad = 0; pad < KitModel::kObjectCount; ++pad) {
    UpdatePadWidgets(pad);
  }
  // Appears in the header only when the active kit references device
  // waves not yet in the local cache; one click downloads them.
  transfer_button_.onClick = [this] { DownloadKitSamples(); };
  transfer_button_.setColour(juce::TextButton::buttonColourId,
                             juce::Colour(0xff2f6a4f));
  addChildComponent(transfer_button_);
  addAndMakeVisible(connection_dot_);
  // The primary sync action: appears when the active kit has edits not
  // yet pushed to the device.
  sync_button_.onClick = [this] { SyncChangesWithDevice(); };
  sync_button_.setColour(juce::TextButton::buttonColourId,
                         juce::Colour(0xffb5761f));
  addChildComponent(sync_button_);
  // The unified kit control: arrows and menu switch kits (stashing the
  // old one), the pencil renames in place.
  kit_chooser_.kit_name = [this](int i) {
    return i == device_.current_kit() ? model_.name() : device_.kit(i).name;
  };
  kit_chooser_.on_select = [this](int index) {
    if (index != device_.current_kit()) {
      AdoptKit(index);
      SyncDeviceKit();  // the connected unit follows to this kit
    }
  };
  kit_chooser_.on_rename = [this](const juce::String& name) {
    undo().beginNewTransaction("rename kit");
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
    UpdateSyncButton();
  };
  document_.on_model_reload = [this](bool loading) {
    model_loading_ = loading;
  };
  // Start as a fresh untitled device, so the model reflects kit 1 and
  // every header widget agrees with it.
  document_.ResetToUntitled();
  RefreshKitSelector();
  OpenMidiInputs();

  // The kit editor's surface switch: the pads, or the external trigger
  // inputs (structurally pads since the trigger-table mapping).
  surface_tabs_.addTab("Pads", juce::Colour(0xff161b22), 0);
  surface_tabs_.addTab("Triggers", juce::Colour(0xff161b22), 1);
  surface_tabs_.on_change = [this](int index) { SelectSurface(index); };
  addAndMakeVisible(surface_tabs_);

  // The tab bar and the Bulk Edit tab. Everything constructed above
  // belongs to Edit Kits; snapshot the list now so switching tabs can
  // hide and show it wholesale (with the conditional widgets re-fixed by
  // SelectTab). The global-bar widgets stay visible on both tabs.
  tabs_.addTab("Edit Kits", juce::Colour(0xff161b22), 0);
  tabs_.addTab("Bulk Edit", juce::Colour(0xff161b22), 1);
  tabs_.on_change = [this](int index) { SelectTab(index); };
  for (int i = 0; i < getNumChildComponents(); ++i) {
    juce::Component* child = getChildComponent(i);
    if (child != &connection_dot_ && child != &status_bar_
        && child != &sync_button_) {
      edit_tab_children_.push_back(child);
    }
  }
  addAndMakeVisible(tabs_);
  addChildComponent(bulk_panel_);

  RefreshProperties();

  // Come back up on the tabs the user left: the left panel's
  // Files/Device/Properties, the editing surface, and the main tab.
  panel_tabs_.setCurrentTabIndex(
      juce::jlimit(0,
                   panel_tabs_.getNumTabs() - 1,
                   settings_.getUserSettings()->getIntValue("uiPanelTab", 0)));
  SelectSurface(juce::jlimit(
      0, 1, settings_.getUserSettings()->getIntValue("uiSurface", 0)));
  SelectTab(juce::jlimit(
      0, 1, settings_.getUserSettings()->getIntValue("uiMainTab", 0)));

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
  undo().beginNewTransaction("load " + file.getFileName());
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
    AppLog::Note("open document failed: " + r.getErrorMessage());
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
                commands::kSyncWithDevice,
                commands::kToggleBrowser,
                commands::kToggleAutoplay,
                commands::kSendFeedback,
                commands::kInstallCli,
                commands::kAbout,
                commands::kShowEditKits,
                commands::kShowBulkEdit});
}

void MainComponent::getCommandInfo(juce::CommandID id,
                                   juce::ApplicationCommandInfo& info) {
  switch (id) {
    case commands::kUndo: {
      // The menu names what it will undo: "Undo layer mode change".
      const juce::String what = undo().getUndoDescription();
      info.setInfo(what.isNotEmpty() ? "Undo " + what : juce::String("Undo"),
                   "Undo the last change",
                   "Edit",
                   0);
      info.addDefaultKeypress('z', juce::ModifierKeys::commandModifier);
      info.setActive(undo().canUndo());
      break;
    }
    case commands::kRedo: {
      const juce::String what = undo().getRedoDescription();
      info.setInfo(what.isNotEmpty() ? "Redo " + what : juce::String("Redo"),
                   "Redo the last undone change",
                   "Edit",
                   0);
      info.addDefaultKeypress('z',
                              juce::ModifierKeys::commandModifier
                                  | juce::ModifierKeys::shiftModifier);
      info.setActive(undo().canRedo());
      break;
    }
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
    case commands::kSyncWithDevice: {
      const int dirty = static_cast<int>(document_.DirtyKits().size());
      info.setInfo(dirty > 1 ? "Sync Changes with Device ("
                           + juce::String(dirty) + " kits)"
                             : juce::String("Sync Changes with Device"),
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
    case commands::kSendFeedback:
      info.setInfo(juce::String::fromUTF8("Report a Bug or Send Feedback…"),
                   "File a report straight from the app — no account needed",
                   "Help",
                   0);
      break;
    case commands::kInstallCli:
      info.setInfo(juce::String::fromUTF8("Install Command-Line Tool…"),
                   "Put the bundled spdutil on your PATH",
                   "Application",
                   0);
      break;
    case commands::kAbout:
      info.setInfo("About SPD-SX PROgram",
                   "Version and dependency credits",
                   "Application",
                   0);
      break;
    case commands::kShowEditKits:
      info.setInfo("Edit Kits", "The kit editor tab", "View", 0);
      info.addDefaultKeypress('1', juce::ModifierKeys::commandModifier);
      break;
    case commands::kShowBulkEdit:
      info.setInfo("Bulk Edit",
                   "Edit many kits at once; sync pushes the changes",
                   "View",
                   0);
      info.addDefaultKeypress('2', juce::ModifierKeys::commandModifier);
      break;
    default:
      break;
  }
}

bool MainComponent::perform(const InvocationInfo& info) {
  switch (info.commandID) {
    case commands::kUndo: {
      const bool ok = undo().undo();
      // A bulk action edits kits with no listener attached; refresh what
      // watches the document as a whole.
      MarkEdited();
      UpdateSyncButton();
      commands_.commandStatusChanged();
      return ok;
    }
    case commands::kRedo: {
      const bool ok = undo().redo();
      MarkEdited();
      UpdateSyncButton();
      commands_.commandStatusChanged();
      return ok;
    }
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
    case commands::kSyncWithDevice:
      SyncChangesWithDevice();
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
    case commands::kSendFeedback: {
      BugReport seed;
      auto* app = juce::JUCEApplication::getInstance();
      seed.app_version =
          app != nullptr ? app->getApplicationVersion() : juce::String("dev");
      seed.os = juce::SystemStats::getOperatingSystemName();
      seed.device = DeviceConnected() ? "connected"
              + (device_firmware_.isNotEmpty()
                     ? ", firmware " + device_firmware_
                     : juce::String())
                                      : juce::String("not connected");
      seed.document =
          "schema v" + juce::String(DeviceDb::kCurrentSchemaVersion);
      FeedbackPanel::Show(std::move(seed));
      return true;
    }
    case commands::kInstallCli:
      InstallCli();
      return true;
    case commands::kShowEditKits:
      SelectTab(0);
      return true;
    case commands::kShowBulkEdit:
      SelectTab(1);
      return true;
    case commands::kAbout: {
      auto* app = juce::JUCEApplication::getInstance();
      AboutPanel::Show(app != nullptr ? app->getApplicationVersion()
                                      : juce::String("dev"));
      return true;
    }
    default:
      return false;
  }
}

namespace {

void ReportCliInstall(bool ok) {
  if (ok) {
    juce::AlertWindow::showMessageBoxAsync(
        juce::MessageBoxIconType::InfoIcon,
        "Install Command-Line Tool",
        juce::String::fromUTF8(
            "spdutil is installed — run “spdutil” in a Terminal."));
  } else {
    juce::AlertWindow::showMessageBoxAsync(
        juce::MessageBoxIconType::WarningIcon,
        "Install Command-Line Tool",
        "spdutil was not installed.");
  }
}

}  // namespace

void MainComponent::SelectTab(int index) {
  if (tabs_.getCurrentTabIndex() != index) {
    tabs_.setCurrentTabIndex(index, false);
  }
  settings_.getUserSettings()->setValue("uiMainTab", index);
  on_edit_tab_ = index == 0;
  for (juce::Component* child : edit_tab_children_) {
    child->setVisible(on_edit_tab_);
  }
  bulk_panel_.setVisible(!on_edit_tab_);
  if (on_edit_tab_) {
    // The wholesale show above is too blunt for the conditional widgets;
    // re-apply their own rules.
    panel_tabs_.setVisible(browser_visible_);
    panel_divider_.setVisible(browser_visible_);
    for (int pad = 0; pad < KitModel::kObjectCount; ++pad) {
      UpdatePadWidgets(pad);
    }
    UpdateTransferButton();
  }
  repaint();
}

void MainComponent::SelectSurface(int surface) {
  if (surface_tabs_.getCurrentTabIndex() != surface) {
    surface_tabs_.setCurrentTabIndex(surface, false);
  }
  if (surface_ == surface) {
    return;
  }
  surface_ = surface;
  settings_.getUserSettings()->setValue("uiSurface", surface);
  if (!ObjectOnSurface(selected_)) {
    SelectObject(SurfaceObject(0, 0));
  }
  for (int pad = 0; pad < KitModel::kObjectCount; ++pad) {
    UpdatePadWidgets(pad);
  }
  resized();  // the two surfaces have different grid shapes
  repaint();
}

bool MainComponent::ObjectOnSurface(int object) const {
  return (surface_ == 1) == KitModel::IsTrigger(object);
}

int MainComponent::SurfaceRows() const {
  return surface_ == 0 ? 3 : 2;
}

int MainComponent::SurfaceCols() const {
  return surface_ == 0 ? 3 : 4;
}

int MainComponent::SurfaceObject(int row, int col) const {
  const int cell = row * SurfaceCols() + col;
  return surface_ == 0 ? cell : KitModel::TriggerObject(cell);
}

juce::Rectangle<int> MainComponent::ObjectBounds(int object) const {
  const int cell =
      KitModel::IsTrigger(object) ? KitModel::TriggerOf(object) : object;
  return PadBounds(cell / SurfaceCols(), cell % SurfaceCols());
}

juce::String MainComponent::ObjectLabel(int object) {
  return KitModel::IsTrigger(object)
      ? "TRIG " + juce::String(KitModel::TriggerOf(object) + 1)
      : juce::String(object + 1);
}

juce::String MainComponent::ApplyBulkPadLink(
    const ops::PadLinkRequest& request) {
  document_.StashActiveKit();
  std::vector<KitData> kits;
  kits.reserve(DeviceModel::kKitCount);
  for (int i = 0; i < DeviceModel::kKitCount; ++i) {
    kits.push_back(device_.kit(i));
  }
  const auto plan = bulk::PlanPadLink(kits, request);
  if (plan.empty()) {
    return "nothing to change";
  }
  std::set<int> touched;
  for (const bulk::LinkChange& change : plan) {
    touched.insert(change.kit);
  }
  undo().beginNewTransaction("pad link change");
  undo().perform(new bulk::PadLinkAction(
      document_, device_, plan, request.group, request.direction));
  MarkEdited();
  UpdateSyncButton();
  commands_.commandStatusChanged();
  AppLog::Note("bulk pad link: " + juce::String(plan.size()) + " pad(s) across "
               + juce::String(touched.size()) + " kit(s)");
  return juce::String(plan.size()) + " pad(s) changed across "
      + juce::String(touched.size()) + " kit(s); sync to push";
}

juce::String MainComponent::ApplyBulkSetMode(
    const ops::SetModeRequest& request) {
  document_.StashActiveKit();
  std::vector<KitData> kits;
  kits.reserve(DeviceModel::kKitCount);
  for (int i = 0; i < DeviceModel::kKitCount; ++i) {
    kits.push_back(device_.kit(i));
  }
  const auto plan = bulk::PlanSetMode(kits, request);
  if (plan.empty()) {
    return "nothing to change";
  }
  std::set<int> touched;
  for (const ops::ModeChange& change : plan) {
    touched.insert(change.kit);
  }
  // One undoable transaction in the current kit's history, like any
  // other edit.
  undo().beginNewTransaction("layer mode change");
  undo().perform(
      new bulk::SetModeAction(document_, device_, plan, request.target));
  MarkEdited();
  UpdateSyncButton();
  commands_.commandStatusChanged();
  AppLog::Note("bulk edit applied: " + juce::String(plan.size())
               + " pad(s) across " + juce::String(touched.size()) + " kit(s)");
  return juce::String(plan.size()) + " pad(s) changed across "
      + juce::String(touched.size()) + " kit(s); sync to push";
}

void MainComponent::SetStatus(const juce::String& status) {
  status_bar_.setText(status, juce::dontSendNotification);
}

void MainComponent::MaybeShowDisclaimer() {
  if (settings_.getUserSettings()->getIntValue(kDisclaimerAcceptedKey, 0)
      >= kDisclaimerVersion) {
    return;
  }
  DisclaimerPanel::Show([this](bool accepted) {
    if (accepted) {
      settings_.getUserSettings()->setValue(kDisclaimerAcceptedKey,
                                            kDisclaimerVersion);
      settings_.getUserSettings()->saveIfNeeded();
    } else {
      juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }
  });
}

void MainComponent::InstallCli() {
  const auto bundle =
      juce::File::getSpecialLocation(juce::File::currentApplicationFile);
  const auto source = bundle.getChildFile("Contents/Helpers/spdutil");
  if (!source.existsAsFile()) {
    juce::AlertWindow::showMessageBoxAsync(
        juce::MessageBoxIconType::WarningIcon,
        "Install Command-Line Tool",
        "This build has no bundled spdutil (" + source.getFullPathName()
            + ").");
    return;
  }
  // Gatekeeper can run a quarantined app "translocated" from an ephemeral
  // read-only mount; a symlink there would dangle on the next launch.
  if (bundle.getFullPathName().contains("/AppTranslocation/")) {
    juce::AlertWindow::showMessageBoxAsync(
        juce::MessageBoxIconType::WarningIcon,
        "Install Command-Line Tool",
        "Move the app to Applications and relaunch it first.");
    return;
  }

  // The link points INTO the bundle, so replacing the app at the same
  // path updates the CLI too. Symlink directly if /usr/local/bin is
  // writable; otherwise run the same command through the macOS
  // administrator-password prompt, off the message thread so the prompt
  // doesn't freeze the UI.
  const juce::File link("/usr/local/bin/spdutil");
  const auto bin = link.getParentDirectory();
  if (bin.isDirectory() && bin.hasWriteAccess()) {
    link.deleteFile();
    if (source.createSymbolicLink(link, /*overwriteExisting=*/true)) {
      ReportCliInstall(true);
      return;
    }
  }
  const auto script = AdminInstallScript(InstallCliCommand(
      source.getFullPathName().toStdString(), "/usr/local/bin", "spdutil"));
  juce::Thread::launch([script] {
    juce::ChildProcess osascript;
    const bool ok =
        osascript.start(juce::StringArray {
            "/usr/bin/osascript", "-e", juce::String::fromUTF8(script.c_str())})
        && osascript.waitForProcessToFinish(120 * 1000)
        && osascript.getExitCode() == 0;
    juce::MessageManager::callAsync([ok] { ReportCliInstall(ok); });
  });
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
  // The opened document brings its own dirty-vs-base state; without
  // this the button only appears after the next edit or connection
  // change, hiding kits that already need a sync.
  UpdateSyncButton();
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
                                 const juce::String& message,
                                 std::function<void()> on_abort) {
  HideProgress();  // never stack two
  ProgressDialog* dialog = nullptr;
  progress_win_ =
      ProgressDialog::Show(title, message, &dialog, std::move(on_abort));
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
    AppLog::Note("load device state failed: " + error);
    juce::AlertWindow::showMessageBoxAsync(
        juce::MessageBoxIconType::WarningIcon, "Load Device State", error);
    return;
  }
  AppLog::Note("load device state ok: " + juce::String(kits.size()) + " kits, "
               + juce::String(pool.size()) + " pool records");
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
  for (int pad = 0; pad < KitModel::kObjectCount; ++pad) {
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
    SetStatus("kit samples already cached");
    juce::Timer::callAfterDelay(
        1500, [safe = juce::Component::SafePointer<MainComponent>(this)] {
          if (safe != nullptr) {
            safe->SetStatus({});
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
  SetStatus(juce::String::fromUTF8("downloading samples\xe2\x80\xa6"));
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
  for (int pad = 0; pad < KitModel::kObjectCount; ++pad) {
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

void MainComponent::AdoptKit(int index) {
  if (index == device_.current_kit() || index < 0
      || index >= DeviceModel::kKitCount) {
    return;
  }
  document_.SwitchKit(index);
  MarkEdited();  // persists the new current kit
  RefreshKitSelector();
  RefreshDocumentState();
  UpdateSyncButton();  // reflect the newly-active kit's dirty state
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
      // The select is fire-and-forget and closing a CDC port can kill an
      // in-flight write (tcdrain doesn't help; live-verified 2026-07-22).
      // The ping's round trip proves delivery before the port closes.
      dev.Ping();
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
  const bool was_connected = device_connected_.load();
  std::thread([safe, was_connected] {
    bool connected = false;
    int device_kit = 0;  // 1-based; 0 = not read
    juce::String firmware;  // version + build, for display
    juce::String version;  // bare, what the firmware gate compares
    try {
      const std::string path = device::FindDevicePort();
      connected = !path.empty();
      // On the way from disconnected to connected (app launch included),
      // learn the unit's active kit (so the app can open on it) and its
      // firmware (for the header of a bug report). Steady-state polls
      // stay a cheap ping.
      if (connected && !was_connected) {
        const std::unique_ptr<device::SerialPort> serial =
            device::PlatformPorts().Open(path);
        device::SpdsxDevice dev(serial.get());
        device_kit = dev.CurrentKit();
        version = juce::String(dev.FirmwareField(0));
        firmware = version;
        const std::string build = dev.FirmwareField(3);
        if (!build.empty()) {
          firmware << " (" << juce::String(build) << ")";
        }
      }
    } catch (const std::exception&) {
      connected = false;  // no node, or nothing answered
    }
    juce::MessageManager::callAsync([safe,
                                     connected,
                                     device_kit,
                                     firmware,
                                     version] {
      if (safe == nullptr) {
        return;
      }
      safe->conn_check_running_ = false;
      if (connected != safe->device_connected_.load()) {
        safe->device_connected_ = connected;
        safe->connection_dot_.SetConnected(connected);
        safe->commands_.commandStatusChanged();  // re-enable device menu items
        safe->UpdateTransferButton();
        safe->UpdateSyncButton();  // enable/disable with connection
        if (connected) {
          safe->device_firmware_ = firmware;
          safe->device_firmware_version_ = version;
          AppLog::Note("device connected, firmware "
                       + (firmware.isEmpty() ? "?" : firmware) + ", kit "
                       + juce::String(device_kit));
          if (device_kit > 0) {
            safe->AdoptKit(device_kit - 1);  // follow the unit, no echo back
          }
        } else {
          AppLog::Note("device disconnected");
        }
      }
    });
  }).detach();
}

void MainComponent::UpdateSyncButton() {
  const int dirty = static_cast<int>(document_.DirtyKits().size());
  const juce::String label = "Sync Changes with Device";
  const juce::String text =
      dirty > 1 ? label + " (" + juce::String(dirty) + " kits)" : label;
  const bool connected = DeviceConnected();
  const bool busy = device_fetching_;
  const bool firmware_ok =
      device_firmware_version_ == device::SpdsxDevice::kSupportedFirmware;
  sync_button_.setEnabled(connected && !busy && firmware_ok);
  // The tooltip answers the question the button raises: why it is
  // disabled, or what clicking it will do.
  juce::String tip;
  if (!connected) {
    tip = "No device is connected. Plug in the SPD-SX PRO to sync.";
  } else if (busy) {
    tip =
        "The device is busy with another operation. Sync when it "
        "finishes.";
  } else if (!firmware_ok) {
    tip = "This unit reports firmware "
        + (device_firmware_version_.isEmpty() ? juce::String("(unknown)")
                                              : device_firmware_version_)
        + "; writing has only been verified against "
        + device::SpdsxDevice::kSupportedFirmware
        + ", so syncing is disabled. Reading still works.";
  } else {
    tip = "Pushes " + juce::String(dirty) + " edited kit"
        + (dirty == 1 ? juce::String("'s") : juce::String("s'"))
        + " changes to the device, pulls changes made on the unit, and "
          "asks before overwriting anything changed on both sides.";
  }
  sync_button_.setTooltip(tip);
  const bool show = dirty > 0;
  if (text != sync_button_.getButtonText()
      || show != sync_button_.isVisible()) {
    sync_button_.setButtonText(text);
    sync_button_.setVisible(show);
    resized();  // reclaim/space the header for the (possibly wider) label
  }
}

void MainComponent::SyncChangesWithDevice() {
  if (device_fetching_.exchange(true)) {
    return;  // the port is busy (a fetch, a download, or another sync)
  }
  document_.StashActiveKit();
  const std::vector<int> dirty = document_.DirtyKits();
  if (dirty.empty() || !DeviceConnected()) {
    device_fetching_ = false;
    UpdateSyncButton();
    return;
  }
  // The write paths were only ever verified against one firmware, and a
  // wrong write lands in flash. The connection poll already read the
  // version, so refuse here rather than partway through the push.
  if (device_firmware_version_ != device::SpdsxDevice::kSupportedFirmware) {
    device_fetching_ = false;
    UpdateSyncButton();
    AppLog::Note("sync refused: firmware " + device_firmware_version_);
    juce::AlertWindow::showMessageBoxAsync(
        juce::MessageBoxIconType::WarningIcon,
        "Sync Changes with Device",
        "This unit reports firmware "
            + (device_firmware_version_.isEmpty()
                   ? juce::String("(unknown)")
                   : "\"" + device_firmware_version_ + "\"")
            + ", and writing has only been verified against \""
            + device::SpdsxDevice::kSupportedFirmware
            + juce::String::fromUTF8(
                "\". Your edits are safe in this document — reading the "
                "device still works, but nothing will be written to it."));
    return;
  }
  AppLog::Note("sync started: " + juce::String(dirty.size()) + " dirty kit(s)");
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
  UpdateSyncButton();  // disabled while the sync runs
  sync_phase_ = SyncPhase::kReading;
  auto blocks = std::make_shared<std::atomic<int>>(0);
  fetch_blocks_ = blocks;
  ShowProgress("Sync with Device",
               juce::String::fromUTF8("Reading device state\xe2\x80\xa6"));
  juce::Component::SafePointer<MainComponent> safe(this);
  std::thread([safe, need_pool, blocks] {
    std::vector<device::KitRecord> kits;
    std::vector<device::SampleRecord> pool;
    juce::String error;
    const auto count = [&blocks](const device::Bytes&) { ++*blocks; };
    try {
      const std::unique_ptr<device::SerialPort> serial =
          device::PlatformPorts().Open(device::FindDevicePort());
      device::SpdsxDevice dev(serial.get());
      kits = device::ParseKits(
          device::CleanBulkImage(dev.DumpBank(device::kBankKits, count)));
      if (kits.empty()) {
        error = "the device's kit data came back empty";
      } else if (need_pool) {
        pool = device::ParseSampleDir(
            device::CleanBulkImage(dev.DumpBank(device::kBankSamples, count)));
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
    SetStatus({});
    juce::AlertWindow::showMessageBoxAsync(
        juce::MessageBoxIconType::WarningIcon,
        "Sync Changes with Device",
        error);
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
  // The conflict dialog is itself modal; the progress bar would fight it,
  // so drop it while the user chooses (RunSyncPush brings it back).
  sync_phase_ = SyncPhase::kResolving;
  fetch_blocks_.reset();
  HideProgress();
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
          safe->SetStatus({});
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
    std::array<SyncResolution, KitModel::kObjectCount> objects;

    KitResolutions() { objects.fill(SyncResolution::kMine); }
  };

  std::map<int, KitResolutions> by_kit;
  for (size_t i = 0; i < sync_->conflicts.size() && i < resolutions.size();
       ++i) {
    const SyncConflict& conflict = sync_->conflicts[i];
    auto& kit = by_kit[conflict.kit];
    if (conflict.pad < 0) {
      kit.name = resolutions[i];
    } else {
      kit.objects[static_cast<size_t>(conflict.pad)] = resolutions[i];
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
    KitSyncPlan plan =
        PlanKitSync(current, base, theirs, res.name, res.objects);
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

  // Now pushing: re-show the bar (the conflict dialog, if any, hid it) and
  // arm the upload counter the timer reads for its "X of N" line.
  sync_phase_ = SyncPhase::kPushing;
  sync_pushed_ = 0;
  sync_upload_total_ = static_cast<int>(sync_->uploads.size());
  fetch_blocks_.reset();
  // The Abort button stops the push waiting on the flash commit (shared so
  // the worker reads it after this returns). It doesn't tear the device
  // down mid-flash — it just stops us waiting; the sync then reports as not
  // committed and nothing is recorded.
  sync_abort_ = std::make_shared<std::atomic<bool>>(false);
  ShowProgress("Sync with Device",
               juce::String::fromUTF8("Saving to device\xe2\x80\xa6"),
               [abort = sync_abort_,
                safe = Component::SafePointer<MainComponent>(this)] {
                 abort->store(true);
                 if (safe != nullptr && safe->progress_dialog_ != nullptr) {
                   safe->progress_dialog_->SetMessage(juce::String::fromUTF8(
                       "Aborting\xe2\x80\xa6 (the device may still finish)"));
                 }
               });
  juce::Component::SafePointer<MainComponent> safe(this);
  std::thread([safe,
               uploads = sync_->uploads,
               writes = std::move(writes),
               abort = sync_abort_] {
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
          dev,
          smp_uploads,
          writes,
          [safe, &uploads](const SmpUpload& done) {
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
          },
          /*pace_seconds=*/0.02,
          // The commit polls until the device reports done, however long
          // that takes; the user's Abort button is the only way to stop it.
          /*should_abort=*/[&abort] { return abort->load(); });
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
  sync_abort_.reset();
  sync_phase_ = SyncPhase::kNone;
  fetch_blocks_.reset();
  HideProgress();
  device_fetching_ = false;
  commands_.commandStatusChanged();
  UpdateSyncButton();
}

void MainComponent::OnWaveUploaded(UploadPlan plan,
                                   juce::MemoryBlock wav,
                                   int frames) {
  // ExecutePush reads every upload back before reporting it, so this one
  // is known to be on the device intact. Record it now — pool entry,
  // cached audio, and the layers that referenced the file repointed at
  // the new index — so a sync that dies later resumes from here rather
  // than uploading the same file to a second pool slot.
  device::SampleRecord record;
  record.index = plan.index;
  record.wavename = plan.wavename;
  record.filename = plan.filename;
  record.frames = static_cast<uint32_t>(frames);
  document_.AddPoolRecord(record);
  document_.StoreWaveAudio(plan.index, wav);
  document_.ReplaceFileLayers(plan.file, plan.index);
  if (sync_ != nullptr) {
    sync_->landed.push_back({std::move(plan), std::move(wav), frames});
  }
  ++sync_pushed_;  // advances the progress bar's "X of N" line
}

void MainComponent::FinishSyncPush(const juce::String& error, bool committed) {
  sync_phase_ = SyncPhase::kNone;
  HideProgress();
  SetStatus({});
  if (sync_ == nullptr) {
    CancelSync();
    return;
  }
  if (error.isNotEmpty() || !committed) {
    AppLog::Note(
        "sync push failed: "
        + (error.isNotEmpty() ? error : juce::String("commit unconfirmed")));
    juce::AlertWindow::showMessageBoxAsync(
        juce::MessageBoxIconType::WarningIcon,
        "Sync Changes with Device",
        error.isNotEmpty()
            ? error
            : juce::String("the device did not confirm the flash commit"));
    // Nothing advanced: the kits stay dirty (with their local files still
    // local, since nothing was recorded), so the next sync re-diffs against
    // a fresh device read and re-uploads. Any samples that did reach the
    // device are left as orphans rather than trusted.
    CancelSync();
    return;
  }
  AppLog::Note("sync push committed: " + juce::String(sync_->landed.size())
               + " upload(s) landed");
  // The uploads recorded themselves as they landed; the commit makes
  // them durable. Advance the kits: ApplySyncedKit installs the
  // device-wave layers the plan already substituted.
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
  sync_abort_.reset();
  device_fetching_ = false;
  commands_.commandStatusChanged();
  RefreshKitSelector();
  RefreshDocumentState();
  UpdateSyncButton();
  SetStatus(skipped ? "synced (skipped conflicts remain)"
                    : "synced with device");
  juce::Timer::callAfterDelay(
      2000, [safe = juce::Component::SafePointer<MainComponent>(this)] {
        if (safe != nullptr) {
          safe->SetStatus({});
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
  for (int pad = 0; pad < KitModel::kObjectCount; ++pad) {
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
  for (int pad = 0; pad < KitModel::kObjectCount; ++pad) {
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
  SetStatus({});
  commands_.commandStatusChanged();
  // Clear indicators and settle every affected slot to its final state
  // (playable if cached, "on device" if it was skipped or failed).
  download_indices_.clear();
  download_current_.reset();
  download_permille_.reset();
  for (int pad = 0; pad < KitModel::kObjectCount; ++pad) {
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
    SetStatus(note);
    juce::Timer::callAfterDelay(
        3000, [safe = juce::Component::SafePointer<MainComponent>(this)] {
          if (safe != nullptr) {
            safe->SetStatus({});
          }
        });
  }
}

void MainComponent::RefreshDeviceSamples() {
  device_samples_.Refresh();
  // Re-resolve device-wave slot displays against the (new) pool.
  for (int pad = 0; pad < KitModel::kObjectCount; ++pad) {
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
  UpdateSyncButton();
  RefreshDocumentState();
}

// The model is the source of truth: engine and slot display sync to it
// here, whether the change came from a user gesture, undo, or a loaded
// kit file. The pad-shaped model maps to the flat slot components as
// idx = pad * 2 + layer.
void MainComponent::SampleChanged(int pad, int layer) {
  MarkEdited();
  UpdateSyncButton();
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

  const auto bar = getLocalBounds().removeFromTop(kGlobalBarHeight);
  g.setColour(juce::Colour(0xff0d1117));
  g.fillRect(bar);
  g.setColour(juce::Colour(0xff222831));
  g.fillRect(bar.withTop(bar.getBottom() - 1));
  if (!on_edit_tab_) {
    return;  // the Bulk Edit panel paints itself
  }

  const auto now = juce::Time::getMillisecondCounter();
  for (int r = 0; r < SurfaceRows(); ++r) {
    for (int c = 0; c < SurfaceCols(); ++c) {
      auto pad = PadBounds(r, c);
      const int object = SurfaceObject(r, c);
      g.setColour(kPadBg);
      g.fillRoundedRectangle(pad.toFloat(), 10.0f);
      // A hit washes the pad in the velocity colour and fades out.
      const auto idx = static_cast<size_t>(object);
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
      if (object == selected_) {
        // The selection ring: the Properties tab is showing this object.
        g.setColour(juce::Colour(0xff58a6ff).withAlpha(0.85f));
        g.drawRoundedRectangle(pad.toFloat().reduced(1.0f), 10.0f, 1.5f);
      }
      g.setColour(kPadLabel);
      g.setFont(juce::Font(juce::FontOptions(13.0f)).boldened());
      g.drawText(
          ObjectLabel(object),
          pad.reduced(kPadPadding + 2, kPadPadding).removeFromTop(kPadHeader),
          juce::Justification::centredLeft);
    }
  }
}

juce::Rectangle<int> MainComponent::GridArea() const {
  auto area = getLocalBounds();
  area.removeFromTop(kGlobalBarHeight + kTabBarHeight + kHeaderHeight
                     + kSurfaceBarHeight);
  if (browser_visible_) {
    area.removeFromLeft(browser_width_);
  }
  return area;
}

juce::Rectangle<int> MainComponent::PadBounds(int row, int col) const {
  const auto area = GridArea();
  const int cols = SurfaceCols();
  const int rows = SurfaceRows();
  const int cell_w =
      (area.getWidth() - 2 * kGridPadding - (cols - 1) * kGridSpacing) / cols;
  const int cell_h =
      (area.getHeight() - 2 * kGridPadding - (rows - 1) * kGridSpacing) / rows;
  return {area.getX() + kGridPadding + col * (cell_w + kGridSpacing),
          area.getY() + kGridPadding + row * (cell_h + kGridSpacing),
          cell_w,
          cell_h};
}

void MainComponent::resized() {
  auto bounds = getLocalBounds();

  // The global bar: connection dot, then the sync button at the right,
  // status text in between. Visible on both tabs.
  auto global_bar = bounds.removeFromTop(kGlobalBarHeight);
  connection_dot_.setBounds(global_bar.removeFromLeft(28));
  global_bar.removeFromRight(10);
  if (sync_button_.isVisible()) {
    // Wide enough for the "(N kits)" suffix when several kits are dirty.
    const int w = juce::jmax(180, sync_button_.getBestWidthForHeight(26));
    sync_button_.setBounds(
        global_bar.removeFromRight(w).withSizeKeepingCentre(w, 26));
    global_bar.removeFromRight(10);
  }
  status_bar_.setBounds(global_bar);

  tabs_.setBounds(bounds.removeFromTop(kTabBarHeight));
  bulk_panel_.setBounds(bounds);

  // Everything below lays out the Edit Kits tab within `bounds`.
  auto header = bounds.removeFromTop(kHeaderHeight);
  header.removeFromLeft(10);
  header.removeFromRight(10);
  if (transfer_button_.isVisible()) {
    header.removeFromRight(8);
    transfer_button_.setBounds(
        header.removeFromRight(120).withSizeKeepingCentre(120, 26));
  }
  // The kit chooser owns what's LEFT of the header — `header` has been
  // whittled down by everything laid out above, so sizing within it is
  // what keeps the chooser clear of the transfer button.
  kit_chooser_.setBounds(header.withSizeKeepingCentre(
      juce::jmin(500, header.getWidth() - 16), 28));
  const int panel_top = kGlobalBarHeight + kTabBarHeight + kHeaderHeight;
  panel_tabs_.setBounds(0, panel_top, browser_width_, getHeight() - panel_top);
  panel_divider_.setBounds(
      browser_width_ - 3, panel_top, 6, getHeight() - panel_top);
  panel_divider_.setVisible(browser_visible_ && on_edit_tab_);
  panel_divider_.toFront(false);
  // The Pads/Triggers surface switch sits in its own strip between the
  // kit header and the grid, spanning the grid's width.
  auto surface_strip = bounds.removeFromTop(kSurfaceBarHeight);
  if (browser_visible_) {
    surface_strip.removeFromLeft(browser_width_);
  }
  surface_tabs_.setBounds(
      surface_strip.withTrimmedLeft(kGridPadding).withTrimmedTop(4));
  for (int r = 0; r < SurfaceRows(); ++r) {
    for (int c = 0; c < SurfaceCols(); ++c) {
      auto inner = PadBounds(r, c).reduced(kPadPadding);
      const int pad = SurfaceObject(r, c);
      // The header row holds only the painted pad number; the layer
      // controls live in the Properties tab.
      inner.removeFromTop(kPadHeader);
      const int slot_h = (inner.getHeight() - kSlotSpacing) / 2;
      const auto slot = static_cast<size_t>(pad * 2);
      slots_[slot]->setBounds(inner.removeFromTop(slot_h));
      inner.removeFromTop(kSlotSpacing);
      slots_[slot + 1]->setBounds(inner.removeFromTop(slot_h));
    }
  }
}

bool MainComponent::keyPressed(const juce::KeyPress& key) {
  // Command-key chords are menu shortcuts (⌘1/⌘2 pick windows, ⌘S syncs).
  // Every trigger key here is unmodified, so let chords fall through to
  // the command manager's key mappings instead of eating ⌘1 as a pad hit.
  if (key.getModifiers().isCommandDown()) {
    return false;
  }
  if (!on_edit_tab_) {
    return false;  // the grid is not on screen; nothing here to trigger
  }
  // Delete/Backspace clears the layer under the cursor (same edit as the
  // slot's right-click Clear). The 30 Hz hover poll keeps hovered_ current.
  if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) {
    if (hovered_ >= 0 && hovered_ < kSlotCount
        && slots_[static_cast<size_t>(hovered_)]->has_sample()) {
      undo().beginNewTransaction("clear layer");
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
    // The digits hit whatever the surface shows: pads 1-9, or triggers
    // 1-8 (the 9 key idles there).
    const int object = surface_ == 0    ? pad
        : pad < KitModel::kTriggerCount ? KitModel::TriggerObject(pad)
                                        : -1;
    auto& held = held_pad_keys_[static_cast<size_t>(pad)];
    if (!held && object >= 0) {
      held = true;
      SelectObject(object);
      TriggerPad(object, kKeyVelocity, pedal_down);
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
  const int velocity = kKeyVelocity;
  for (int pad = 0; pad < KitModel::kObjectCount; ++pad) {
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
  if (!on_edit_tab_) {
    return;
  }
  const auto pos = event.getPosition();
  if (const int pad = PadAt(pos); pad >= 0) {
    SelectObject(pad);
    TriggerPad(pad, VelocityForPointInPad(pad, pos), HiHatPedalDown());
  }
}

int MainComponent::PadAt(juce::Point<int> point) const {
  for (int r = 0; r < SurfaceRows(); ++r) {
    for (int c = 0; c < SurfaceCols(); ++c) {
      if (PadBounds(r, c).contains(point)) {
        return SurfaceObject(r, c);
      }
    }
  }
  return -1;
}

int MainComponent::VelocityForPointInPad(int pad,
                                         juce::Point<int> point) const {
  const auto bounds = ObjectBounds(pad);
  const float height_fraction = static_cast<float>(bounds.getBottom() - point.y)
      / static_cast<float>(juce::jmax(1, bounds.getHeight()));
  return juce::jlimit(
      1, 127, static_cast<int>(std::lround(height_fraction * 127.0f)));
}

void MainComponent::TriggerPad(int pad, int velocity, bool pedal_down) {
  if (pad < 0 || pad >= KitModel::kObjectCount) {
    return;
  }
  const auto p = static_cast<size_t>(pad);
  // Flash the pad in the velocity colour; the timer fades it out.
  pad_flash_velocity_[p] = velocity;
  pad_flash_ms_[p] = juce::Time::getMillisecondCounter();
  if (ObjectOnSurface(pad)) {
    repaint(ObjectBounds(pad));
  }
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

MainComponent::PropertiesTab::PropertiesTab() {
  viewport.setViewedComponent(&panel, false);
  viewport.setScrollBarsShown(true, false);
  addAndMakeVisible(viewport);
}

void MainComponent::PropertiesTab::resized() {
  viewport.setBounds(getLocalBounds());
  // Header bands span the tab's full width; the panel resizes to fill
  // whatever the viewport can show.
  panel.set_content_width(viewport.getMaximumVisibleWidth());
}

void MainComponent::SelectObject(int object) {
  if (object < 0 || object >= KitModel::kObjectCount || object == selected_) {
    return;
  }
  const int previous = selected_;
  selected_ = object;
  RefreshProperties();
  if (ObjectOnSurface(previous)) {
    repaint(ObjectBounds(previous));
  }
  if (ObjectOnSurface(selected_)) {
    repaint(ObjectBounds(selected_));
  }
}

void MainComponent::RefreshProperties() {
  properties_tab_.panel.set_title(
      KitModel::IsTrigger(selected_)
          ? "Trigger " + juce::String(KitModel::TriggerOf(selected_) + 1)
          : "Pad " + juce::String(selected_ + 1));
  properties_tab_.panel.SetParams(model_.params(selected_));
}

void MainComponent::UpdatePadWidgets(int pad) {
  // The layer controls live in the Properties tab now; the grid's only
  // per-object widgets are the two slots, visible on their surface.
  const auto p = static_cast<size_t>(pad);
  const bool shown = ObjectOnSurface(pad);
  slots_[p * 2]->setVisible(shown);
  slots_[p * 2 + 1]->setVisible(shown);
}

void MainComponent::PadParamsChanged(int pad) {
  MarkEdited();
  UpdateSyncButton();
  UpdatePadWidgets(pad);
  // Keep the Properties tab honest when undo/redo (or anything else)
  // changes the object underneath it.
  if (pad == selected_) {
    RefreshProperties();
  }
}

void MainComponent::MoveSample(int from, int to, bool copy) {
  if (from == to || from < 0 || to < 0) {
    return;
  }
  const LayerSample sample = model_.sample(from / KitModel::kLayersPerPad,
                                           from % KitModel::kLayersPerPad);
  undo().beginNewTransaction(copy ? "duplicate sample" : "move sample");
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
  undo().beginNewTransaction(copy ? "duplicate pad" : "move pad");
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

  // Live detail in the progress dialog. Load Device State and the sync's
  // read phase both stream blocks (fetch_blocks_); the sync's push phase
  // reports its upload count instead.
  if (progress_dialog_ != nullptr) {
    if (sync_phase_ == SyncPhase::kPushing) {
      progress_dialog_->SetMessage(
          sync_upload_total_ > 0 && sync_pushed_ < sync_upload_total_
              ? juce::String::fromUTF8("Uploading samples\xe2\x80\xa6\n")
                  + juce::String(sync_pushed_) + " of "
                  + juce::String(sync_upload_total_)
              : juce::String::fromUTF8("Writing kit changes\xe2\x80\xa6"));
    } else if (device_fetching_ && fetch_blocks_ != nullptr) {
      const int n = fetch_blocks_->load();
      progress_dialog_->SetMessage(
          juce::String::fromUTF8("Reading device state\xe2\x80\xa6\n")
          + juce::String(n) + (n == 1 ? " block" : " blocks"));
    }
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
  for (int pad = 0; pad < KitModel::kObjectCount; ++pad) {
    const auto p = static_cast<size_t>(pad);
    if (pad_flash_velocity_[p] > 0) {
      if (now - pad_flash_ms_[p] >= kPadFlashMs) {
        pad_flash_velocity_[p] = 0;
      }
      if (ObjectOnSurface(pad)) {
        repaint(ObjectBounds(pad));
      }
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
