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

// Changes a pad's layer mode / fade parameters as one undo step.
class SetLayerParamsAction : public juce::UndoableAction {
public:
  SetLayerParamsAction(
      KitModel& model, int pad, LayerMode mode, int fade_point, int fade_end)
      : model_(model)
      , pad_(pad)
      , new_mode_(mode)
      , new_point_(fade_point)
      , new_end_(fade_end)
      , old_mode_(model.layer_mode(pad))
      , old_point_(model.fade_point(pad))
      , old_end_(model.fade_end(pad))
  {
  }

  bool perform() override
  {
    model_.SetLayerParams(pad_, new_mode_, new_point_, new_end_);
    return true;
  }

  bool undo() override
  {
    model_.SetLayerParams(pad_, old_mode_, old_point_, old_end_);
    return true;
  }

private:
  KitModel& model_;
  int pad_;
  LayerMode new_mode_;
  int new_point_;
  int new_end_;
  LayerMode old_mode_;
  int old_point_;
  int old_end_;
};

}  // namespace spdsx

#endif  // SPDSX_PATCHEDIT_SOURCE_ACTIONS_H_
