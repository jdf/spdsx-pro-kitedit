#include "kit_document.hpp"

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

void KitDocument::reset_to_untitled()
{
  model_.set_name("Untitled Kit");
  for (int pad = 0; pad < KitModel::kPadCount; ++pad) {
    for (int layer = 0; layer < KitModel::kLayersPerPad; ++layer) {
      model_.set_sample(pad, layer, juce::File());
    }
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
  }
  // A freshly loaded kit starts with a clean history; undoing into a
  // different document's edits would be nonsense.
  undo_.clearUndoHistory();
  return juce::Result::ok();
}

juce::Result KitDocument::saveDocument(const juce::File& file)
{
  auto* obj = new juce::DynamicObject();
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
