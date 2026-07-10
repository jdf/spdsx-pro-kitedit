// Undoable mutations of the kit model.
#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include "kit_model.hpp"

namespace spdsx {

// Covers the whole slot mutation surface: add is empty->file, replace
// is file->file, delete is file->empty.
class SetSlotAction : public juce::UndoableAction {
public:
  SetSlotAction(KitModel& model, int idx, const juce::File& new_file)
      : model_(model)
      , idx_(idx)
      , new_(new_file)
      , old_(model.slot(idx))
  {
  }

  bool perform() override
  {
    model_.set_slot(idx_, new_);
    return true;
  }

  bool undo() override
  {
    model_.set_slot(idx_, old_);
    return true;
  }

private:
  KitModel& model_;
  int idx_;
  juce::File new_;
  juce::File old_;
};

}  // namespace spdsx
