#include "kit_model.h"

#include <stdexcept>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace spdsx {
namespace {

// Records what the model announced, so the tests can assert both that a
// notification fired and that it carried the right pad/layer.
class RecordingListener : public KitModel::Listener {
public:
  void KitNameChanged() override { ++name_changes; }

  void SampleChanged(int pad, int layer) override {
    samples.emplace_back(pad, layer);
  }

  void PadParamsChanged(int pad) override { params.push_back(pad); }

  int name_changes = 0;
  std::vector<std::pair<int, int>> samples;
  std::vector<int> params;
};

class KitModelTest : public ::testing::Test {
protected:
  void SetUp() override { model.AddListener(&listener); }

  void TearDown() override { model.RemoveListener(&listener); }

  KitModel model;
  RecordingListener listener;
};

// ---- PadParams ----

// The literals are the device's factory values (see the kit-record layout in
// CLAUDE.md), so this pins them rather than restating the constants.
TEST(PadParams, DefaultsAreTheDeviceFactoryValues) {
  const PadParams params;
  EXPECT_EQ(params.mode, LayerMode::kMix);
  EXPECT_EQ(params.fade_point, 80);
  EXPECT_EQ(params.fade_end, 127);
  EXPECT_TRUE(params.dynamics);
  EXPECT_EQ(params.curve, DynamicsCurve::kLinear);
  EXPECT_EQ(params.fixed_velocity, 127);
  EXPECT_FALSE(params.trigger_reserve);
  EXPECT_EQ(params.hi_hat_volume, 80);
  EXPECT_EQ(params.hi_hat_fade_in, 0);
  EXPECT_EQ(params.hi_hat_decay, 25);
}

TEST(PadParams, ComparesEveryField) {
  const PadParams params;
  EXPECT_EQ(params, PadParams());

  PadParams changed = params;
  changed.hi_hat_decay = params.hi_hat_decay + 1;
  EXPECT_NE(changed, params);
}

// ---- LayerSample: the dual identity (nothing / local file / pool wave) ----

TEST(LayerSample, DefaultHoldsNothing) {
  const LayerSample sample;
  EXPECT_TRUE(sample.empty());
  EXPECT_FALSE(sample.is_file());
  EXPECT_FALSE(sample.is_device());
}

TEST(LayerSample, HoldsALocalFile) {
  const LayerSample sample {juce::File("/tmp/kick.wav")};
  EXPECT_FALSE(sample.empty());
  EXPECT_TRUE(sample.is_file());
  EXPECT_FALSE(sample.is_device());
  EXPECT_EQ(sample.file, juce::File("/tmp/kick.wav"));
}

TEST(LayerSample, HoldsADevicePoolWave) {
  const LayerSample sample = LayerSample::DeviceWave(1590);
  EXPECT_FALSE(sample.empty());
  EXPECT_FALSE(sample.is_file());
  EXPECT_TRUE(sample.is_device());
  EXPECT_EQ(sample.device_index, 1590);
}

// Pool index 0 means "no wave" on the device, so it degrades to empty rather
// than to a device reference.
TEST(LayerSample, DeviceWaveZeroIsEmpty) {
  const LayerSample sample = LayerSample::DeviceWave(0);
  EXPECT_TRUE(sample.empty());
  EXPECT_FALSE(sample.is_device());
}

// ---- KitModel defaults ----

TEST(KitModel, StartsUntitled) {
  EXPECT_EQ(KitModel().name(), juce::String("Untitled Kit"));
}

TEST(KitModel, LastPadDefaultsToHiHatTheRestToMix) {
  const KitModel model;
  for (int pad = 0; pad < KitModel::kPadCount - 1; ++pad) {
    EXPECT_EQ(model.params(pad).mode, LayerMode::kMix) << "pad " << pad;
    EXPECT_EQ(KitModel::DefaultParams(pad).mode, LayerMode::kMix)
        << "pad " << pad;
  }
  EXPECT_EQ(model.params(KitModel::kPadCount - 1).mode, LayerMode::kHiHat);
  EXPECT_EQ(KitModel::DefaultParams(KitModel::kPadCount - 1).mode,
            LayerMode::kHiHat);
}

TEST(KitModel, StartsWithEveryLayerEmpty) {
  const KitModel model;
  for (int pad = 0; pad < KitModel::kPadCount; ++pad) {
    for (int layer = 0; layer < KitModel::kLayersPerPad; ++layer) {
      EXPECT_TRUE(model.sample(pad, layer).empty())
          << "pad " << pad << " layer " << layer;
    }
  }
}

TEST(KitModel, PadIndexIsBoundsChecked) {
  const KitModel model;
  EXPECT_THROW((void)model.pad(KitModel::kPadCount), std::out_of_range);
  EXPECT_THROW((void)model.pad(-1), std::out_of_range);
}

// ---- Mutation and notification ----

TEST_F(KitModelTest, SetNameStoresAndNotifies) {
  model.set_name("ZZZ");
  EXPECT_EQ(model.name(), juce::String("ZZZ"));
  EXPECT_EQ(listener.name_changes, 1);
}

// The no-op guards are what keep undo transactions and the document's dirty
// flag from firing on writes that change nothing.
TEST_F(KitModelTest, SettingTheSameNameDoesNotNotify) {
  model.set_name("ZZZ");
  model.set_name("ZZZ");
  EXPECT_EQ(listener.name_changes, 1);
}

TEST_F(KitModelTest, SetSampleStoresAndNotifiesWithPadAndLayer) {
  const LayerSample sample = LayerSample::DeviceWave(7);
  model.set_sample(3, 1, sample);

  EXPECT_EQ(model.sample(3, 1), sample);
  ASSERT_EQ(listener.samples.size(), 1u);
  EXPECT_EQ(listener.samples.front(), std::make_pair(3, 1));
}

TEST_F(KitModelTest, SettingAnIdenticalSampleDoesNotNotify) {
  const LayerSample sample {juce::File("/tmp/snare.wav")};
  model.set_sample(0, 0, sample);
  model.set_sample(0, 0, sample);
  EXPECT_EQ(listener.samples.size(), 1u);
}

TEST_F(KitModelTest, LayerZeroIsTopAndLayerOneIsBottom) {
  const LayerSample top = LayerSample::DeviceWave(1);
  const LayerSample bottom = LayerSample::DeviceWave(2);
  model.set_sample(4, 0, top);
  model.set_sample(4, 1, bottom);

  EXPECT_EQ(model.sample(4, 0), top);
  EXPECT_EQ(model.sample(4, 1), bottom);
  EXPECT_EQ(model.pad(4).samples.first, top);
  EXPECT_EQ(model.pad(4).samples.second, bottom);
}

TEST_F(KitModelTest, SetPadParamsStoresAndNotifies) {
  PadParams params = model.params(2);
  params.mode = LayerMode::kXfade;
  params.fade_point = 40;
  model.SetPadParams(2, params);

  EXPECT_EQ(model.params(2), params);
  EXPECT_EQ(model.pad(2).params, params);
  ASSERT_EQ(listener.params.size(), 1u);
  EXPECT_EQ(listener.params.front(), 2);
}

// A gesture that changes several fields is still one undo step, one event.
TEST_F(KitModelTest, SetPadParamsNotifiesOncePerCall) {
  PadParams params = model.params(0);
  params.dynamics = false;
  params.curve = DynamicsCurve::kLoud3;
  params.fixed_velocity = 100;
  model.SetPadParams(0, params);

  EXPECT_EQ(listener.params.size(), 1u);
}

TEST_F(KitModelTest, SettingIdenticalParamsDoesNotNotify) {
  model.SetPadParams(1, model.params(1));
  EXPECT_TRUE(listener.params.empty());
}

TEST_F(KitModelTest, PadsAreIndependent) {
  PadParams params = model.params(0);
  params.mode = LayerMode::kAlternate;
  model.SetPadParams(0, params);

  EXPECT_EQ(model.params(1).mode, LayerMode::kMix);
  EXPECT_TRUE(model.sample(1, 0).empty());
}

// Listener's methods default to no-ops, so a subclass overrides only the
// events it cares about -- a listener that overrides nothing still has to
// survive every event the model emits.
TEST(KitModel, ListenersMayOverrideNothing) {
  KitModel model;
  KitModel::Listener bare;
  model.AddListener(&bare);

  model.set_name("ZZZ");
  model.set_sample(0, 0, LayerSample::DeviceWave(1));
  PadParams params = model.params(0);
  params.mode = LayerMode::kSwitch;
  model.SetPadParams(0, params);

  model.RemoveListener(&bare);
  EXPECT_EQ(model.name(), juce::String("ZZZ"));
}

TEST_F(KitModelTest, RemovedListenersStopHearingChanges) {
  model.RemoveListener(&listener);
  model.set_name("silent");
  model.set_sample(0, 0, LayerSample::DeviceWave(3));

  EXPECT_EQ(listener.name_changes, 0);
  EXPECT_TRUE(listener.samples.empty());

  model.AddListener(&listener);  // TearDown removes it again.
}

}  // namespace
}  // namespace spdsx
