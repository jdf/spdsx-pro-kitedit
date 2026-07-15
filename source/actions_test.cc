#include "actions.h"

#include <gtest/gtest.h>

namespace spdsx {
namespace {

class ActionsTest : public ::testing::Test {
protected:
  KitModel model;
};

// ---- SetSampleAction: assign, replace, clear ----

TEST_F(ActionsTest, SetSampleActionAssignsAndReverts)
{
  const LayerSample sample = LayerSample::DeviceWave(7);
  SetSampleAction action(model, 3, 1, sample);

  EXPECT_TRUE(action.perform());
  EXPECT_EQ(model.sample(3, 1), sample);

  EXPECT_TRUE(action.undo());
  EXPECT_TRUE(model.sample(3, 1).empty());
}

TEST_F(ActionsTest, SetSampleActionRestoresTheReplacedSample)
{
  const LayerSample first {juce::File("/tmp/a.wav")};
  const LayerSample second = LayerSample::DeviceWave(2);
  model.set_sample(0, 0, first);

  SetSampleAction action(model, 0, 0, second);
  action.perform();
  EXPECT_EQ(model.sample(0, 0), second);

  action.undo();
  EXPECT_EQ(model.sample(0, 0), first);
}

// Clearing is an assignment of an empty sample, so it undoes like any other.
TEST_F(ActionsTest, SetSampleActionClearsALayer)
{
  const LayerSample sample = LayerSample::DeviceWave(5);
  model.set_sample(0, 0, sample);

  SetSampleAction action(model, 0, 0, LayerSample());
  action.perform();
  EXPECT_TRUE(model.sample(0, 0).empty());

  action.undo();
  EXPECT_EQ(model.sample(0, 0), sample);
}

// The action snapshots the old value when it is CONSTRUCTED, not when it
// performs: undo returns to the state at construction time, whatever
// happened in between.
TEST_F(ActionsTest, SetSampleActionSnapshotsTheOldValueAtConstruction)
{
  const LayerSample at_construction {juce::File("/tmp/a.wav")};
  model.set_sample(0, 0, at_construction);

  SetSampleAction action(model, 0, 0, LayerSample::DeviceWave(9));
  model.set_sample(0, 0, LayerSample::DeviceWave(3));  // changed behind it

  action.perform();
  action.undo();
  EXPECT_EQ(model.sample(0, 0), at_construction);
}

// ---- SetKitNameAction ----

TEST_F(ActionsTest, SetKitNameActionAppliesAndReverts)
{
  const juce::String original = model.name();
  SetKitNameAction action(model, "ZZZ");

  EXPECT_TRUE(action.perform());
  EXPECT_EQ(model.name(), juce::String("ZZZ"));

  EXPECT_TRUE(action.undo());
  EXPECT_EQ(model.name(), original);
}

// ---- SetPadParamsAction ----

TEST_F(ActionsTest, SetPadParamsActionAppliesAndRevertsAsAUnit)
{
  const PadParams before = model.params(0);
  PadParams params = before;
  params.mode = LayerMode::kXfade;
  params.dynamics = false;
  params.curve = DynamicsCurve::kLoud3;
  params.fixed_velocity = 30;

  SetPadParamsAction action(model, 0, params);

  EXPECT_TRUE(action.perform());
  EXPECT_EQ(model.params(0), params);

  EXPECT_TRUE(action.undo());
  EXPECT_EQ(model.params(0), before);
}

TEST_F(ActionsTest, SetPadParamsActionLeavesOtherPadsAlone)
{
  PadParams params = model.params(0);
  params.mode = LayerMode::kAlternate;

  SetPadParamsAction action(model, 0, params);
  action.perform();

  EXPECT_EQ(model.params(1).mode, LayerMode::kMix);
}

// ---- Through a real UndoManager, the way main_component drives them ----

TEST_F(ActionsTest, ActionsUndoAndRedoThroughTheUndoManager)
{
  juce::UndoManager undo;
  undo.beginNewTransaction();
  undo.perform(new SetKitNameAction(model, "ZZZ"));

  EXPECT_EQ(model.name(), juce::String("ZZZ"));
  ASSERT_TRUE(undo.canUndo());

  undo.undo();
  EXPECT_EQ(model.name(), juce::String("Untitled Kit"));

  ASSERT_TRUE(undo.canRedo());
  undo.redo();
  EXPECT_EQ(model.name(), juce::String("ZZZ"));
}

// A gesture is wrapped in one transaction, so its actions undo as one step.
TEST_F(ActionsTest, ActionsInOneTransactionUndoTogether)
{
  juce::UndoManager undo;
  undo.beginNewTransaction();
  undo.perform(new SetSampleAction(model, 0, 0, LayerSample::DeviceWave(1)));
  undo.perform(new SetSampleAction(model, 0, 1, LayerSample::DeviceWave(2)));

  undo.undo();

  EXPECT_TRUE(model.sample(0, 0).empty());
  EXPECT_TRUE(model.sample(0, 1).empty());
  EXPECT_FALSE(undo.canUndo());
}

TEST_F(ActionsTest, SeparateTransactionsUndoSeparately)
{
  juce::UndoManager undo;
  undo.beginNewTransaction();
  undo.perform(new SetKitNameAction(model, "FIRST"));
  undo.beginNewTransaction();
  undo.perform(new SetKitNameAction(model, "SECOND"));

  undo.undo();
  EXPECT_EQ(model.name(), juce::String("FIRST"));

  undo.undo();
  EXPECT_EQ(model.name(), juce::String("Untitled Kit"));
}

}  // namespace
}  // namespace spdsx
