#include "device_db.h"

#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <sqlite3.h>

#include "temp_dir.h"

namespace spdsx {
namespace {

// A model with something distinctive in every field the store persists.
DeviceModel EditedModel() {
  DeviceModel model;
  model.kit(0).name = "FIRST";
  model.kit(DeviceModel::kKitCount - 1).name = "LAST";

  KitData& kit = model.kit(7);
  kit.name = "EDITED";
  Pad& pad = kit.pads[2];
  pad.params.mode = LayerMode::kXfade;
  pad.params.fade_point = 40;
  pad.params.fade_end = 90;
  pad.params.dynamics = false;
  pad.params.curve = DynamicsCurve::kLoud2;
  pad.params.fixed_velocity = 64;
  pad.params.trigger_reserve = true;
  pad.params.hi_hat_volume = 10;
  pad.params.hi_hat_fade_in = 20;
  pad.params.hi_hat_decay = 30;
  // Both halves of the dual identity, so neither read path is missed.
  pad.samples.first = LayerSample::DeviceWave(1590);
  pad.samples.second = LayerSample(juce::File("/tmp/snare.wav"));

  model.set_current_kit(7);
  return model;
}

// Reaches around the API to plant what a corrupt, foreign, or future-schema
// document could hold, and to break the store under a write. DeviceDb offers
// no SQL surface of its own, and these defensive paths have no other trigger.
void ExecSql(const juce::File& path, const char* sql) {
  sqlite3* raw = nullptr;
  ASSERT_EQ(sqlite3_open(path.getFullPathName().toRawUTF8(), &raw), SQLITE_OK);
  char* err = nullptr;
  const int rc = sqlite3_exec(raw, sql, nullptr, nullptr, &err);
  const std::string message = err != nullptr ? err : "";
  sqlite3_free(err);
  sqlite3_close(raw);
  ASSERT_EQ(rc, SQLITE_OK) << message;
}

void ExpectKitsEqual(const DeviceModel& actual, const DeviceModel& expected) {
  for (int k = 0; k < DeviceModel::kKitCount; ++k) {
    EXPECT_EQ(actual.kit(k).name, expected.kit(k).name) << "kit " << k;
    for (int p = 0; p < KitModel::kPadCount; ++p) {
      const Pad& a = actual.kit(k).pads[static_cast<size_t>(p)];
      const Pad& e = expected.kit(k).pads[static_cast<size_t>(p)];
      EXPECT_EQ(a.params, e.params) << "kit " << k << " pad " << p;
      EXPECT_EQ(a.samples.first, e.samples.first)
          << "kit " << k << " pad " << p;
      EXPECT_EQ(a.samples.second, e.samples.second)
          << "kit " << k << " pad " << p;
    }
  }
}

class DeviceDbTest : public ::testing::Test {
protected:
  void SetUp() override {
    juce::String error;
    db = DeviceDb::Open(path(), error);
    ASSERT_NE(db, nullptr) << error;
  }

  juce::File path() const { return temp.file("test.spdsx"); }

  spdsx_testing::TempDir temp;
  std::unique_ptr<DeviceDb> db;
};

// ---- Open ----

TEST_F(DeviceDbTest, OpenCreatesTheFileAndStampsTheSchemaVersion) {
  EXPECT_TRUE(path().existsAsFile());
  EXPECT_EQ(db->SchemaVersion(), DeviceDb::kCurrentSchemaVersion);
}

TEST_F(DeviceDbTest, OpenReusesAnExistingDatabase) {
  db->WriteKits(EditedModel());
  db.reset();  // close

  juce::String error;
  auto reopened = DeviceDb::Open(path(), error);
  ASSERT_NE(reopened, nullptr) << error;

  DeviceModel model;
  reopened->ReadKits(model);
  EXPECT_EQ(model.kit(7).name, juce::String("EDITED"));
}

TEST_F(DeviceDbTest, OpenReportsAPathItCannotUse) {
  juce::String error;
  auto failed = DeviceDb::Open(temp.file("no/such/dir/x.spdsx"), error);
  EXPECT_EQ(failed, nullptr);
  EXPECT_TRUE(error.isNotEmpty());
}

// The version records the format the file was written in, so opening a
// document must never restamp it: that would destroy the only signal that it
// came from a newer build, leaving this build to misread it as its own.
TEST_F(DeviceDbTest, OpenNeverRestampsTheSchemaVersion) {
  ExecSql(path(), "UPDATE meta SET value='99' WHERE key='schema_version';");
  db.reset();  // close

  juce::String error;
  auto reopened = DeviceDb::Open(path(), error);
  ASSERT_NE(reopened, nullptr) << error;
  EXPECT_EQ(reopened->SchemaVersion(), 99);
}

TEST_F(DeviceDbTest, OpenRejectsAFileThatIsNotADatabase) {
  const juce::File junk = temp.file("junk.spdsx");
  junk.replaceWithText("this is not a database");

  juce::String error;
  auto failed = DeviceDb::Open(junk, error);
  EXPECT_EQ(failed, nullptr);
  EXPECT_TRUE(error.contains("schema init failed")) << error;
}

// ---- Kits round trip ----

TEST_F(DeviceDbTest, KitsRoundTrip) {
  const DeviceModel written = EditedModel();
  db->WriteKits(written);

  DeviceModel read;
  db->ReadKits(read);

  ExpectKitsEqual(read, written);
  EXPECT_EQ(read.current_kit(), 7);
}

TEST_F(DeviceDbTest, WriteKitsReplacesRatherThanAccumulates) {
  db->WriteKits(EditedModel());

  DeviceModel second;
  second.kit(7).name = "SECOND";
  db->WriteKits(second);

  DeviceModel read;
  db->ReadKits(read);
  EXPECT_EQ(read.kit(7).name, juce::String("SECOND"));
  EXPECT_EQ(read.kit(0).name, juce::String("USER KIT"));
  ExpectKitsEqual(read, second);
}

// Anything the database does not hold reads back as a clean USER KIT, so a
// partial or empty document can't leave stale state in the model.
TEST_F(DeviceDbTest, ReadKitsStartsFromDefaults) {
  DeviceModel model = EditedModel();
  db->ReadKits(model);  // nothing written yet

  ExpectKitsEqual(model, DeviceModel());
}

// ---- Defensive reads and error surfacing ----

// A row naming a kit or pad outside the model's range -- a corrupt file, or
// one from a build with a bigger model -- is skipped, not trusted.
TEST_F(DeviceDbTest, ReadKitsIgnoresRowsOutsideTheModel) {
  db->WriteKits(DeviceModel());
  ExecSql(path(),
          "INSERT INTO kits(snapshot,idx,name) VALUES('current',9999,'GHOST');"
          "INSERT INTO pads(snapshot,kit_idx,pad_idx,mode) VALUES"
          "('current',9999,0,3),('current',0,99,3);");

  DeviceModel read;
  db->ReadKits(read);

  ExpectKitsEqual(read, DeviceModel());
}

// Each write is one transaction: a failure part-way rolls back and surfaces
// the error rather than leaving a half-written document behind.
TEST_F(DeviceDbTest, WriteKitsSurfacesStoreErrors) {
  ExecSql(path(), "DROP TABLE pads;");
  EXPECT_THROW(db->WriteKits(DeviceModel()), std::runtime_error);
}

TEST_F(DeviceDbTest, WritePoolSurfacesStoreErrors) {
  ExecSql(path(), "DROP TABLE samples;");

  DeviceModel model;
  device::SampleRecord rec;
  rec.index = 1;
  model.set_sample_pool({rec});
  EXPECT_THROW(db->WritePool(model), std::runtime_error);
}

TEST_F(DeviceDbTest, CaptureBaseSurfacesStoreErrors) {
  ExecSql(path(), "DROP TABLE pads;");
  EXPECT_THROW(db->CaptureBase(), std::runtime_error);
}

// ---- Defensive reads: the store clamps what it loads ----

TEST_F(DeviceDbTest, ReadKitsClampsOutOfRangeParams) {
  DeviceModel model;
  PadParams& params = model.kit(0).pads[0].params;
  params.mode = static_cast<LayerMode>(99);
  params.curve = static_cast<DynamicsCurve>(99);
  params.fade_point = 999;
  params.fade_end = 999;
  params.fixed_velocity = 999;
  params.hi_hat_volume = 999;
  params.hi_hat_fade_in = -5;
  params.hi_hat_decay = 999;
  db->WriteKits(model);

  DeviceModel read;
  db->ReadKits(read);

  const PadParams& got = read.kit(0).pads[0].params;
  EXPECT_EQ(got.mode, static_cast<LayerMode>(kLayerModeCount - 1));
  EXPECT_EQ(got.curve, static_cast<DynamicsCurve>(kDynamicsCurveCount - 1));
  EXPECT_EQ(got.fade_point, 127);
  EXPECT_EQ(got.fade_end, 127);
  EXPECT_EQ(got.fixed_velocity, 127);
  EXPECT_EQ(got.hi_hat_volume, 127);
  EXPECT_EQ(got.hi_hat_fade_in, 0);
  EXPECT_EQ(got.hi_hat_decay, 127);
}

// The device constrains fade end >= fade point; the read enforces it rather
// than trusting the file.
TEST_F(DeviceDbTest, ReadKitsHoldsFadeEndAtOrAboveFadePoint) {
  DeviceModel model;
  model.kit(0).pads[0].params.fade_point = 100;
  model.kit(0).pads[0].params.fade_end = 20;
  db->WriteKits(model);

  DeviceModel read;
  db->ReadKits(read);
  EXPECT_EQ(read.kit(0).pads[0].params.fade_end, 100);
}

TEST_F(DeviceDbTest, ReadKitsClampsTheCurrentKit) {
  DeviceModel model;
  model.set_current_kit(9999);
  db->WriteKits(model);

  DeviceModel read;
  db->ReadKits(read);
  EXPECT_EQ(read.current_kit(), DeviceModel::kKitCount - 1);
}

// ---- Snapshots ----

TEST_F(DeviceDbTest, CurrentAndBaseSnapshotsAreIndependent) {
  DeviceModel current;
  current.kit(3).name = "CURRENT";
  db->WriteKits(current, Snapshot::kCurrent);

  DeviceModel base;
  base.kit(3).name = "BASE";
  db->WriteKits(base, Snapshot::kBase);

  DeviceModel read_current;
  db->ReadKits(read_current, Snapshot::kCurrent);
  EXPECT_EQ(read_current.kit(3).name, juce::String("CURRENT"));

  DeviceModel read_base;
  db->ReadKits(read_base, Snapshot::kBase);
  EXPECT_EQ(read_base.kit(3).name, juce::String("BASE"));
}

// current_kit and the pool are properties of the working document, not of a
// sync baseline, so the base snapshot leaves them alone.
TEST_F(DeviceDbTest, TheBaseSnapshotCarriesNeitherCurrentKitNorPool) {
  db->WriteKits(EditedModel(), Snapshot::kBase);

  DeviceModel read;
  read.set_current_kit(42);
  read.set_sample_pool({[] {
    device::SampleRecord rec;
    rec.index = 1;
    rec.wavename = "Kick";
    return rec;
  }()});
  db->ReadKits(read, Snapshot::kBase);

  EXPECT_EQ(read.kit(7).name, juce::String("EDITED"));
  EXPECT_EQ(read.current_kit(), 42);
  EXPECT_EQ(read.sample_pool().size(), 1u);
}

TEST_F(DeviceDbTest, CaptureBaseCopiesCurrentOntoBase) {
  const DeviceModel written = EditedModel();
  db->WriteKits(written);
  db->CaptureBase();

  DeviceModel base;
  db->ReadKits(base, Snapshot::kBase);
  ExpectKitsEqual(base, written);
}

TEST_F(DeviceDbTest, CaptureBaseReplacesAnEarlierBase) {
  DeviceModel first;
  first.kit(0).name = "FIRST";
  db->WriteKits(first);
  db->CaptureBase();

  DeviceModel second;
  second.kit(0).name = "SECOND";
  db->WriteKits(second);
  db->CaptureBase();

  DeviceModel base;
  db->ReadKits(base, Snapshot::kBase);
  EXPECT_EQ(base.kit(0).name, juce::String("SECOND"));
  ExpectKitsEqual(base, second);
}

// ---- The sample pool ----

TEST_F(DeviceDbTest, ThePoolRoundTrips) {
  DeviceModel model;
  device::SampleRecord kick;
  kick.index = 1;
  kick.wavename = "Kick";
  kick.filename = "kick.wav";
  kick.frames = 48000;
  kick.category = 3;
  model.set_sample_pool({kick});
  db->WritePool(model);

  DeviceModel read;
  db->ReadKits(read);

  ASSERT_EQ(read.sample_pool().size(), 1u);
  const device::SampleRecord& got = read.sample_pool().front();
  EXPECT_EQ(got.index, 1);
  EXPECT_EQ(got.wavename, "Kick");
  EXPECT_EQ(got.filename, "kick.wav");
  EXPECT_EQ(got.frames, 48000u);
  EXPECT_EQ(got.category, 3);
}

TEST_F(DeviceDbTest, ThePoolReadsBackInIndexOrder) {
  DeviceModel model;
  std::vector<device::SampleRecord> pool;
  for (const int index : {1590, 7, 1}) {
    device::SampleRecord rec;
    rec.index = index;
    pool.push_back(rec);
  }
  model.set_sample_pool(std::move(pool));
  db->WritePool(model);

  DeviceModel read;
  db->ReadKits(read);

  ASSERT_EQ(read.sample_pool().size(), 3u);
  EXPECT_EQ(read.sample_pool()[0].index, 1);
  EXPECT_EQ(read.sample_pool()[1].index, 7);
  EXPECT_EQ(read.sample_pool()[2].index, 1590);
}

// Index 0 is the device's "no sample" sentinel, not a wave.
TEST_F(DeviceDbTest, ThePoolSkipsTheZeroIndexSentinel) {
  DeviceModel model;
  device::SampleRecord sentinel;
  sentinel.index = 0;
  device::SampleRecord real;
  real.index = 1;
  model.set_sample_pool({sentinel, real});
  db->WritePool(model);

  DeviceModel read;
  db->ReadKits(read);

  ASSERT_EQ(read.sample_pool().size(), 1u);
  EXPECT_EQ(read.sample_pool().front().index, 1);
}

// ---- Cached audio blobs ----

TEST_F(DeviceDbTest, AudioBlobsRoundTrip) {
  const std::string audio = "RIFF....WAVEfmt ";
  EXPECT_FALSE(db->HasAudio(5));

  db->PutAudio(5, audio.data(), audio.size());

  EXPECT_TRUE(db->HasAudio(5));
  const juce::MemoryBlock got = db->GetAudio(5);
  ASSERT_EQ(got.getSize(), audio.size());
  EXPECT_EQ(std::memcmp(got.getData(), audio.data(), audio.size()), 0);
}

TEST_F(DeviceDbTest, PutAudioReplacesAnEarlierBlob) {
  const std::string first = "first";
  const std::string second = "second-and-longer";
  db->PutAudio(5, first.data(), first.size());
  db->PutAudio(5, second.data(), second.size());

  const juce::MemoryBlock got = db->GetAudio(5);
  ASSERT_EQ(got.getSize(), second.size());
  EXPECT_EQ(std::memcmp(got.getData(), second.data(), second.size()), 0);
}

TEST_F(DeviceDbTest, AudioIsAbsentForAnUncachedSample) {
  EXPECT_FALSE(db->HasAudio(404));
  EXPECT_TRUE(db->GetAudio(404).isEmpty());
}

// A pool refresh (from a device read) must not throw away audio already
// downloaded for those indices.
TEST_F(DeviceDbTest, WritePoolKeepsCachedAudio) {
  const std::string audio = "cached-bytes";
  db->PutAudio(1, audio.data(), audio.size());

  DeviceModel model;
  device::SampleRecord rec;
  rec.index = 1;
  rec.wavename = "Renamed";
  model.set_sample_pool({rec});
  db->WritePool(model);

  EXPECT_TRUE(db->HasAudio(1));
  const juce::MemoryBlock got = db->GetAudio(1);
  ASSERT_EQ(got.getSize(), audio.size());
  EXPECT_EQ(std::memcmp(got.getData(), audio.data(), audio.size()), 0);

  DeviceModel read;
  db->ReadKits(read);
  ASSERT_EQ(read.sample_pool().size(), 1u);
  EXPECT_EQ(read.sample_pool().front().wavename, "Renamed");
}

TEST_F(DeviceDbTest, WritePoolUpdatesMetadataInPlace) {
  DeviceModel model;
  device::SampleRecord rec;
  rec.index = 1;
  rec.wavename = "Before";
  rec.frames = 1;
  model.set_sample_pool({rec});
  db->WritePool(model);

  rec.wavename = "After";
  rec.frames = 2;
  model.set_sample_pool({rec});
  db->WritePool(model);

  DeviceModel read;
  db->ReadKits(read);
  ASSERT_EQ(read.sample_pool().size(), 1u);
  EXPECT_EQ(read.sample_pool().front().wavename, "After");
  EXPECT_EQ(read.sample_pool().front().frames, 2u);
}

}  // namespace
}  // namespace spdsx
