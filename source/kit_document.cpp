#include "kit_document.hpp"

namespace spdsx {

KitDocument::KitDocument(KitModel& model, juce::UndoManager& undo)
    : juce::FileBasedDocument(
          ".kit", "*.kit", "Open a kit", "Save this kit")
    , model_(model)
    , undo_(undo)
{
}

juce::String KitDocument::getDocumentTitle()
{
  return model_.name();
}

void KitDocument::reset_to_untitled()
{
  model_.set_name("Untitled Kit");
  for (int i = 0; i < KitModel::kSlotCount; ++i) {
    model_.set_slot(i, juce::File());
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
  const auto* slots = parsed.getProperty("slots", {}).getArray();
  for (int i = 0; i < KitModel::kSlotCount; ++i) {
    juce::var entry;
    if (slots != nullptr && i < slots->size()) {
      entry = (*slots)[i];
    }
    // Tolerates short arrays and non-string entries; unreadable paths
    // surface as "missing" slots rather than failing the load.
    model_.set_slot(
        i, entry.isString() ? juce::File(entry.toString()) : juce::File());
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
  juce::Array<juce::var> slots;
  for (int i = 0; i < KitModel::kSlotCount; ++i) {
    const auto& sample = model_.slot(i);
    slots.add(sample == juce::File()
            ? juce::var()
            : juce::var(sample.getFullPathName()));
  }
  obj->setProperty("slots", slots);
  if (!file.replaceWithText(
          juce::JSON::toString(juce::var(obj)) + "\n"))
  {
    return juce::Result::fail(
        "couldn't write " + file.getFullPathName());
  }
  return juce::Result::ok();
}

juce::File KitDocument::getLastDocumentOpened()
{
  return last_opened_;
}

void KitDocument::setLastDocumentOpened(const juce::File& file)
{
  last_opened_ = file;
}

}  // namespace spdsx
