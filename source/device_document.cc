#include "device_document.h"

namespace spdsx {

namespace {

constexpr const char* kManifestName = "device.json";
// The newest legacy single-kit .kit version ImportKitFile understands
// (the KitFormat history: 1 flat slots, 2 pads, 3 layer modes, 4
// dynamics).
constexpr int kMaxKitVersion = 4;

juce::String NameOf(std::string_view s)
{
  return {s.data(), s.size()};
}

juce::var PadToVar(const Pad& pad)
{
  auto* obj = new juce::DynamicObject();
  juce::Array<juce::var> samples;
  for (const auto* sample : {&pad.samples.first, &pad.samples.second}) {
    samples.add(*sample == juce::File()
            ? juce::var()
            : juce::var(sample->getFullPathName()));
  }
  obj->setProperty("samples", samples);
  obj->setProperty("mode", NameOf(LayerModeName(pad.params.mode)));
  obj->setProperty("fadePoint", pad.params.fade_point);
  obj->setProperty("fadeEnd", pad.params.fade_end);
  obj->setProperty("dynamics", pad.params.dynamics);
  obj->setProperty(
      "dynamicsCurve", NameOf(DynamicsCurveName(pad.params.curve)));
  obj->setProperty("triggerReserve", pad.params.trigger_reserve);
  return juce::var(obj);
}

// Tolerates short arrays, non-string entries, and absent fields
// throughout, so old and hand-edited files degrade gracefully.
Pad PadFromVar(const juce::var& v)
{
  Pad pad;
  auto to_file = [](const juce::var& entry)
  {
    return entry.isString() ? juce::File(entry.toString()) : juce::File();
  };
  if (const auto* samples = v.getProperty("samples", {}).getArray()) {
    if (samples->size() > 0) {
      pad.samples.first = to_file((*samples)[0]);
    }
    if (samples->size() > 1) {
      pad.samples.second = to_file((*samples)[1]);
    }
  }
  pad.params.mode = ParseLayerMode(
      v.getProperty("mode", "").toString().toStdString(), LayerMode::kMix);
  pad.params.fade_point = juce::jlimit(1, 127,
      static_cast<int>(v.getProperty("fadePoint", kDefaultFadePoint)));
  pad.params.fade_end = juce::jlimit(1, 127,
      static_cast<int>(v.getProperty("fadeEnd", kDefaultFadeEnd)));
  pad.params.dynamics = v.getProperty("dynamics", true);
  pad.params.curve = ParseDynamicsCurve(
      v.getProperty("dynamicsCurve", "").toString().toStdString(),
      DynamicsCurve::kLinear);
  pad.params.trigger_reserve = v.getProperty("triggerReserve", false);
  return pad;
}

juce::var KitToVar(const KitData& kit)
{
  auto* obj = new juce::DynamicObject();
  obj->setProperty("name", kit.name);
  juce::Array<juce::var> pads;
  for (const auto& pad : kit.pads) {
    pads.add(PadToVar(pad));
  }
  obj->setProperty("pads", pads);
  return juce::var(obj);
}

KitData KitFromVar(const juce::var& v)
{
  KitData kit;
  const auto name = v.getProperty("name", "").toString();
  if (name.isNotEmpty()) {
    kit.name = name;
  }
  if (const auto* pads = v.getProperty("pads", {}).getArray()) {
    for (int i = 0; i < KitModel::kPadCount && i < pads->size(); ++i) {
      kit.pads[static_cast<size_t>(i)] = PadFromVar((*pads)[i]);
    }
  }
  return kit;
}

}  // namespace

DeviceDocument::DeviceDocument(DeviceModel& device,
    KitModel& model,
    juce::UndoManager& undo,
    juce::ApplicationProperties& settings)
    : juce::FileBasedDocument(".spdsx",
          // device.json is included so the manifest stays openable even
          // where the package registration hasn't taken effect and the
          // .spdsx folder can only be navigated into.
          "*.spdsx;device.json",
          "Open a device",
          "Save this device")
    , device_(device)
    , model_(model)
    , undo_(undo)
    , settings_(settings)
{
}

juce::String DeviceDocument::getDocumentTitle()
{
  return getFile() != juce::File() ? getFile().getFileNameWithoutExtension()
                                   : "Untitled Device";
}

void DeviceDocument::ResetToUntitled()
{
  device_.Reset();
  LoadActiveKitIntoModel();
  undo_.clearUndoHistory();
  setFile(juce::File());
  setChangedFlag(false);
}

void DeviceDocument::StashActiveKit()
{
  auto& kit = device_.kit(device_.current_kit());
  kit.name = model_.name();
  for (int pad = 0; pad < KitModel::kPadCount; ++pad) {
    auto& stored = kit.pads[static_cast<size_t>(pad)];
    stored.samples = {model_.sample(pad, 0), model_.sample(pad, 1)};
    stored.params = model_.params(pad);
  }
}

void DeviceDocument::LoadActiveKitIntoModel()
{
  const KitData& kit = device_.kit(device_.current_kit());
  model_.set_name(kit.name);
  for (int pad = 0; pad < KitModel::kPadCount; ++pad) {
    const auto& stored = kit.pads[static_cast<size_t>(pad)];
    model_.set_sample(pad, 0, stored.samples.first);
    model_.set_sample(pad, 1, stored.samples.second);
    model_.SetPadParams(pad, stored.params);
  }
}

void DeviceDocument::SwitchKit(int index)
{
  if (index == device_.current_kit() || index < 0
      || index >= DeviceModel::kKitCount)
  {
    return;
  }
  // Loading the model fires change listeners that mark the document
  // edited; a kit switch is view state, so put the flag back.
  const bool was_changed = hasChangedSinceSaved();
  StashActiveKit();
  device_.set_current_kit(index);
  LoadActiveKitIntoModel();
  undo_.clearUndoHistory();
  setChangedFlag(was_changed);
}

juce::Result DeviceDocument::loadDocument(const juce::File& chosen)
{
  // Accept the package folder, or its manifest directly (the fallback
  // path when the folder isn't registered as a package).
  const juce::File folder = chosen.getFileName() == kManifestName
      ? chosen.getParentDirectory()
      : chosen;
  const juce::File manifest = folder.getChildFile(kManifestName);
  if (!manifest.existsAsFile()) {
    return juce::Result::fail(
        folder.getFileName() + " has no " + kManifestName);
  }
  auto parsed = juce::JSON::parse(manifest.loadFileAsString());
  if (!parsed.isObject()) {
    return juce::Result::fail(
        juce::String(kManifestName) + " is not valid JSON");
  }
  const int format = parsed.getProperty("format", 0);
  if (format > static_cast<int>(DeviceFormat::kCurrent)) {
    return juce::Result::fail(folder.getFileName()
        + " was saved by a newer version of spdsx-patchedit (format v"
        + juce::String(format) + "; this build reads up to v"
        + juce::String(static_cast<int>(DeviceFormat::kCurrent)) + ")");
  }
  device_.Reset();
  if (const auto* kits = parsed.getProperty("kits", {}).getArray()) {
    for (int i = 0; i < DeviceModel::kKitCount && i < kits->size(); ++i) {
      device_.kit(i) = KitFromVar((*kits)[i]);
    }
  }
  device_.set_current_kit(juce::jlimit(0, DeviceModel::kKitCount - 1,
      static_cast<int>(parsed.getProperty("currentKit", 0))));
  LoadActiveKitIntoModel();
  // A freshly loaded device starts with a clean history.
  undo_.clearUndoHistory();
  return juce::Result::ok();
}

juce::Result DeviceDocument::saveDocument(const juce::File& chosen)
{
  StashActiveKit();
  // A stale plain file in the way gets replaced by the package folder;
  // an existing folder is written into, preserving anything else it
  // holds (the future samples/ cache).
  if (chosen.existsAsFile()) {
    chosen.deleteFile();
  }
  if (const auto res = chosen.createDirectory(); res.failed()) {
    return juce::Result::fail(
        "couldn't create " + chosen.getFullPathName());
  }
  auto* obj = new juce::DynamicObject();
  obj->setProperty("format", static_cast<int>(DeviceFormat::kCurrent));
  obj->setProperty("currentKit", device_.current_kit());
  juce::Array<juce::var> kits;
  for (int i = 0; i < DeviceModel::kKitCount; ++i) {
    kits.add(KitToVar(device_.kit(i)));
  }
  obj->setProperty("kits", kits);
  const juce::File manifest = chosen.getChildFile(kManifestName);
  if (!manifest.replaceWithText(
          juce::JSON::toString(juce::var(obj)) + "\n"))
  {
    return juce::Result::fail(
        "couldn't write " + manifest.getFullPathName());
  }
  return juce::Result::ok();
}

juce::Result DeviceDocument::OpenDevice(const juce::File& chosen)
{
  const juce::File folder = chosen.getFileName() == kManifestName
      ? chosen.getParentDirectory()
      : chosen;
  const auto result = loadDocument(folder);
  if (result.failed()) {
    return result;
  }
  setFile(folder);
  setLastDocumentOpened(folder);
  setChangedFlag(false);
  return result;
}

juce::Result DeviceDocument::ImportKitFile(const juce::File& file)
{
  auto parsed = juce::JSON::parse(file.loadFileAsString());
  if (!parsed.isObject()) {
    return juce::Result::fail(
        file.getFileName() + " is not a valid .kit file");
  }
  const int version = parsed.getProperty("version", 0);
  if (version > kMaxKitVersion) {
    return juce::Result::fail(file.getFileName() + " is kit format v"
        + juce::String(version) + "; this build reads up to v"
        + juce::String(kMaxKitVersion));
  }
  KitData kit;
  const auto name = parsed.getProperty("name", "").toString();
  kit.name = name.isNotEmpty() ? name : "Untitled Kit";
  if (const auto* pads = parsed.getProperty("pads", {}).getArray()) {
    for (int i = 0; i < KitModel::kPadCount && i < pads->size(); ++i) {
      kit.pads[static_cast<size_t>(i)] = PadFromVar((*pads)[i]);
    }
  } else if (const auto* slots = parsed.getProperty("slots", {}).getArray())
  {
    // The pre-pad flat era: 18 entries in (pad * 2 + layer) order.
    for (int i = 0; i < KitModel::kSlotCount && i < slots->size(); ++i) {
      const auto& entry = (*slots)[i];
      auto& pad = kit.pads[static_cast<size_t>(i / 2)];
      auto& sample = i % 2 == 0 ? pad.samples.first : pad.samples.second;
      sample = entry.isString() ? juce::File(entry.toString())
                                : juce::File();
    }
  }
  device_.kit(device_.current_kit()) = kit;
  LoadActiveKitIntoModel();
  undo_.clearUndoHistory();
  changed();
  return juce::Result::ok();
}

// FileBasedDocument starts its open/save dialogs here; persisting it
// keeps device dialogs anchored to device territory across sessions,
// independent of where samples were last browsed.
juce::File DeviceDocument::getLastDocumentOpened()
{
  return {settings_.getUserSettings()->getValue("lastDeviceFile")};
}

void DeviceDocument::setLastDocumentOpened(const juce::File& file)
{
  settings_.getUserSettings()->setValue(
      "lastDeviceFile", file.getFullPathName());
}

}  // namespace spdsx
