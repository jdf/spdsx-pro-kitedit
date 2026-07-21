#include "device_document.h"

#include "device_sync.h"  // KitDataFromDevice

namespace spdsx {

namespace {

// The newest legacy single-kit .kit version ImportKitFile understands
// (the KitFormat history: 1 flat slots, 2 pads, 3 layer modes, 4
// dynamics).
constexpr int kMaxKitVersion = 4;

// Tolerates short arrays, non-string entries, and absent fields
// throughout, so old and hand-edited files degrade gracefully.
Pad PadFromVar(const juce::var& v) {
  Pad pad;
  auto to_sample = [](const juce::var& entry) {
    if (entry.isString()) {
      return LayerSample(juce::File(entry.toString()));
    }
    if (entry.isInt() || entry.isInt64()) {
      return LayerSample::DeviceWave(juce::jmax(0, static_cast<int>(entry)));
    }
    return LayerSample();
  };
  if (const auto* samples = v.getProperty("samples", {}).getArray()) {
    if (samples->size() > 0) {
      pad.samples.first = to_sample((*samples)[0]);
    }
    if (samples->size() > 1) {
      pad.samples.second = to_sample((*samples)[1]);
    }
  }
  pad.params.mode = ParseLayerMode(
      v.getProperty("mode", "").toString().toStdString(), LayerMode::kMix);
  pad.params.fade_point = juce::jlimit(
      1, 127, static_cast<int>(v.getProperty("fadePoint", kDefaultFadePoint)));
  pad.params.fade_end = juce::jlimit(
      1, 127, static_cast<int>(v.getProperty("fadeEnd", kDefaultFadeEnd)));
  pad.params.dynamics = v.getProperty("dynamics", true);
  pad.params.curve = ParseDynamicsCurve(
      v.getProperty("dynamicsCurve", "").toString().toStdString(),
      DynamicsCurve::kLinear);
  // The blended modes read fade end as "at least fade point".
  pad.params.fade_end = juce::jmax(pad.params.fade_end, pad.params.fade_point);
  pad.params.fixed_velocity = juce::jlimit(
      1,
      127,
      static_cast<int>(v.getProperty("fixedVelocity", kDefaultFixedVelocity)));
  pad.params.trigger_reserve = v.getProperty("triggerReserve", false);
  pad.params.hi_hat_volume = juce::jlimit(
      0,
      127,
      static_cast<int>(v.getProperty("hiHatVolume", kDefaultHiHatVolume)));
  pad.params.hi_hat_fade_in = juce::jlimit(
      0,
      127,
      static_cast<int>(v.getProperty("hiHatFadeIn", kDefaultHiHatFadeIn)));
  pad.params.hi_hat_decay = juce::jlimit(
      0,
      127,
      static_cast<int>(v.getProperty("hiHatDecay", kDefaultHiHatDecay)));
  return pad;
}

}  // namespace

DeviceDocument::DeviceDocument(DeviceModel& device,
                               KitModel& model,
                               juce::ApplicationProperties& settings)
    : juce::FileBasedDocument(
          ".spdsx", "*.spdsx", "Open a device", "Save this device")
    , device_(device)
    , model_(model)
    , settings_(settings) {}

juce::String DeviceDocument::getDocumentTitle() {
  return getFile() != juce::File() ? getFile().getFileNameWithoutExtension()
                                   : "Untitled Device";
}

void DeviceDocument::ResetHistory() {
  if (on_history_reset) {
    on_history_reset();
  }
}

void DeviceDocument::ResetToUntitled() {
  device_.Reset();
  LoadActiveKitIntoModel();
  ResetHistory();
  setFile(juce::File());
  setChangedFlag(false);
}

bool DeviceDocument::HasCachedAudio(int sample_index) const {
  return db_ != nullptr && sample_index > 0 && db_->HasAudio(sample_index);
}

juce::File DeviceDocument::CachedWaveFile(int sample_index) {
  if (!HasCachedAudio(sample_index)) {
    return {};
  }
  // Extract the blob to a temp cache file the audio engine can load. The
  // DB is the source of truth; the temp file is derived and disposable.
  const juce::File file =
      juce::File::getSpecialLocation(juce::File::tempDirectory)
          .getChildFile("spdsx-wavecache")
          .getChildFile(juce::String(sample_index).paddedLeft('0', 5) + ".wav");
  if (!file.existsAsFile()) {
    const juce::MemoryBlock wav = db_->GetAudio(sample_index);
    file.getParentDirectory().createDirectory();
    file.replaceWithData(wav.getData(), wav.getSize());
  }
  return file;
}

void DeviceDocument::StoreWaveAudio(int sample_index,
                                    const juce::MemoryBlock& wav) {
  if (db_ == nullptr || sample_index <= 0) {
    return;
  }
  db_->PutAudio(sample_index, wav.getData(), wav.getSize());
  // Invalidate any stale extraction so the next load re-extracts.
  juce::File::getSpecialLocation(juce::File::tempDirectory)
      .getChildFile("spdsx-wavecache")
      .getChildFile(juce::String(sample_index).paddedLeft('0', 5) + ".wav")
      .deleteFile();
}

void DeviceDocument::StashActiveKit() {
  auto& kit = device_.kit(device_.current_kit());
  kit.name = model_.name();
  for (int pad = 0; pad < KitModel::kPadCount; ++pad) {
    auto& stored = kit.pads[static_cast<size_t>(pad)];
    stored.samples = {model_.sample(pad, 0), model_.sample(pad, 1)};
    stored.params = model_.params(pad);
  }
}

void DeviceDocument::LoadActiveKitIntoModel() {
  if (on_model_reload) {
    on_model_reload(true);
  }
  const KitData& kit = device_.kit(device_.current_kit());
  model_.set_name(kit.name);
  for (int pad = 0; pad < KitModel::kPadCount; ++pad) {
    const auto& stored = kit.pads[static_cast<size_t>(pad)];
    model_.set_sample(pad, 0, stored.samples.first);
    model_.set_sample(pad, 1, stored.samples.second);
    model_.SetPadParams(pad, stored.params);
  }
  if (on_model_reload) {
    on_model_reload(false);
  }
}

void DeviceDocument::SwitchKit(int index) {
  if (index == device_.current_kit() || index < 0
      || index >= DeviceModel::kKitCount) {
    return;
  }
  // Loading the model fires change listeners that mark the document
  // edited; a kit switch is view state, so put the flag back. Undo
  // histories are per-kit and survive the switch untouched.
  const bool was_changed = hasChangedSinceSaved();
  StashActiveKit();
  device_.set_current_kit(index);
  LoadActiveKitIntoModel();
  setChangedFlag(was_changed);
}

juce::Result DeviceDocument::OpenDb(const juce::File& file) {
  juce::String error;
  auto db = DeviceDb::Open(file, error);
  if (db == nullptr) {
    return juce::Result::fail(error);
  }
  if (db->SchemaVersion() > DeviceDb::kCurrentSchemaVersion) {
    return juce::Result::fail(
        file.getFileName()
        + " was written by a newer version of spdsx-patchedit (document v"
        + juce::String(db->SchemaVersion()) + "; this build reads up to v"
        + juce::String(DeviceDb::kCurrentSchemaVersion) + ")");
  }
  db_ = std::move(db);
  return juce::Result::ok();
}

juce::Result DeviceDocument::loadDocument(const juce::File& chosen) {
  if (const auto r = OpenDb(chosen); r.failed()) {
    return r;
  }
  device_.Reset();
  db_->ReadKits(device_);  // kits + pads + current kit + sample pool
  LoadActiveKitIntoModel();
  ResetHistory();  // a freshly loaded device starts with clean histories
  return juce::Result::ok();
}

juce::Result DeviceDocument::saveDocument(const juce::File& chosen) {
  StashActiveKit();
  // Save As / move: carry the existing database (with its audio blobs) to
  // the new path, then continue writing there. Closing first is what makes
  // the file safe to copy — in WAL mode a clean close is what checkpoints it.
  if (db_ != nullptr && getFile() != juce::File() && getFile() != chosen) {
    const juce::File previous = getFile();
    db_.reset();
    if (!previous.copyFileTo(chosen)) {
      // Reopen the one we just closed: left without a database the document
      // would go on taking edits while Autosave silently dropped every one.
      if (const auto r = OpenDb(previous); r.failed()) {
        return juce::Result::fail(
            "couldn't copy document to " + chosen.getFullPathName()
            + ", and couldn't reopen " + previous.getFileName() + ": "
            + r.getErrorMessage());
      }
      return juce::Result::fail("couldn't copy document to "
                                + chosen.getFullPathName());
    }
  }
  if (db_ == nullptr) {
    if (const auto r = OpenDb(chosen); r.failed()) {
      return r;
    }
  }
  db_->WriteKits(device_);
  db_->WritePool(device_);
  return juce::Result::ok();
}

juce::Result DeviceDocument::OpenDevice(const juce::File& chosen) {
  const auto result = loadDocument(chosen);
  if (result.failed()) {
    return result;
  }
  setFile(chosen);
  setLastDocumentOpened(chosen);
  setChangedFlag(false);
  return result;
}

juce::Result DeviceDocument::CreateNew(const juce::File& file) {
  // Start a brand-new database, blowing away anything already there —
  // including a legacy folder-package document at the same path.
  if (file.isDirectory()) {
    file.deleteRecursively();
  } else {
    file.deleteFile();
  }
  device_.Reset();
  LoadActiveKitIntoModel();
  ResetHistory();
  setFile(file);
  if (const auto r = OpenDb(file); r.failed()) {
    return r;
  }
  db_->WriteKits(device_);
  db_->WritePool(device_);
  setLastDocumentOpened(file);
  setChangedFlag(false);
  return juce::Result::ok();
}

void DeviceDocument::Autosave() {
  if (db_ == nullptr || getFile() == juce::File()) {
    return;
  }
  StashActiveKit();
  db_->WriteKits(device_);  // kits only; pool/audio persist when they change
  setChangedFlag(false);
}

void DeviceDocument::ReplaceWithDeviceState(
    const std::vector<device::KitRecord>& kits,
    std::vector<device::SampleRecord> pool) {
  for (int i = 0; i < DeviceModel::kKitCount; ++i) {
    device_.kit(i) = i < static_cast<int>(kits.size())
        ? KitDataFromDevice(kits[static_cast<size_t>(i)])
        : KitData();
  }
  device_.set_sample_pool(std::move(pool));
  LoadActiveKitIntoModel();
  // Freshly read from the device: persist kits + the new pool, and make
  // this the clean sync base (current == device == base).
  if (db_ != nullptr) {
    db_->WriteKits(device_);
    db_->WritePool(device_);
    db_->CaptureBase();
  }
  // Replaced wholesale; deliberately not undoable.
  ResetHistory();
  changed();
}

juce::Result DeviceDocument::ImportKitFile(const juce::File& file) {
  auto parsed = juce::JSON::parse(file.loadFileAsString());
  if (!parsed.isObject()) {
    return juce::Result::fail(file.getFileName() + " is not a valid .kit file");
  }
  const int version = parsed.getProperty("version", 0);
  if (version > kMaxKitVersion) {
    return juce::Result::fail(
        file.getFileName() + " is kit format v" + juce::String(version)
        + "; this build reads up to v" + juce::String(kMaxKitVersion));
  }
  KitData kit;
  const auto name = parsed.getProperty("name", "").toString();
  kit.name = name.isNotEmpty() ? name : "Untitled Kit";
  if (const auto* pads = parsed.getProperty("pads", {}).getArray()) {
    for (int i = 0; i < KitModel::kPadCount && i < pads->size(); ++i) {
      kit.pads[static_cast<size_t>(i)] = PadFromVar((*pads)[i]);
    }
  } else if (const auto* slots = parsed.getProperty("slots", {}).getArray()) {
    // The pre-pad flat era: 18 entries in (pad * 2 + layer) order.
    for (int i = 0; i < KitModel::kSlotCount && i < slots->size(); ++i) {
      const auto& entry = (*slots)[i];
      auto& pad = kit.pads[static_cast<size_t>(i / 2)];
      auto& sample = i % 2 == 0 ? pad.samples.first : pad.samples.second;
      sample = entry.isString() ? LayerSample(juce::File(entry.toString()))
                                : LayerSample();
    }
  }
  device_.kit(device_.current_kit()) = kit;
  LoadActiveKitIntoModel();
  ResetHistory();
  changed();
  return juce::Result::ok();
}

// FileBasedDocument starts its open/save dialogs here; persisting it
// keeps device dialogs anchored to device territory across sessions,
// independent of where samples were last browsed.
juce::File DeviceDocument::getLastDocumentOpened() {
  return {settings_.getUserSettings()->getValue("lastDeviceFile")};
}

void DeviceDocument::setLastDocumentOpened(const juce::File& file) {
  settings_.getUserSettings()->setValue("lastDeviceFile",
                                        file.getFullPathName());
}

}  // namespace spdsx
