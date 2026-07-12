#include "kit_document.h"

namespace spdsx {

KitDocument::KitDocument(KitModel& model,
    juce::UndoManager& undo,
    juce::ApplicationProperties& settings)
    : juce::FileBasedDocument(
          ".kit", "*.kit", "Open a kit", "Save this kit")
    , model_(model)
    , undo_(undo)
    , settings_(settings)
{
}

juce::String KitDocument::getDocumentTitle()
{
  return model_.name();
}

void KitDocument::ResetToUntitled()
{
  model_.set_name("Untitled Kit");
  for (int pad = 0; pad < KitModel::kPadCount; ++pad) {
    for (int layer = 0; layer < KitModel::kLayersPerPad; ++layer) {
      model_.set_sample(pad, layer, juce::File());
    }
    model_.SetPadParams(pad, KitModel::DefaultParams(pad));
  }
  undo_.clearUndoHistory();
  setFile(juce::File());
  setChangedFlag(false);
}

juce::Result KitDocument::loadDocument(const juce::File& file)
{
  auto parsed = juce::JSON::parse(file.loadFileAsString());
  if (!parsed.isObject()) {
    return juce::Result::fail(
        file.getFileName() + " is not a valid .kit file");
  }
  // Unversioned files predate the version field: flat_slots-era kits
  // and the first pad-shaped kits alike; the structural fallback below
  // handles both.
  const int version = parsed.getProperty("version", 0);
  if (version > static_cast<int>(KitFormat::kCurrent)) {
    return juce::Result::fail(file.getFileName()
        + " was saved by a newer version of spdsx-patchedit (format v"
        + juce::String(version) + "; this build reads up to v"
        + juce::String(static_cast<int>(KitFormat::kCurrent)) + ")");
  }

  auto name = parsed.getProperty("name", "Untitled Kit").toString();
  model_.set_name(name.isNotEmpty() ? name : "Untitled Kit");

  // Tolerates short arrays and non-string entries throughout; unreadable
  // paths surface as "missing" slots rather than failing the load.
  auto to_file = [](const juce::var& entry)
  {
    return entry.isString() ? juce::File(entry.toString()) : juce::File();
  };
  const auto* pads = parsed.getProperty("pads", {}).getArray();
  const auto* legacy_slots = parsed.getProperty("slots", {}).getArray();
  for (int pad = 0; pad < KitModel::kPadCount; ++pad) {
    for (int layer = 0; layer < KitModel::kLayersPerPad; ++layer) {
      juce::var entry;
      if (pads != nullptr) {
        if (pad < pads->size()) {
          const auto* samples =
              (*pads)[pad].getProperty("samples", {}).getArray();
          if (samples != nullptr && layer < samples->size()) {
            entry = (*samples)[layer];
          }
        }
      } else if (legacy_slots != nullptr) {
        // Kits from before the pad-shaped format: a flat 18-entry array
        // in (pad * 2 + layer) order.
        const int idx = pad * KitModel::kLayersPerPad + layer;
        if (idx < legacy_slots->size()) {
          entry = (*legacy_slots)[idx];
        }
      }
      model_.set_sample(pad, layer, to_file(entry));
    }
    // Layer parameters arrived in v3, dynamics in v4; absent fields
    // read as the field defaults (MIX, dynamics on, LINEAR, no reserve).
    PadParams params;
    if (pads != nullptr && pad < pads->size()) {
      const auto& pad_var = (*pads)[pad];
      params.mode = ParseLayerMode(
          pad_var.getProperty("mode", "").toString().toStdString(),
          LayerMode::kMix);
      params.fade_point = juce::jlimit(1, 127,
          static_cast<int>(
              pad_var.getProperty("fadePoint", kDefaultFadePoint)));
      params.fade_end = juce::jlimit(1, 127,
          static_cast<int>(pad_var.getProperty("fadeEnd", kDefaultFadeEnd)));
      params.dynamics = pad_var.getProperty("dynamics", true);
      params.curve = ParseDynamicsCurve(
          pad_var.getProperty("dynamicsCurve", "").toString().toStdString(),
          DynamicsCurve::kLinear);
      params.trigger_reserve = pad_var.getProperty("triggerReserve", false);
    }
    model_.SetPadParams(pad, params);
  }
  // A freshly loaded kit starts with a clean history; undoing into a
  // different document's edits would be nonsense.
  undo_.clearUndoHistory();
  return juce::Result::ok();
}

juce::Result KitDocument::saveDocument(const juce::File& file)
{
  auto* obj = new juce::DynamicObject();
  obj->setProperty("version", static_cast<int>(KitFormat::kCurrent));
  obj->setProperty("name", model_.name());
  juce::Array<juce::var> pads;
  for (int pad = 0; pad < KitModel::kPadCount; ++pad) {
    auto* pad_obj = new juce::DynamicObject();
    juce::Array<juce::var> samples;
    for (int layer = 0; layer < KitModel::kLayersPerPad; ++layer) {
      const auto& sample = model_.sample(pad, layer);
      samples.add(sample == juce::File()
              ? juce::var()
              : juce::var(sample.getFullPathName()));
    }
    pad_obj->setProperty("samples", samples);
    const auto& params = model_.params(pad);
    const auto mode_name = LayerModeName(params.mode);
    pad_obj->setProperty("mode",
        juce::String(mode_name.data(), mode_name.size()));
    pad_obj->setProperty("fadePoint", params.fade_point);
    pad_obj->setProperty("fadeEnd", params.fade_end);
    pad_obj->setProperty("dynamics", params.dynamics);
    const auto curve_name = DynamicsCurveName(params.curve);
    pad_obj->setProperty("dynamicsCurve",
        juce::String(curve_name.data(), curve_name.size()));
    pad_obj->setProperty("triggerReserve", params.trigger_reserve);
    pads.add(juce::var(pad_obj));
  }
  obj->setProperty("pads", pads);
  if (!file.replaceWithText(
          juce::JSON::toString(juce::var(obj)) + "\n"))
  {
    return juce::Result::fail(
        "couldn't write " + file.getFullPathName());
  }
  return juce::Result::ok();
}

// FileBasedDocument starts its open/save dialogs here; persisting it
// keeps the kit dialogs anchored to kit territory across sessions,
// independent of where samples were last browsed.
juce::File KitDocument::getLastDocumentOpened()
{
  return {settings_.getUserSettings()->getValue("lastKitFile")};
}

void KitDocument::setLastDocumentOpened(const juce::File& file)
{
  settings_.getUserSettings()->setValue(
      "lastKitFile", file.getFullPathName());
}

}  // namespace spdsx
