// Undoable mutations of the kit model.
#ifndef SPDSX_PATCHEDIT_SOURCE_ACTIONS_H_
#define SPDSX_PATCHEDIT_SOURCE_ACTIONS_H_

#include <juce_data_structures/juce_data_structures.h>

#include "kit_model.h"

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

// Changes a pad's hit-response parameters (layer mode, fades, dynamics,
// trigger reserve) as one undo step.
class SetPadParamsAction : public juce::UndoableAction {
public:
  SetPadParamsAction(KitModel& model, int pad, const PadParams& params)
      : model_(model)
      , pad_(pad)
      , new_(params)
      , old_(model.params(pad))
  {
  }

  bool perform() override
  {
    model_.SetPadParams(pad_, new_);
    return true;
  }

  bool undo() override
  {
    model_.SetPadParams(pad_, old_);
    return true;
  }

private:
  KitModel& model_;
  int pad_;
  PadParams new_;
  PadParams old_;
};

}  // namespace spdsx

#endif  // SPDSX_PATCHEDIT_SOURCE_ACTIONS_H_
