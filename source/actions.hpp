// Undoable mutations of the kit model.
#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include "kit_model.hpp"

namespace spdsx {

// Covers the whole sample mutation surface: add is empty->file, replace
// is file->file, delete is file->empty.
class SetSampleAction : public juce::UndoableAction {
public:
  SetSampleAction(
      KitModel& model, int pad, int layer, const juce::File& new_file)
      : model_(model)
      , pad_(pad)
      , layer_(layer)
      , new_(new_file)
      , old_(model.sample(pad, layer))
  {
  }

  bool perform() override
  {
    model_.set_sample(pad_, layer_, new_);
    return true;
  }

  bool undo() override
  {
    model_.set_sample(pad_, layer_, old_);
    return true;
  }

private:
  KitModel& model_;
  int pad_;
  int layer_;
  juce::File new_;
  juce::File old_;
};

}  // namespace spdsx
