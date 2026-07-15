#include "device_model.h"

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace spdsx {
namespace {

device::SampleRecord Record(int index, std::string wavename)
{
  device::SampleRecord record;
  record.index = index;
  record.wavename = std::move(wavename);
  return record;
}

// FindSample binary-searches, so the pool must be in index order.
std::vector<device::SampleRecord> Pool()
{
  return {Record(1, "Kick"), Record(7, "Snare"), Record(1590, "A_sine")};
}

// ---- KitData ----

TEST(KitData, DefaultsToTheDevicesOwnKitName)
{
  EXPECT_EQ(KitData().name, juce::String("USER KIT"));
}

// KitData and KitModel both seed from KitModel::DefaultParams, so a stored
// kit and a freshly edited one start out agreeing.
TEST(KitData, PadDefaultsMatchTheKitModels)
{
  const KitData data;
  const KitModel model;
  for (int pad = 0; pad < KitModel::kPadCount; ++pad) {
    EXPECT_EQ(data.pads[static_cast<size_t>(pad)].params, model.params(pad))
        << "pad " << pad;
    EXPECT_TRUE(data.pads[static_cast<size_t>(pad)].samples.first.empty());
    EXPECT_TRUE(data.pads[static_cast<size_t>(pad)].samples.second.empty());
  }
}

// ---- DeviceModel: the kit array ----

TEST(DeviceModel, HoldsTheDevicesTwoHundredKits)
{
  const DeviceModel model;
  EXPECT_EQ(DeviceModel::kKitCount, 200);
  EXPECT_EQ(model.kit(0).name, juce::String("USER KIT"));
  EXPECT_EQ(model.kit(DeviceModel::kKitCount - 1).name,
            juce::String("USER KIT"));
}

TEST(DeviceModel, KitIndexIsBoundsChecked)
{
  DeviceModel model;
  EXPECT_THROW((void)model.kit(DeviceModel::kKitCount), std::out_of_range);
  EXPECT_THROW((void)model.kit(-1), std::out_of_range);
}

TEST(DeviceModel, KitsAreMutableAndIndependent)
{
  DeviceModel model;
  model.kit(129).name = "TRACER XYZZY";

  EXPECT_EQ(model.kit(129).name, juce::String("TRACER XYZZY"));
  EXPECT_EQ(model.kit(128).name, juce::String("USER KIT"));
  EXPECT_EQ(model.kit(130).name, juce::String("USER KIT"));
}

// ---- DeviceModel: the active kit (view state) ----

TEST(DeviceModel, StartsOnTheFirstKit)
{
  EXPECT_EQ(DeviceModel().current_kit(), 0);
}

TEST(DeviceModel, RemembersTheCurrentKit)
{
  DeviceModel model;
  model.set_current_kit(199);
  EXPECT_EQ(model.current_kit(), 199);
}

// ---- DeviceModel: the sample pool ----

TEST(DeviceModel, StartsWithAnEmptyPool)
{
  EXPECT_TRUE(DeviceModel().sample_pool().empty());
}

TEST(DeviceModel, StoresTheSamplePool)
{
  DeviceModel model;
  model.set_sample_pool(Pool());

  ASSERT_EQ(model.sample_pool().size(), 3u);
  EXPECT_EQ(model.sample_pool().front().wavename, "Kick");
}

TEST(DeviceModel, FindSampleReturnsTheRecordForAPoolIndex)
{
  DeviceModel model;
  model.set_sample_pool(Pool());

  const device::SampleRecord* found = model.FindSample(7);
  ASSERT_NE(found, nullptr);
  EXPECT_EQ(found->index, 7);
  EXPECT_EQ(found->wavename, "Snare");
  // The record lives in the pool rather than being a copy.
  EXPECT_EQ(found, &model.sample_pool()[1]);
}

// The pool is sparse: it lists only the named waves, so an index between
// two entries is absent rather than nearby.
TEST(DeviceModel, FindSampleRejectsIndicesThePoolDoesNotList)
{
  DeviceModel model;
  model.set_sample_pool(Pool());

  EXPECT_EQ(model.FindSample(2), nullptr);  // a gap
  EXPECT_EQ(model.FindSample(0), nullptr);  // below the first
  EXPECT_EQ(model.FindSample(20000), nullptr);  // past the last
}

TEST(DeviceModel, FindSampleOnAnEmptyPoolFindsNothing)
{
  EXPECT_EQ(DeviceModel().FindSample(1), nullptr);
}

// ---- DeviceModel::Reset ----

TEST(DeviceModel, ResetRestoresEveryKitThePoolAndTheCurrentKit)
{
  DeviceModel model;
  model.kit(5).name = "EDITED";
  model.kit(5).pads[0].samples.first = LayerSample::DeviceWave(7);
  model.set_sample_pool(Pool());
  model.set_current_kit(42);

  model.Reset();

  EXPECT_EQ(model.kit(5).name, juce::String("USER KIT"));
  EXPECT_TRUE(model.kit(5).pads[0].samples.first.empty());
  EXPECT_TRUE(model.sample_pool().empty());
  EXPECT_EQ(model.current_kit(), 0);
  EXPECT_EQ(model.kit(DeviceModel::kKitCount - 1).name,
            juce::String("USER KIT"));
}

}  // namespace
}  // namespace spdsx
