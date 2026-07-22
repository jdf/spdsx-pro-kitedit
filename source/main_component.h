// The main window content: a 3x3 grid of pads labeled 1-9, each with a
// top and a bottom sample slot (18 slots, indexed (row * 3 + col) * 2,
// +1 for the bottom).
#ifndef SPDSX_PATCHEDIT_SOURCE_MAIN_COMPONENT_H_
#define SPDSX_PATCHEDIT_SOURCE_MAIN_COMPONENT_H_

#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "audio.h"
#include "device_document.h"
#include "device_model.h"
#include "device_samples.h"
#include "device_sync.h"
#include "kit_chooser.h"
#include "kit_model.h"
#include "pad_settings.h"
#include "progress_dialog.h"
#include "sample_browser.h"
#include "sample_slot.h"

namespace spdsx {

// A small header status light: green when the device is connected, gray
// when not. Set from the connection poller.
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

class MainComponent
    : public juce::Component
    , public juce::ApplicationCommandTarget
    , public juce::DragAndDropContainer
    , private juce::Timer
    , private juce::MidiInputCallback
    , private KitModel::Listener {
public:
  static constexpr int kSlotCount = KitModel::kSlotCount;

  explicit MainComponent(juce::ApplicationCommandManager& commands);
  ~MainComponent() override;

  // Assigns a sample to a slot, via the model. Shared by --load,
  // drag-and-drop, and the file dialog.
  void LoadSample(int idx, const juce::File& file);

  // The document, for app-level flows (quit interception).
  DeviceDocument& document() { return document_; }

  // Opens the most recently used device document, if it still exists;
  // otherwise stays untitled. A device document is a staging area for
  // one physical device, so launching back into it is the common case.
  void OpenLastDocument();

  // Opens a specific device document and refreshes the UI around it.
  // Shared by File > Open and Finder (double-clicking a .spdsx).
  void OpenDocument(const juce::File& file);

  void paint(juce::Graphics& g) override;
  void resized() override;
  bool keyPressed(const juce::KeyPress& key) override;
  bool keyStateChanged(bool is_key_down) override;
  void mouseDown(const juce::MouseEvent& event) override;

  ApplicationCommandTarget* getNextCommandTarget() override;
  void getAllCommands(juce::Array<juce::CommandID>& ids) override;
  void getCommandInfo(juce::CommandID id,
                      juce::ApplicationCommandInfo& info) override;
  bool perform(const InvocationInfo& info) override;

private:
  void SampleChanged(int pad, int layer) override;
  void KitNameChanged() override;
  void PadParamsChanged(int pad) override;

  // Sets the settings storage parameters; must run before any member
  // that reads settings in its constructor.
  juce::ApplicationProperties& ConfigureSettings();

  // Opens every available MIDI input so a physical pad hit triggers its
  // grid pad. The SPD-SX PRO sends note-on on channel 10, pads 1-9 on
  // notes 60-68.
  void OpenMidiInputs();
  void handleIncomingMidiMessage(juce::MidiInput* source,
                                 const juce::MidiMessage& message) override;

  void ApplyTransportAction(int idx, TransportAction action);
  // A velocity-aware hit on a whole pad (MIDI note-on, mouse/space at
  // cursor-height velocity, or keys 1-9 at the header velocity): plays
  // the layers the pad's layer mode selects, at the gains it computes,
  // flashes the pad in the velocity colour, and tints each sounding
  // layer with its adjusted velocity. pedal_down is the hi-hat pedal.
  void TriggerPad(int pad, int velocity, bool pedal_down);
  // The pad containing a point (in our coordinates), or -1.
  int PadAt(juce::Point<int> point) const;
  // Mouse-as-velocity: the cursor's height within the pad, bottom = 1
  // (softest) up to top = 127 (hardest).
  int VelocityForPointInPad(int pad, juce::Point<int> point) const;
  // The H key is the hi-hat pedal. A press is a foot-close on every
  // HI-HAT pad: it cuts the open layer and sounds the closed one (the
  // "chick"); a release is silent. While held, hits play closed.
  void SetHiHatKeyDown(bool down);
  // Pedal state from any source: the H key, or MIDI CC4 >= 64.
  bool HiHatPedalDown() const;
  // Pushes the pad's layer widgets into the model as one undo step.
  void ApplyLayerParams(int pad);
  // Syncs the pad's layer widgets (and their visibility) from the model.
  void UpdatePadWidgets(int pad);
  // The pad's "..." settings panel (a CallOutBox): dynamics on/off,
  // dynamics curve, fixed velocity, trigger reserve — the shared
  // hit-response properties that don't earn permanent header space.
  void ShowPadSettings(int pad);
  // Moves (or, when copy=true, duplicates) a slot's sample to another
  // slot as a single undoable action.
  void MoveSample(int from, int to, bool copy);
  // Same, but both layers of a whole pad (command-drag).
  void MovePad(int from_pad, int to_pad, bool copy);
  // Highlights the drag drop target: the hovered slot, plus its pad
  // sibling when whole_pad is set. idx<0 clears.
  void SetDragTarget(int idx, bool whole_pad);
  void SetBrowserVisible(bool visible);
  void RefreshDocumentState();
  // File > Load Device State: after a dire confirmation, reads kits +
  // wave pool from the hardware on a worker thread and REPLACES the
  // whole document (not undoable). FinishDeviceFetch lands the result
  // back on the message thread.
  void LoadDeviceState();
  void StartDeviceStateFetch();
  // Opens/closes the modal progress dialog for a long device operation.
  void ShowProgress(const juce::String& title, const juce::String& message);
  void HideProgress();
  void FinishDeviceFetch(std::vector<device::KitRecord> kits,
                         std::vector<device::SampleRecord> pool,
                         const juce::String& error);
  // Repopulates the device tab and re-resolves device-wave slots after
  // the pool changes (device fetch, open, new).
  void RefreshDeviceSamples();
  // Syncs one slot's engine + display from the model, without marking
  // the document edited.
  void SyncSlotFromModel(int pad, int layer);
  // Loads an audio file into a slot's engine channel + spectrogram
  // under a display name (shared by local files and cached device
  // waves).
  void LoadAudioIntoSlot(int idx,
                         const juce::File& file,
                         const juce::String& display_name);
  // File > Download Kit Samples: fetches the active kit's uncached
  // device waves into the bundle cache on a worker, refreshing slots as
  // each lands.
  void DownloadKitSamples();
  // Message-thread landing for a downloaded wave: stores its blob in the
  // document, then refreshes the slots that use it.
  void OnWaveDownloaded(int sample_index, const device::Bytes& wav);
  void OnWaveCached(int sample_index);
  void FinishKitSampleDownload(const juce::String& error, int done, int failed);
  // Pushes the current download progress into each affected slot's
  // indicator; called from the 30Hz timer while a fetch runs.
  void UpdateDownloadIndicators();
  // Device waves in the active kit whose audio isn't cached yet.
  int UncachedDeviceWaveCount() const;
  // Shows/hides + labels the header transfer button from that count.
  void UpdateTransferButton();
  // Moves the connected unit's playback kit to match the app's active
  // kit (a DT1 kit-select). No-op when no device is connected or a
  // larger device op holds the port. Rapid switches coalesce to the
  // latest kit, and only one worker runs at a time.
  void SyncDeviceKit();
  // Periodically probes for the device on a worker thread (throttled),
  // updating device_connected_ + the header dot + command enablement.
  // Skipped while a device operation holds the port.
  void PollConnection();
  // Shows/enables the header "Save Changes to Device" button: visible
  // when any kit differs from the last-synced base snapshot, enabled
  // when a device is connected and no sync is already running.
  void UpdateSaveButton();
  // The three-way sync (jdf's Phase 3 spec): a fresh device read on a
  // worker ("theirs"), a per-pad current/base/theirs diff, a resolution
  // dialog for conflicts, then uploads + kit writes + one flash commit
  // on a worker, and finally the base snapshots advance.
  void SaveChangesToDevice();
  // Message-thread landing for the fresh device read; builds the
  // conflict list and either runs the push or asks the user first.
  void FinishSyncFetch(std::vector<device::KitRecord> kits,
                       std::vector<device::SampleRecord> pool,
                       const juce::String& error);
  // Plans every kit with the user's conflict resolutions and starts the
  // push worker (converting + uploading local files first).
  void RunSyncPush(const std::vector<SyncResolution>& resolutions);
  // Abandons an in-flight sync (dialog cancelled or fetch failed).
  void CancelSync();
  // Message-thread landing for one durable upload: records the new pool
  // wave, caches its audio, and swaps the file layers to the new index —
  // kept even if a later push step fails, so a retry won't re-upload.
  void OnWaveUploaded(UploadPlan plan, juce::MemoryBlock wav, int frames);
  // Message-thread landing for the push: on success lands every kit's
  // merged content, advances base, and persists both snapshots.
  void FinishSyncPush(const juce::String& error, bool committed);

  // True only while the device answered the most recent probe. Actions
  // that need the hardware (Load Device State, Download Kit Samples) are
  // disabled otherwise.
  bool DeviceConnected() const { return device_connected_.load(); }

  // Every model mutation lands here: stamps the edit time so the timer
  // can autosave once the edits go quiet.
  void MarkEdited();
  // Syncs the header's kit chooser with the active kit and its name.
  void RefreshKitSelector();
  void timerCallback() override;
  juce::Rectangle<int> GridArea() const;
  juce::Rectangle<int> PadBounds(int row, int col) const;

  // The current kit's undo history. Histories are per-kit (created
  // lazily) so switching kits doesn't destroy them; document-level
  // events (open/new/import) clear all via on_history_reset.
  juce::UndoManager& undo();

  juce::ApplicationCommandManager& commands_;
  KitModel model_;
  DeviceModel device_;
  std::array<std::unique_ptr<juce::UndoManager>, DeviceModel::kKitCount> undos_;
  juce::ApplicationProperties settings_;
  DeviceDocument document_ {device_, model_, settings_};
  AudioEngine engine_ {kSlotCount};
  std::array<std::unique_ptr<SampleSlot>, kSlotCount> slots_;
  // Per-pad layer controls, living in each pad's header row.
  std::array<std::unique_ptr<juce::ComboBox>, KitModel::kPadCount> mode_boxes_;
  std::array<std::unique_ptr<juce::Slider>, KitModel::kPadCount>
      fade_point_sliders_;
  std::array<std::unique_ptr<juce::Slider>, KitModel::kPadCount>
      fade_end_sliders_;
  std::array<std::unique_ptr<juce::TextButton>, KitModel::kPadCount>
      pad_menu_buttons_;
  // The open "..." settings panel, if any (the CallOutBox owns it; the
  // SafePointer nulls itself when the box closes), and which pad it
  // is editing.
  juce::Component::SafePointer<PadSettingsPanel> pad_settings_panel_;
  int pad_settings_pad_ = -1;
  // ALTERNATE mode's per-pad flip-flop (false = layer A fires next);
  // runtime state, deliberately not persisted.
  std::array<bool, KitModel::kPadCount> alternate_flip_ {};
  // Velocity-coloured pad flash: velocity of the last hit (0 = idle)
  // and when it landed; the timer fades and expires it.
  std::array<int, KitModel::kPadCount> pad_flash_velocity_ {};
  std::array<juce::uint32, KitModel::kPadCount> pad_flash_ms_ {};
  // Last seen hi-hat pedal position (MIDI CC4), written on the MIDI
  // thread; >= 64 means pedal down (closed).
  std::atomic<int> hihat_cc_ {0};
  // Whether the H key (the keyboard's hi-hat pedal) is held.
  bool hihat_key_down_ = false;
  // Trigger keys currently held, so OS auto-repeat doesn't machine-gun
  // pads: keyPressed consumes repeats while a key is marked held, and
  // keyStateChanged clears the mark on release.
  std::array<bool, KitModel::kPadCount> held_pad_keys_ {};
  bool held_space_ = false;
  // Velocity used by keyboard pad triggers (keys 1-9).
  juce::Slider velocity_slider_;
  juce::Label velocity_caption_;
  // Shown in the header when the active kit has uncached device waves.
  juce::TextButton transfer_button_ {"Download samples"};
  // Header connection light + its polling state.
  ConnectionDot connection_dot_;
  std::atomic<bool> device_connected_ {false};
  std::atomic<bool> conn_check_running_ {false};
  juce::uint32 last_conn_check_ms_ = 0;
  // Follow-the-app kit selection on the unit: the pending 1-based kit
  // (shared so a detached worker can read it after switches), and a
  // single-runner flag. shared_ptr so the worker never touches a
  // destroyed component.
  std::shared_ptr<std::atomic<int>> pending_select_kit_ =
      std::make_shared<std::atomic<int>>(0);
  std::shared_ptr<std::atomic<bool>> kit_select_running_ =
      std::make_shared<std::atomic<bool>>(false);
  // "Save Changes to Device" header button. Dirtiness is computed, not
  // tracked: a kit is dirty when its content differs from the document's
  // base snapshot (DeviceDocument::DirtyKits). model_loading_ marks kit
  // loads, whose change listeners are not user edits.
  juce::TextButton save_button_ {
      juce::String::fromUTF8("Save Changes to Device")};
  bool model_loading_ = false;

  // An in-flight "Save Changes to Device": the fresh device read, the
  // conflicts awaiting resolution, and the per-kit plans being pushed.
  // Present only while the sync runs; device_fetching_ stays true for
  // its whole span so nothing else opens the port.
  struct SyncSession {
    std::vector<KitData> theirs;  // all 200 kits, from the fresh read
    std::vector<SyncConflict> conflicts;
    std::vector<std::pair<int, KitSyncPlan>> plans;
    std::vector<UploadPlan> uploads;
    bool pulled = false;  // device-side changes landed locally
  };

  std::unique_ptr<SyncSession> sync_;
  // The unified kit control: arrows, kit menu, in-place rename.
  KitChooser kit_chooser_ {DeviceModel::kKitCount};
  std::unique_ptr<juce::FileChooser> import_chooser_;
  std::unique_ptr<juce::FileChooser> open_chooser_;
  // Device-fetch state: the flag gates the command, the counter is
  // shared with the worker thread for the progress line.
  std::atomic<bool> device_fetching_ {false};
  std::shared_ptr<std::atomic<int>> fetch_blocks_;
  // Modal progress dialog for the long serial operations (Load Device
  // State). SafePointers so a programmatic close can't dangle.
  juce::Component::SafePointer<juce::DialogWindow> progress_win_;
  juce::Component::SafePointer<ProgressDialog> progress_dialog_;
  // Kit-sample download: the pool indices this run covers, plus the
  // wave currently transferring and its permille (published by the
  // worker, read by the timer to animate slot indicators).
  std::vector<int> download_indices_;
  std::shared_ptr<std::atomic<int>> download_current_;
  std::shared_ptr<std::atomic<int>> download_permille_;
  SampleBrowser browser_;
  DeviceSamplePanel device_samples_ {device_, settings_};
  // The left panel: "Files" (the sample browser) and "Device" (the
  // wave pool) tabs.
  juce::TabbedComponent panel_tabs_ {juce::TabbedButtonBar::TabsAtTop};
  std::vector<std::unique_ptr<juce::MidiInput>> midi_inputs_;
  bool browser_visible_ = true;
  int hovered_ = -1;
  bool could_undo_ = false;
  bool could_redo_ = false;
  // When the last edit landed; the timer autosaves after a quiet spell.
  juce::uint32 last_edit_ms_ = 0;
};

}  // namespace spdsx

#endif  // SPDSX_PATCHEDIT_SOURCE_MAIN_COMPONENT_H_
