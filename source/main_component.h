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
#include "bulk_edit_panel.h"
#include "connection_dot.h"
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

  // The app's settings file, for state owned outside this component
  // (the main window's saved size and position).
  juce::PropertiesFile* user_settings() { return settings_.getUserSettings(); }

  // Opens the most recently used device document, if it still exists;
  // otherwise stays untitled. A device document is a staging area for
  // one physical device, so launching back into it is the common case.
  void OpenLastDocument();

  // Opens a specific device document and refreshes the UI around it.
  // Shared by File > Open and Finder (double-clicking a .spdsx).
  void OpenDocument(const juce::File& file);

  // View menu: which tab fills the window — the kit editor, or Bulk
  // Edit. 0 = Edit Kits, 1 = Bulk Edit.
  void SelectTab(int index);

  // Which editing surface the kit editor shows: 0 = the nine pads,
  // 1 = the eight external trigger inputs.
  void SelectSurface(int surface);

  // A line of device/sync narration in the strip along the bottom of the
  // window — where it stays visible whatever panel or tab is showing.
  // Empty clears it.
  void SetStatus(const juce::String& status);

  // Shows the first-launch clickwrap risk acknowledgment unless this
  // settings profile already accepted the current version; declining
  // quits the app.
  void MaybeShowDisclaimer();

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
  // Syncs an object's grid widgets (slot visibility) with the surface.
  void UpdatePadWidgets(int pad);
  // Makes the object the ONLY selected one: the Properties tab follows
  // it and its tile wears the selection ring. Clicking a pad (tile or
  // slot) or hitting it with a digit key selects it; MIDI hits don't.
  void SelectObject(int object);
  // Multi-selection: ⌘-click toggles an object in the selection (the
  // last one can't leave), ⇧-click extends it from the anchor to the
  // clicked object. The Properties panel edits every selected object.
  void ToggleSelected(int object);
  void ExtendSelectionTo(int object);
  bool IsSelected(int object) const;
  // The selected objects, anchor first, the rest in grid order.
  std::vector<int> SelectionObjects() const;
  // Lands a new selection set + anchor: repaints the tiles that changed
  // and refreshes the Properties panel.
  void ApplySelection(const std::array<bool, KitModel::kObjectCount>& next,
                      int anchor);
  // Syncs the Properties tab's heading and controls from the model.
  void RefreshProperties();
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
  // A non-empty on_abort adds an Abort button that calls it.
  void ShowProgress(const juce::String& title,
                    const juce::String& message,
                    std::function<void()> on_abort = {});
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
  // Switches the app's active kit (0-based index): document, chooser,
  // header state. The device is NOT told — the kit chooser follows this
  // with SyncDeviceKit; a device-initiated adoption must not echo back.
  void AdoptKit(int index);
  // Periodically probes for the device on a worker thread (throttled),
  // updating device_connected_ + the header dot + command enablement.
  // Skipped while a device operation holds the port. Every poll reads
  // the device's ACTIVE kit and adopts a change (so the app opens on
  // what the unit shows, and follows kit switches made on the
  // hardware); the transition to connected also reads the firmware.
  void PollConnection();
  // Shows/enables the header "Sync Changes with Device" button: visible
  // when any kit differs from the last-synced base snapshot, enabled
  // when a device is connected and no sync is already running.
  void UpdateSyncButton();
  // The three-way sync (jdf's Phase 3 spec): a fresh device read on a
  // worker ("theirs"), a per-pad current/base/theirs diff, a resolution
  // dialog for conflicts, then uploads + kit writes + one flash commit
  // on a worker, and finally the base snapshots advance.
  void SyncChangesWithDevice();
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
  // The app menu's Install Command-Line Tool: symlinks the bundled
  // spdutil into /usr/local/bin, asking for an administrator password if
  // needed.
  void InstallCli();
  void timerCallback() override;
  juce::Rectangle<int> GridArea() const;
  // The pads/triggers section's inner frame; the Properties view aligns
  // its top and bottom with it.
  juce::Rectangle<int> SurfaceOutline() const;
  juce::Rectangle<int> PadBounds(int row, int col) const;
  // Whether the object (pad or trigger) lives on the active surface.
  bool ObjectOnSurface(int object) const;
  // The active surface's grid shape: 3x3 pads, or 2x4 triggers.
  int SurfaceRows() const;
  int SurfaceCols() const;
  // The object at a grid cell of the active surface, and its bounds.
  int SurfaceObject(int row, int col) const;
  juce::Rectangle<int> ObjectBounds(int object) const;
  // The label painted in an object's corner ("3", or "TRIG 3").
  static juce::String ObjectLabel(int object);

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

  // The left panel's Properties tab: the selected object's settings
  // panel (title strip included), scrollable when it outgrows the tab.
  class PropertiesTab : public juce::Component {
  public:
    PropertiesTab();
    void resized() override;

    juce::Viewport viewport;
    PadSettingsPanel panel;
  };

  PropertiesTab properties_tab_;

  // The draggable divider on the left panel's right edge.
  class PanelDivider : public juce::Component {
  public:
    PanelDivider() { setMouseCursor(juce::MouseCursor::LeftRightResizeCursor); }

    std::function<int()> width_at_drag_start;
    std::function<void(int)> on_drag;  // the new panel width

    void mouseDown(const juce::MouseEvent&) override {
      start_width_ = width_at_drag_start ? width_at_drag_start() : 0;
    }

    void mouseDrag(const juce::MouseEvent& event) override {
      if (on_drag) {
        on_drag(start_width_ + event.getDistanceFromDragStartX());
      }
    }

    void paint(juce::Graphics& g) override {
      g.setColour(juce::Colour(0xff222831));
      g.fillRect(getLocalBounds().withSizeKeepingCentre(2, getHeight()));
    }

  private:
    int start_width_ = 0;
  };

  PanelDivider panel_divider_;
  int browser_width_ = 260;
  // The selection the Properties panel edits: which objects are in it,
  // and the anchor — the most recently clicked member, which ⇧-click
  // ranges extend from and whose values a mixed control displays.
  // Never empty: the anchor is always a member.
  std::array<bool, KitModel::kObjectCount> selected_set_ {{true}};
  int selected_ = 0;
  // ALTERNATE mode's per-pad flip-flop (false = layer A fires next);
  // runtime state, deliberately not persisted.
  std::array<bool, KitModel::kObjectCount> alternate_flip_ {};
  // Velocity-coloured pad flash: velocity of the last hit (0 = idle)
  // and when it landed; the timer fades and expires it.
  std::array<int, KitModel::kObjectCount> pad_flash_velocity_ {};
  std::array<juce::uint32, KitModel::kObjectCount> pad_flash_ms_ {};
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
  // Shown in the header when the active kit has uncached device waves.
  juce::TextButton transfer_button_ {"Download samples"};
  // Header connection light + its polling state.
  ConnectionDot connection_dot_;
  std::atomic<bool> device_connected_ {false};
  // The unit's firmware version, read once per connection (message
  // thread only); shown in bug reports.
  // The version as the unit reports it ("2.00"), and the same plus the
  // build for display ("2.00 (0094)"). The firmware gate compares the
  // bare version; the display string is for bug reports.
  juce::Label status_bar_;
  // Without one of these, every setTooltip in the app is silent — JUCE
  // only shows tooltips while a TooltipWindow instance exists. On the
  // DESKTOP, not parented here: a child tooltip's square window corners
  // poke through its rounded bubble; a desktop one composites with
  // per-pixel alpha.
  juce::TooltipWindow tooltip_window_;

  // The tab bar under the global header. A plain TabbedButtonBar (not a
  // TabbedComponent): the Edit Kits "tab content" is this component's
  // own children and painting, switched by visibility.
  class TabsBar : public juce::TabbedButtonBar {
  public:
    using juce::TabbedButtonBar::TabbedButtonBar;
    std::function<void(int)> on_change;

    void currentTabChanged(int index, const juce::String&) override {
      if (on_change) {
        on_change(index);
      }
    }
  };

  TabsBar tabs_ {juce::TabbedButtonBar::TabsAtTop};
  // The kit editor's own surface switch: Pads / Triggers.
  TabsBar surface_tabs_ {juce::TabbedButtonBar::TabsAtTop};
  int surface_ = 0;  // 0 = pads, 1 = triggers
  BulkEditPanel bulk_panel_;
  // Every child that belongs to the Edit Kits tab (snapshot at the end
  // of the constructor); hidden wholesale when Bulk Edit is showing.
  std::vector<juce::Component*> edit_tab_children_;
  bool on_edit_tab_ = true;

  // Applies a bulk layer-mode edit to the document as one undoable
  // transaction (active kit stashed first). Returns the status line to
  // show.
  juce::String ApplyBulkSetMode(const ops::SetModeRequest& request);
  juce::String ApplyBulkPadLink(const ops::PadLinkRequest& request);
  juce::String device_firmware_version_;
  juce::String device_firmware_;
  // True while the connection poll's worker may be on the port. Shared
  // with every device-op worker: they wait it out before opening the
  // port themselves (opens are exclusive; a probe mid-flight would make
  // the op's open fail).
  std::shared_ptr<std::atomic<bool>> conn_check_running_ =
      std::make_shared<std::atomic<bool>>(false);
  juce::uint32 last_conn_check_ms_ = 0;
  // Follow-the-app kit selection on the unit: the pending 1-based kit
  // (shared so a detached worker can read it after switches), and a
  // single-runner flag. shared_ptr so the worker never touches a
  // destroyed component.
  std::shared_ptr<std::atomic<int>> pending_select_kit_ =
      std::make_shared<std::atomic<int>>(0);
  std::shared_ptr<std::atomic<bool>> kit_select_running_ =
      std::make_shared<std::atomic<bool>>(false);
  // When the app last told the unit which kit to show; the poll's
  // follow-the-device adoption holds off for an interval after it, so a
  // read that raced the select can't drag the app back (message-thread
  // only).
  juce::uint32 last_app_kit_select_ms_ = 0;
  // "Sync Changes with Device" header button. Dirtiness is computed, not
  // tracked: a kit is dirty when its content differs from the document's
  // base snapshot (DeviceDocument::DirtyKits). model_loading_ marks kit
  // loads, whose change listeners are not user edits.
  juce::TextButton sync_button_ {
      juce::String::fromUTF8("Sync Changes with Device")};
  bool model_loading_ = false;

  // An in-flight "Sync Changes with Device": the fresh device read, the
  // conflicts awaiting resolution, and the per-kit plans being pushed.
  // Present only while the sync runs; device_fetching_ stays true for
  // its whole span so nothing else opens the port.
  struct SyncSession {
    std::vector<KitData> theirs;  // all 200 kits, from the fresh read
    std::vector<SyncConflict> conflicts;
    std::vector<std::pair<int, KitSyncPlan>> plans;
    std::vector<UploadPlan> uploads;
    // Local files the pool already held (matched by filename +
    // duration): repointed at the existing wave, nothing uploaded. On
    // success their local audio becomes the wave's cache.
    std::vector<UploadPlan> reused;
    bool pulled = false;  // device-side changes landed locally

    // Uploads recorded this push. Each is written into the document
    // (pool entry, cached audio, and the layers that referenced the file
    // repointed at the new index) the moment ExecutePush reports it,
    // which it does only after reading it back off the device — so a
    // sync interrupted later resumes instead of uploading again.
    struct Landed {
      UploadPlan plan;
      juce::MemoryBlock wav;
      int frames = 0;
    };

    std::vector<Landed> landed;
  };

  std::unique_ptr<SyncSession> sync_;
  // Which stage the sync is in, so the 30 Hz timer feeds the right text
  // into the progress dialog (kResolving = the conflict dialog is up, so
  // no progress bar). Message-thread only.
  enum class SyncPhase {
    kNone,
    kReading,
    kResolving,
    kPushing
  };
  SyncPhase sync_phase_ = SyncPhase::kNone;
  int sync_pushed_ = 0;  // uploads landed so far this push (drives the bar)
  int sync_upload_total_ = 0;  // uploads this push will do
  // Set by the progress dialog's Abort button to stop waiting on the flash
  // commit; shared so the detached push worker reads it safely.
  std::shared_ptr<std::atomic<bool>> sync_abort_;
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
  // A TabbedComponent that reports tab changes, so the selection can
  // be persisted (JUCE's own has no callback).
  class PanelTabs : public juce::TabbedComponent {
  public:
    using juce::TabbedComponent::TabbedComponent;
    std::function<void(int)> on_change;

    void currentTabChanged(int index, const juce::String&) override {
      if (on_change) {
        on_change(index);
      }
    }
  };

  PanelTabs panel_tabs_ {juce::TabbedButtonBar::TabsAtTop};
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
