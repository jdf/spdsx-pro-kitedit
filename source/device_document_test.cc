#include "device_document.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <sqlite3.h>

#include "temp_dir.h"

namespace spdsx {
namespace {

// loadDocument/saveDocument are protected: FileBasedDocument's synchronous
// save/load are compiled out in JUCE 8, so the app drives them through
// OpenDevice/CreateNew/Autosave and the tests reach them here.
class TestDocument : public DeviceDocument {
public:
  using DeviceDocument::DeviceDocument;
  using DeviceDocument::getLastDocumentOpened;
  using DeviceDocument::loadDocument;
  using DeviceDocument::saveDocument;
  using DeviceDocument::setLastDocumentOpened;
};

// A device kit record carrying values the parser would really produce.
device::KitRecord DeviceKit(std::string name) {
  device::KitRecord rec;
  rec.name = std::move(name);
  for (auto& pad : rec.pads) {
    pad.fade_point = 80;
    pad.fade_end = 127;
    pad.dynamics = 1;
    pad.fixed_velocity = 127;
  }
  return rec;
}

void ExecSql(const juce::File& path, const char* sql) {
  sqlite3* raw = nullptr;
  ASSERT_EQ(sqlite3_open(path.getFullPathName().toRawUTF8(), &raw), SQLITE_OK);
  ASSERT_EQ(sqlite3_exec(raw, sql, nullptr, nullptr, nullptr), SQLITE_OK);
  sqlite3_close(raw);
}

class DeviceDocumentTest : public ::testing::Test {
protected:
  void SetUp() override {
    juce::PropertiesFile::Options options;
    options.applicationName = "spdsx-patchedit-test";
    options.filenameSuffix = ".settings";
    // "Application Support" keeps JUCE's own path assertion quiet; the
    // absolute folderName is what actually redirects the file.
    options.osxLibrarySubFolder = "Application Support";
    options.folderName = temp.dir().getFullPathName();
    settings.setStorageParameters(options);
    // Guard: a test must never touch the real user preferences.
    ASSERT_TRUE(settings.getUserSettings()->getFile().isAChildOf(temp.dir()));

    doc = std::make_unique<TestDocument>(device, model, settings);
    doc->on_history_reset = [this] { ++history_resets; };
    doc->on_model_reload = [this](bool loading) { reloads.push_back(loading); };
  }

  void TearDown() override {
    // CachedWaveFile extracts into the shared temp cache, not our TempDir.
    WaveCache().deleteRecursively();
  }

  static juce::File WaveCache() {
    return juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("spdsx-wavecache");
  }

  juce::File path() const { return temp.file("dev.spdsx"); }

  spdsx_testing::TempDir temp;
  juce::ApplicationProperties settings;
  DeviceModel device;
  KitModel model;
  std::unique_ptr<TestDocument> doc;
  int history_resets = 0;
  std::vector<bool> reloads;
};

// ---- Title ----

TEST_F(DeviceDocumentTest, IsUntitledUntilItHasAFile) {
  EXPECT_EQ(doc->getDocumentTitle(), juce::String("Untitled Device"));

  ASSERT_TRUE(doc->CreateNew(path()).wasOk());
  EXPECT_EQ(doc->getDocumentTitle(), juce::String("dev"));
}

// ---- CreateNew ----

TEST_F(DeviceDocumentTest, CreateNewWritesAFileAndStartsClean) {
  ASSERT_TRUE(doc->CreateNew(path()).wasOk());

  EXPECT_TRUE(path().existsAsFile());
  EXPECT_EQ(doc->getFile(), path());
  EXPECT_FALSE(doc->hasChangedSinceSaved());
  EXPECT_EQ(history_resets, 1);
  EXPECT_EQ(doc->getLastDocumentOpened(), path());
}

TEST_F(DeviceDocumentTest, CreateNewReplacesAnExistingDocument) {
  ASSERT_TRUE(doc->CreateNew(path()).wasOk());
  model.set_name("EDITED");
  doc->Autosave();

  ASSERT_TRUE(doc->CreateNew(path()).wasOk());

  EXPECT_EQ(device.kit(0).name, juce::String("USER KIT"));
  EXPECT_EQ(model.name(), juce::String("USER KIT"));
}

// A legacy folder-package document could be sitting at the same path.
TEST_F(DeviceDocumentTest, CreateNewReplacesALegacyFolderAtThePath) {
  ASSERT_TRUE(path().createDirectory().wasOk());
  path().getChildFile("device.json").replaceWithText("{}");
  ASSERT_TRUE(path().isDirectory());

  ASSERT_TRUE(doc->CreateNew(path()).wasOk());

  EXPECT_TRUE(path().existsAsFile());
  EXPECT_FALSE(path().isDirectory());
}

TEST_F(DeviceDocumentTest, CreateNewReportsAPathItCannotUse) {
  EXPECT_TRUE(doc->CreateNew(temp.file("no/such/dir/x.spdsx")).failed());
}

// ---- Stashing the active kit ----

TEST_F(DeviceDocumentTest, StashActiveKitCopiesTheLiveModelIntoTheDevice) {
  model.set_name("LIVE");
  model.set_sample(2, 1, LayerSample::DeviceWave(9));
  PadParams params = model.params(2);
  params.mode = LayerMode::kSwitch;
  model.SetPadParams(2, params);

  doc->StashActiveKit();

  const KitData& kit = device.kit(device.current_kit());
  EXPECT_EQ(kit.name, juce::String("LIVE"));
  EXPECT_EQ(kit.pads[2].samples.second, LayerSample::DeviceWave(9));
  EXPECT_EQ(kit.pads[2].params.mode, LayerMode::kSwitch);
}

// ---- SwitchKit ----

TEST_F(DeviceDocumentTest, SwitchKitStashesTheOldKitAndLoadsTheNew) {
  model.set_name("KIT ZERO");
  device.kit(5).name = "KIT FIVE";

  doc->SwitchKit(5);

  EXPECT_EQ(device.current_kit(), 5);
  EXPECT_EQ(model.name(), juce::String("KIT FIVE"));
  EXPECT_EQ(device.kit(0).name, juce::String("KIT ZERO"));

  doc->SwitchKit(0);
  EXPECT_EQ(model.name(), juce::String("KIT ZERO"));
}

// Switching kits is view state, so it must not dirty a clean document nor
// clean a dirty one -- even though reloading the model fires change
// listeners that would otherwise mark it edited.
TEST_F(DeviceDocumentTest, SwitchKitPreservesTheChangedFlag) {
  doc->setChangedFlag(false);
  doc->SwitchKit(3);
  EXPECT_FALSE(doc->hasChangedSinceSaved());

  doc->setChangedFlag(true);
  doc->SwitchKit(4);
  EXPECT_TRUE(doc->hasChangedSinceSaved());
}

// Undo histories are per-kit and survive a switch.
TEST_F(DeviceDocumentTest, SwitchKitLeavesTheUndoHistoriesAlone) {
  doc->SwitchKit(3);
  EXPECT_EQ(history_resets, 0);
}

// The UI uses the bracket to tell a load's listener storm from a real edit.
TEST_F(DeviceDocumentTest, SwitchKitBracketsTheModelReload) {
  doc->SwitchKit(3);
  EXPECT_EQ(reloads, (std::vector<bool> {true, false}));
}

TEST_F(DeviceDocumentTest, SwitchKitIgnoresTheCurrentAndOutOfRangeKits) {
  doc->SwitchKit(device.current_kit());
  doc->SwitchKit(-1);
  doc->SwitchKit(DeviceModel::kKitCount);

  EXPECT_EQ(device.current_kit(), 0);
  EXPECT_TRUE(reloads.empty());
}

// ---- ResetToUntitled ----

TEST_F(DeviceDocumentTest, ResetToUntitledClearsEverything) {
  ASSERT_TRUE(doc->CreateNew(path()).wasOk());
  model.set_name("EDITED");
  doc->StashActiveKit();
  doc->setChangedFlag(true);
  history_resets = 0;

  doc->ResetToUntitled();

  EXPECT_EQ(doc->getFile(), juce::File());
  EXPECT_FALSE(doc->hasChangedSinceSaved());
  EXPECT_EQ(device.kit(0).name, juce::String("USER KIT"));
  EXPECT_EQ(model.name(), juce::String("USER KIT"));
  EXPECT_EQ(history_resets, 1);
}

// ---- Autosave / OpenDevice round trip ----

TEST_F(DeviceDocumentTest, AutosavePersistsTheLiveKitAndMarksItSaved) {
  ASSERT_TRUE(doc->CreateNew(path()).wasOk());
  model.set_name("AUTOSAVED");
  doc->setChangedFlag(true);

  doc->Autosave();

  EXPECT_FALSE(doc->hasChangedSinceSaved());

  DeviceModel reopened_device;
  KitModel reopened_model;
  TestDocument reopened(reopened_device, reopened_model, settings);
  ASSERT_TRUE(reopened.OpenDevice(path()).wasOk());
  EXPECT_EQ(reopened_model.name(), juce::String("AUTOSAVED"));
}

// Only the --load test runs reach the untitled state; it must not crash.
TEST_F(DeviceDocumentTest, AutosaveWhileUntitledDoesNothing) {
  model.set_name("NOWHERE TO GO");
  doc->Autosave();
  EXPECT_EQ(doc->getFile(), juce::File());
}

TEST_F(DeviceDocumentTest, OpenDeviceStartsCleanAndRemembersThePath) {
  ASSERT_TRUE(doc->CreateNew(path()).wasOk());
  doc->Autosave();

  DeviceModel other_device;
  KitModel other_model;
  TestDocument other(other_device, other_model, settings);
  ASSERT_TRUE(other.OpenDevice(path()).wasOk());

  EXPECT_EQ(other.getFile(), path());
  EXPECT_FALSE(other.hasChangedSinceSaved());
  EXPECT_EQ(other.getLastDocumentOpened(), path());
}

TEST_F(DeviceDocumentTest, OpenDeviceReportsAFileItCannotRead) {
  const juce::File junk = temp.file("junk.spdsx");
  junk.replaceWithText("not a database");

  const juce::Result result = doc->OpenDevice(junk);
  EXPECT_TRUE(result.failed());
}

// The loader refuses a document from a newer build rather than silently
// misreading it.
TEST_F(DeviceDocumentTest, OpenDeviceRefusesANewerSchema) {
  ASSERT_TRUE(doc->CreateNew(path()).wasOk());
  ExecSql(path(), "UPDATE meta SET value='99' WHERE key='schema_version';");

  DeviceModel other_device;
  KitModel other_model;
  TestDocument other(other_device, other_model, settings);
  const juce::Result result = other.OpenDevice(path());

  ASSERT_TRUE(result.failed());
  EXPECT_TRUE(result.getErrorMessage().contains("newer version"))
      << result.getErrorMessage();
}

// ---- saveDocument: Save As carries the database, blobs and all ----

TEST_F(DeviceDocumentTest, SaveDocumentToANewPathCarriesTheCachedAudio) {
  ASSERT_TRUE(doc->CreateNew(path()).wasOk());
  const juce::MemoryBlock wav("cached-bytes", 12);
  doc->StoreWaveAudio(3, wav);
  model.set_name("MOVED");

  const juce::File moved = temp.file("moved.spdsx");
  ASSERT_TRUE(doc->saveDocument(moved).wasOk());

  EXPECT_TRUE(moved.existsAsFile());
  EXPECT_TRUE(doc->HasCachedAudio(3));

  DeviceModel other_device;
  KitModel other_model;
  TestDocument other(other_device, other_model, settings);
  ASSERT_TRUE(other.OpenDevice(moved).wasOk());
  EXPECT_EQ(other_model.name(), juce::String("MOVED"));
  EXPECT_TRUE(other.HasCachedAudio(3));
}

TEST_F(DeviceDocumentTest, SaveDocumentReportsAPathItCannotOpen) {
  // Untitled: nothing to carry across, so this is the plain open failure.
  EXPECT_TRUE(doc->saveDocument(temp.file("no/such/dir/x.spdsx")).failed());
}

// Save As has to carry the existing database across; if the copy cannot be
// made, say so rather than reporting a save that did not happen.
TEST_F(DeviceDocumentTest, SaveDocumentReportsAFailedCopy) {
  ASSERT_TRUE(doc->CreateNew(path()).wasOk());

  const juce::Result result =
      doc->saveDocument(temp.file("no/such/dir/moved.spdsx"));

  ASSERT_TRUE(result.failed());
  EXPECT_TRUE(result.getErrorMessage().contains("couldn't copy document"))
      << result.getErrorMessage();
}

// Carrying the database across means closing it first, so a copy that fails
// must put it back: otherwise the document keeps taking edits for the rest of
// the session while Autosave silently drops every one of them.
TEST_F(DeviceDocumentTest, AFailedSaveAsLeavesTheDocumentStillSaving) {
  ASSERT_TRUE(doc->CreateNew(path()).wasOk());
  ASSERT_TRUE(doc->saveDocument(temp.file("no/such/dir/moved.spdsx")).failed());

  model.set_name("STILL SAVING");
  doc->Autosave();

  DeviceModel other_device;
  KitModel other_model;
  TestDocument other(other_device, other_model, settings);
  ASSERT_TRUE(other.OpenDevice(path()).wasOk());
  EXPECT_EQ(other_model.name(), juce::String("STILL SAVING"));
}

// Worst case: the copy fails and the original has gone too (the volume it
// lived on disappeared). Report that, rather than only the failed copy --
// there is no database to save into any more.
TEST_F(DeviceDocumentTest, SaveDocumentReportsWhenTheOriginalIsGoneToo) {
  const juce::File dir = temp.dir().getChildFile("vanishing");
  ASSERT_TRUE(dir.createDirectory().wasOk());
  ASSERT_TRUE(doc->CreateNew(dir.getChildFile("dev.spdsx")).wasOk());

  dir.deleteRecursively();

  const juce::Result result =
      doc->saveDocument(temp.file("no/such/dir/moved.spdsx"));

  ASSERT_TRUE(result.failed());
  EXPECT_TRUE(result.getErrorMessage().contains("couldn't reopen"))
      << result.getErrorMessage();
}

// ---- Cached wave audio ----

TEST_F(DeviceDocumentTest, CachedAudioRoundTripsAndExtractsAPlayableFile) {
  ASSERT_TRUE(doc->CreateNew(path()).wasOk());
  EXPECT_FALSE(doc->HasCachedAudio(3));

  const juce::MemoryBlock wav("RIFFxxxxWAVE", 12);
  doc->StoreWaveAudio(3, wav);

  EXPECT_TRUE(doc->HasCachedAudio(3));
  const juce::File extracted = doc->CachedWaveFile(3);
  ASSERT_TRUE(extracted.existsAsFile());
  EXPECT_EQ(extracted.getSize(), static_cast<juce::int64>(wav.getSize()));
}

TEST_F(DeviceDocumentTest, CachedWaveFileIsInvalidForAnUncachedWave) {
  ASSERT_TRUE(doc->CreateNew(path()).wasOk());
  EXPECT_EQ(doc->CachedWaveFile(404), juce::File());
}

// A re-download must not keep serving the previous extraction.
TEST_F(DeviceDocumentTest, StoreWaveAudioInvalidatesAStaleExtraction) {
  ASSERT_TRUE(doc->CreateNew(path()).wasOk());
  doc->StoreWaveAudio(3, juce::MemoryBlock("first", 5));
  ASSERT_TRUE(doc->CachedWaveFile(3).existsAsFile());

  doc->StoreWaveAudio(3, juce::MemoryBlock("second-longer", 13));

  const juce::File extracted = doc->CachedWaveFile(3);
  EXPECT_EQ(extracted.loadFileAsString(), juce::String("second-longer"));
}

// There is nowhere to put audio before the document has a database, and
// index 0 is the "no sample" sentinel.
TEST_F(DeviceDocumentTest, AudioIsIgnoredWithoutADatabaseOrAValidIndex) {
  doc->StoreWaveAudio(3, juce::MemoryBlock("x", 1));  // still untitled
  EXPECT_FALSE(doc->HasCachedAudio(3));

  ASSERT_TRUE(doc->CreateNew(path()).wasOk());
  doc->StoreWaveAudio(0, juce::MemoryBlock("x", 1));
  EXPECT_FALSE(doc->HasCachedAudio(0));
  EXPECT_FALSE(doc->HasCachedAudio(-1));
}

// ---- ReplaceWithDeviceState ----

TEST_F(DeviceDocumentTest, ReplaceWithDeviceStateMapsKitsAndPool) {
  ASSERT_TRUE(doc->CreateNew(path()).wasOk());

  device::KitRecord first = DeviceKit("FROM DEVICE");
  first.pads[0].layer_mode = static_cast<uint8_t>(LayerMode::kXfade);
  first.pads[0].dynamics_curve = static_cast<uint8_t>(DynamicsCurve::kLoud2);
  first.pads[0].fade_point = 40;
  first.pads[0].fade_end = 90;
  first.pads[0].fixed_velocity = 64;
  first.pads[0].trigger_reserve = 1;
  first.pads[0].wave_top = 42;

  device::SampleRecord rec;
  rec.index = 42;
  rec.wavename = "Kick";
  doc->ReplaceWithDeviceState({first}, {rec});

  const KitData& kit = device.kit(0);
  EXPECT_EQ(kit.name, juce::String("FROM DEVICE"));
  EXPECT_EQ(kit.pads[0].params.mode, LayerMode::kXfade);
  EXPECT_EQ(kit.pads[0].params.curve, DynamicsCurve::kLoud2);
  EXPECT_EQ(kit.pads[0].params.fade_point, 40);
  EXPECT_EQ(kit.pads[0].params.fade_end, 90);
  EXPECT_EQ(kit.pads[0].params.fixed_velocity, 64);
  EXPECT_TRUE(kit.pads[0].params.trigger_reserve);
  EXPECT_EQ(kit.pads[0].samples.first, LayerSample::DeviceWave(42));
  EXPECT_TRUE(kit.pads[0].samples.second.empty());  // wave_bottom 0 = none
  EXPECT_EQ(model.name(), juce::String("FROM DEVICE"));

  ASSERT_EQ(device.sample_pool().size(), 1u);
  EXPECT_EQ(device.sample_pool().front().index, 42);
}

// The closed-pedal trio lives at kit-record +0x07/08/09 (mapped live
// 2026-07-13); ParseKits reads it and SetPadLayerParams writes it back, so a
// device read must carry it too rather than resetting it to the defaults.
TEST_F(DeviceDocumentTest, ReplaceWithDeviceStateMapsTheHiHatClosedPedalTrio) {
  device::KitRecord rec = DeviceKit("HIHAT");
  rec.pads[8].layer_mode = static_cast<uint8_t>(LayerMode::kHiHat);
  rec.pads[8].hi_hat_volume = 100;
  rec.pads[8].hi_hat_fade_in = 5;
  rec.pads[8].hi_hat_decay = 60;

  doc->ReplaceWithDeviceState({rec}, {});

  const PadParams& params = device.kit(0).pads[8].params;
  EXPECT_EQ(params.mode, LayerMode::kHiHat);
  EXPECT_EQ(params.hi_hat_volume, 100);
  EXPECT_EQ(params.hi_hat_fade_in, 5);
  EXPECT_EQ(params.hi_hat_decay, 60);
}

// The device gave us fewer kits than the model holds: the rest are blank,
// not stale leftovers.
TEST_F(DeviceDocumentTest, ReplaceWithDeviceStateDefaultsTheKitsNotSupplied) {
  ASSERT_TRUE(doc->CreateNew(path()).wasOk());
  device.kit(1).name = "STALE";

  doc->ReplaceWithDeviceState({DeviceKit("ONLY ONE")}, {});

  EXPECT_EQ(device.kit(0).name, juce::String("ONLY ONE"));
  EXPECT_EQ(device.kit(1).name, juce::String("USER KIT"));
  EXPECT_EQ(device.kit(DeviceModel::kKitCount - 1).name,
            juce::String("USER KIT"));
}

// Freshly read from the hardware, so device == current == base: the sync
// baseline is established in the same breath.
TEST_F(DeviceDocumentTest, ReplaceWithDeviceStateCapturesTheSyncBase) {
  ASSERT_TRUE(doc->CreateNew(path()).wasOk());
  history_resets = 0;

  doc->ReplaceWithDeviceState({DeviceKit("FROM DEVICE")}, {});

  EXPECT_EQ(history_resets, 1);  // replaced wholesale, not undoable

  juce::String error;
  auto db = DeviceDb::Open(path(), error);
  ASSERT_NE(db, nullptr) << error;
  DeviceModel base;
  db->ReadKits(base, Snapshot::kBase);
  EXPECT_EQ(base.kit(0).name, juce::String("FROM DEVICE"));
  DeviceModel current;
  db->ReadKits(current, Snapshot::kCurrent);
  EXPECT_EQ(current.kit(0).name, juce::String("FROM DEVICE"));
}

TEST_F(DeviceDocumentTest, ReplaceWithDeviceStateClampsOutOfRangeDeviceValues) {
  device::KitRecord rec = DeviceKit("WILD");
  rec.pads[0].layer_mode = 200;
  rec.pads[0].dynamics_curve = 200;
  rec.pads[0].fade_point = 100;
  rec.pads[0].fade_end = 20;  // below the point
  rec.pads[0].fixed_velocity = 0;
  rec.pads[0].hi_hat_volume = 200;

  doc->ReplaceWithDeviceState({rec}, {});

  const PadParams& params = device.kit(0).pads[0].params;
  EXPECT_EQ(params.mode, static_cast<LayerMode>(kLayerModeCount - 1));
  EXPECT_EQ(params.curve, static_cast<DynamicsCurve>(kDynamicsCurveCount - 1));
  EXPECT_EQ(params.fade_end, 100);  // held at the fade point
  EXPECT_EQ(params.fixed_velocity, 1);
  EXPECT_EQ(params.hi_hat_volume, 127);
}

// An unnamed record keeps the model's default rather than becoming blank.
TEST_F(DeviceDocumentTest, ReplaceWithDeviceStateKeepsTheDefaultNameWhenEmpty) {
  doc->ReplaceWithDeviceState({DeviceKit("")}, {});
  EXPECT_EQ(device.kit(0).name, juce::String("USER KIT"));
}

// ---- ImportKitFile: the legacy single-kit .kit reader ----

TEST_F(DeviceDocumentTest, ImportKitFileReadsThePadFormat) {
  const juce::File file = temp.file("legacy.kit");
  file.replaceWithText(R"({
    "version": 4, "name": "LEGACY",
    "pads": [
      {"samples": ["/tmp/a.wav", 7], "mode": "XFADE", "fadePoint": 40,
       "fadeEnd": 90, "dynamics": false, "dynamicsCurve": "LOUD2",
       "fixedVelocity": 64, "triggerReserve": true}
    ]
  })");

  ASSERT_TRUE(doc->ImportKitFile(file).wasOk());

  EXPECT_EQ(model.name(), juce::String("LEGACY"));
  EXPECT_EQ(model.sample(0, 0), LayerSample(juce::File("/tmp/a.wav")));
  EXPECT_EQ(model.sample(0, 1), LayerSample::DeviceWave(7));
  const PadParams& params = model.params(0);
  EXPECT_EQ(params.mode, LayerMode::kXfade);
  EXPECT_EQ(params.fade_point, 40);
  EXPECT_EQ(params.fade_end, 90);
  EXPECT_FALSE(params.dynamics);
  EXPECT_EQ(params.curve, DynamicsCurve::kLoud2);
  EXPECT_EQ(params.fixed_velocity, 64);
  EXPECT_TRUE(params.trigger_reserve);
  EXPECT_EQ(history_resets, 1);
}

// v1 files predate pads: 18 flat slots in (pad * 2 + layer) order.
TEST_F(DeviceDocumentTest, ImportKitFileReadsTheFlatSlotFormat) {
  const juce::File file = temp.file("v1.kit");
  file.replaceWithText(R"({
    "version": 1, "name": "FLAT",
    "slots": ["/tmp/a.wav", "/tmp/b.wav", "/tmp/c.wav"]
  })");

  ASSERT_TRUE(doc->ImportKitFile(file).wasOk());

  EXPECT_EQ(model.name(), juce::String("FLAT"));
  EXPECT_EQ(model.sample(0, 0), LayerSample(juce::File("/tmp/a.wav")));
  EXPECT_EQ(model.sample(0, 1), LayerSample(juce::File("/tmp/b.wav")));
  EXPECT_EQ(model.sample(1, 0), LayerSample(juce::File("/tmp/c.wav")));
  EXPECT_TRUE(model.sample(1, 1).empty());
}

// Old and hand-edited files degrade rather than fail: absent fields fall
// back to defaults, and entries of the wrong type become empty.
TEST_F(DeviceDocumentTest, ImportKitFileToleratesSparseAndOddFiles) {
  const juce::File file = temp.file("sparse.kit");
  file.replaceWithText(R"({
    "version": 2,
    "pads": [{"samples": [true]}, {}]
  })");

  ASSERT_TRUE(doc->ImportKitFile(file).wasOk());

  EXPECT_EQ(model.name(), juce::String("Untitled Kit"));
  EXPECT_TRUE(model.sample(0, 0).empty());  // a bool is not a sample
  EXPECT_TRUE(model.sample(0, 1).empty());  // short array
  EXPECT_EQ(model.params(0).mode, LayerMode::kMix);
  EXPECT_EQ(model.params(0).fade_point, kDefaultFadePoint);
  EXPECT_EQ(model.params(1).fixed_velocity, kDefaultFixedVelocity);
}

TEST_F(DeviceDocumentTest, ImportKitFileRefusesANewerKitFormat) {
  const juce::File file = temp.file("future.kit");
  file.replaceWithText(R"({"version": 99, "name": "FUTURE"})");

  const juce::Result result = doc->ImportKitFile(file);

  ASSERT_TRUE(result.failed());
  EXPECT_TRUE(result.getErrorMessage().contains("v99"))
      << result.getErrorMessage();
  EXPECT_EQ(model.name(), juce::String("Untitled Kit"));  // untouched
}

TEST_F(DeviceDocumentTest, ImportKitFileRefusesSomethingThatIsNotAKit) {
  const juce::File file = temp.file("junk.kit");
  file.replaceWithText("this is not JSON");

  const juce::Result result = doc->ImportKitFile(file);

  ASSERT_TRUE(result.failed());
  EXPECT_TRUE(result.getErrorMessage().contains("not a valid .kit file"))
      << result.getErrorMessage();
}

TEST_F(DeviceDocumentTest, ImportKitFileLandsOnTheActiveKit) {
  const juce::File file = temp.file("k.kit");
  file.replaceWithText(R"({"version": 4, "name": "IMPORTED"})");
  doc->SwitchKit(5);

  ASSERT_TRUE(doc->ImportKitFile(file).wasOk());

  EXPECT_EQ(device.kit(5).name, juce::String("IMPORTED"));
  EXPECT_EQ(model.name(), juce::String("IMPORTED"));
  // A kit that was never active is untouched. (Kit 0 is not a fair check
  // here: switching away stashed the live model onto it, and this model was
  // never seeded from stored kit data the way the app seeds it.)
  EXPECT_EQ(device.kit(1).name, juce::String("USER KIT"));
}

// ---- The remembered document path ----

TEST_F(DeviceDocumentTest, TheLastOpenedDocumentRoundTripsThroughSettings) {
  EXPECT_EQ(doc->getLastDocumentOpened(), juce::File());

  doc->setLastDocumentOpened(path());
  EXPECT_EQ(doc->getLastDocumentOpened(), path());
}

}  // namespace
}  // namespace spdsx
