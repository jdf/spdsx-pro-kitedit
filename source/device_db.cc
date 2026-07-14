#include "device_db.h"

#include <stdexcept>
#include <string>

#include <sqlite3.h>

namespace spdsx {

namespace {

// Bump when the schema changes (stored in PRAGMA user_version).
constexpr int kSchemaVersion = 1;

const char* kSchemaSql = R"SQL(
CREATE TABLE IF NOT EXISTS meta(
  key TEXT PRIMARY KEY, value TEXT);
CREATE TABLE IF NOT EXISTS kits(
  snapshot TEXT NOT NULL, idx INTEGER NOT NULL, name TEXT NOT NULL,
  PRIMARY KEY(snapshot, idx));
CREATE TABLE IF NOT EXISTS pads(
  snapshot TEXT NOT NULL, kit_idx INTEGER NOT NULL, pad_idx INTEGER NOT NULL,
  mode INTEGER, fade_point INTEGER, fade_end INTEGER, dynamics INTEGER,
  curve INTEGER, fixed_velocity INTEGER, hihat_vol INTEGER,
  hihat_fadein INTEGER, hihat_decay INTEGER, trigger_reserve INTEGER,
  top_device INTEGER, top_local TEXT, bottom_device INTEGER, bottom_local TEXT,
  PRIMARY KEY(snapshot, kit_idx, pad_idx));
CREATE TABLE IF NOT EXISTS samples(
  idx INTEGER PRIMARY KEY, wavename TEXT, filename TEXT, frames INTEGER,
  category INTEGER, content_hash INTEGER, audio BLOB);
)SQL";

const char* SnapshotName(Snapshot s)
{
  return s == Snapshot::kBase ? "base" : "current";
}

// Throws on a real SQLite error (ROW/DONE/OK are all fine).
void Check(sqlite3* db, int rc, const char* what)
{
  if (rc != SQLITE_OK && rc != SQLITE_ROW && rc != SQLITE_DONE) {
    throw std::runtime_error(
        std::string(what) + ": " + sqlite3_errmsg(db));
  }
}

void Exec(sqlite3* db, const char* sql)
{
  char* err = nullptr;
  if (sqlite3_exec(db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
    const std::string msg = err ? err : "exec failed";
    sqlite3_free(err);
    throw std::runtime_error(msg);
  }
}

// A prepared statement with reset-and-reuse; finalizes on destruction.
class Stmt {
public:
  Stmt(sqlite3* db, const char* sql) : db_(db)
  {
    Check(db, sqlite3_prepare_v2(db, sql, -1, &stmt_, nullptr), "prepare");
  }
  ~Stmt() { sqlite3_finalize(stmt_); }
  Stmt(const Stmt&) = delete;
  Stmt& operator=(const Stmt&) = delete;

  void Int(int col, int v) { sqlite3_bind_int(stmt_, col, v); }
  void Int64(int col, sqlite3_int64 v) { sqlite3_bind_int64(stmt_, col, v); }
  void Text(int col, const juce::String& v)
  {
    const auto utf8 = v.toStdString();
    // SQLITE_TRANSIENT: sqlite copies the bytes, so utf8 can die.
    sqlite3_bind_text(stmt_, col, utf8.c_str(), -1, SQLITE_TRANSIENT);
  }
  void Blob(int col, const void* data, size_t bytes)
  {
    sqlite3_bind_blob(stmt_, col, data, static_cast<int>(bytes),
        SQLITE_TRANSIENT);
  }

  // Steps; returns true while a row is available.
  bool Step()
  {
    const int rc = sqlite3_step(stmt_);
    Check(db_, rc, "step");
    return rc == SQLITE_ROW;
  }
  void RunOnce()
  {
    Step();
    Reset();
  }
  void Reset()
  {
    sqlite3_reset(stmt_);
    sqlite3_clear_bindings(stmt_);
  }

  int ColInt(int col) { return sqlite3_column_int(stmt_, col); }
  juce::String ColText(int col)
  {
    const auto* p = reinterpret_cast<const char*>(
        sqlite3_column_text(stmt_, col));
    return p != nullptr ? juce::String::fromUTF8(p) : juce::String();
  }

  sqlite3_stmt* raw() { return stmt_; }

private:
  sqlite3* db_;
  sqlite3_stmt* stmt_ = nullptr;
};

// A pad's dual-identity layer -> (device index, local path).
int LayerDevice(const LayerSample& s) { return s.is_device() ? s.device_index : 0; }
juce::String LayerLocal(const LayerSample& s)
{
  return s.is_file() ? s.file.getFullPathName() : juce::String();
}
LayerSample LayerFrom(int device_idx, const juce::String& local)
{
  if (device_idx > 0) {
    return LayerSample::DeviceWave(device_idx);
  }
  if (local.isNotEmpty()) {
    return LayerSample(juce::File(local));
  }
  return LayerSample();
}

}  // namespace

DeviceDb::~DeviceDb() { sqlite3_close(db_); }

std::unique_ptr<DeviceDb> DeviceDb::Open(const juce::File& path,
    juce::String& error)
{
  sqlite3* db = nullptr;
  if (sqlite3_open(path.getFullPathName().toRawUTF8(), &db) != SQLITE_OK) {
    error = juce::String("couldn't open ") + path.getFullPathName() + ": "
        + sqlite3_errmsg(db);
    sqlite3_close(db);
    return nullptr;
  }
  try {
    Exec(db, "PRAGMA journal_mode=WAL;");
    Exec(db, "PRAGMA foreign_keys=ON;");
    Exec(db, kSchemaSql);
    Exec(db, ("PRAGMA user_version=" + std::to_string(kSchemaVersion)).c_str());
  } catch (const std::exception& e) {
    error = juce::String("schema init failed: ") + e.what();
    sqlite3_close(db);
    return nullptr;
  }
  return std::unique_ptr<DeviceDb>(new DeviceDb(db));
}

void DeviceDb::WriteKits(const DeviceModel& model, Snapshot snapshot)
{
  const char* snap = SnapshotName(snapshot);
  Exec(db_, "BEGIN;");
  try {
    {
      Stmt del(db_, "DELETE FROM kits WHERE snapshot=?1;");
      del.Text(1, snap);
      del.RunOnce();
      Stmt delp(db_, "DELETE FROM pads WHERE snapshot=?1;");
      delp.Text(1, snap);
      delp.RunOnce();
    }
    Stmt kit(db_, "INSERT INTO kits(snapshot, idx, name) VALUES(?1,?2,?3);");
    Stmt pad(db_,
        "INSERT INTO pads(snapshot, kit_idx, pad_idx, mode, fade_point, "
        "fade_end, dynamics, curve, fixed_velocity, hihat_vol, hihat_fadein, "
        "hihat_decay, trigger_reserve, top_device, top_local, bottom_device, "
        "bottom_local) VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12,?13,?14,"
        "?15,?16,?17);");
    for (int k = 0; k < DeviceModel::kKitCount; ++k) {
      const KitData& kd = model.kit(k);
      kit.Text(1, snap);
      kit.Int(2, k);
      kit.Text(3, kd.name);
      kit.RunOnce();
      for (int p = 0; p < KitModel::kPadCount; ++p) {
        const Pad& pd = kd.pads[static_cast<size_t>(p)];
        const PadParams& pp = pd.params;
        pad.Text(1, snap);
        pad.Int(2, k);
        pad.Int(3, p);
        pad.Int(4, static_cast<int>(pp.mode));
        pad.Int(5, pp.fade_point);
        pad.Int(6, pp.fade_end);
        pad.Int(7, pp.dynamics ? 1 : 0);
        pad.Int(8, static_cast<int>(pp.curve));
        pad.Int(9, pp.fixed_velocity);
        pad.Int(10, pp.hi_hat_volume);
        pad.Int(11, pp.hi_hat_fade_in);
        pad.Int(12, pp.hi_hat_decay);
        pad.Int(13, pp.trigger_reserve ? 1 : 0);
        pad.Int(14, LayerDevice(pd.samples.first));
        pad.Text(15, LayerLocal(pd.samples.first));
        pad.Int(16, LayerDevice(pd.samples.second));
        pad.Text(17, LayerLocal(pd.samples.second));
        pad.RunOnce();
      }
    }
    if (snapshot == Snapshot::kCurrent) {
      Stmt meta(db_,
          "INSERT INTO meta(key, value) VALUES('current_kit', ?1) "
          "ON CONFLICT(key) DO UPDATE SET value=excluded.value;");
      meta.Text(1, juce::String(model.current_kit()));
      meta.RunOnce();
    }
  } catch (...) {
    Exec(db_, "ROLLBACK;");
    throw;
  }
  Exec(db_, "COMMIT;");
}

void DeviceDb::ReadKits(DeviceModel& model, Snapshot snapshot)
{
  const char* snap = SnapshotName(snapshot);
  // Start from defaults so any kit missing from the DB is a clean USER KIT.
  for (int k = 0; k < DeviceModel::kKitCount; ++k) {
    model.kit(k) = KitData();
  }
  {
    Stmt kit(db_, "SELECT idx, name FROM kits WHERE snapshot=?1;");
    kit.Text(1, snap);
    while (kit.Step()) {
      const int idx = kit.ColInt(0);
      if (idx >= 0 && idx < DeviceModel::kKitCount) {
        model.kit(idx).name = kit.ColText(1);
      }
    }
  }
  {
    Stmt pad(db_,
        "SELECT kit_idx, pad_idx, mode, fade_point, fade_end, dynamics, "
        "curve, fixed_velocity, hihat_vol, hihat_fadein, hihat_decay, "
        "trigger_reserve, top_device, top_local, bottom_device, bottom_local "
        "FROM pads WHERE snapshot=?1;");
    pad.Text(1, snap);
    while (pad.Step()) {
      const int k = pad.ColInt(0);
      const int p = pad.ColInt(1);
      if (k < 0 || k >= DeviceModel::kKitCount || p < 0
          || p >= KitModel::kPadCount) {
        continue;
      }
      Pad& pd = model.kit(k).pads[static_cast<size_t>(p)];
      PadParams& pp = pd.params;
      pp.mode = static_cast<LayerMode>(
          juce::jlimit(0, kLayerModeCount - 1, pad.ColInt(2)));
      pp.fade_point = juce::jlimit(1, 127, pad.ColInt(3));
      pp.fade_end = juce::jmax(pp.fade_point, juce::jlimit(1, 127, pad.ColInt(4)));
      pp.dynamics = pad.ColInt(5) != 0;
      pp.curve = static_cast<DynamicsCurve>(
          juce::jlimit(0, kDynamicsCurveCount - 1, pad.ColInt(6)));
      pp.fixed_velocity = juce::jlimit(1, 127, pad.ColInt(7));
      pp.hi_hat_volume = juce::jlimit(0, 127, pad.ColInt(8));
      pp.hi_hat_fade_in = juce::jlimit(0, 127, pad.ColInt(9));
      pp.hi_hat_decay = juce::jlimit(0, 127, pad.ColInt(10));
      pp.trigger_reserve = pad.ColInt(11) != 0;
      pd.samples.first = LayerFrom(pad.ColInt(12), pad.ColText(13));
      pd.samples.second = LayerFrom(pad.ColInt(14), pad.ColText(15));
    }
  }
  if (snapshot == Snapshot::kCurrent) {
    Stmt meta(db_, "SELECT value FROM meta WHERE key='current_kit';");
    if (meta.Step()) {
      model.set_current_kit(juce::jlimit(0, DeviceModel::kKitCount - 1,
          meta.ColText(0).getIntValue()));
    }
    std::vector<device::SampleRecord> pool;
    Stmt s(db_, "SELECT idx, wavename, filename, frames, category "
                "FROM samples ORDER BY idx;");
    while (s.Step()) {
      device::SampleRecord rec;
      rec.index = s.ColInt(0);
      rec.wavename = s.ColText(1).toStdString();
      rec.filename = s.ColText(2).toStdString();
      rec.frames = static_cast<uint32_t>(s.ColInt(3));
      rec.category = s.ColInt(4);
      if (rec.index > 0) {
        pool.push_back(std::move(rec));
      }
    }
    model.set_sample_pool(std::move(pool));
  }
}

void DeviceDb::WritePool(const DeviceModel& model)
{
  Exec(db_, "BEGIN;");
  try {
    // Upsert metadata, leaving any cached audio blob untouched.
    Stmt s(db_,
        "INSERT INTO samples(idx, wavename, filename, frames, category) "
        "VALUES(?1,?2,?3,?4,?5) ON CONFLICT(idx) DO UPDATE SET "
        "wavename=excluded.wavename, filename=excluded.filename, "
        "frames=excluded.frames, category=excluded.category;");
    for (const auto& rec : model.sample_pool()) {
      s.Int(1, rec.index);
      s.Text(2, juce::String(rec.wavename));
      s.Text(3, juce::String(rec.filename));
      s.Int64(4, static_cast<sqlite3_int64>(rec.frames));
      s.Int(5, rec.category);
      s.RunOnce();
    }
  } catch (...) {
    Exec(db_, "ROLLBACK;");
    throw;
  }
  Exec(db_, "COMMIT;");
}

bool DeviceDb::HasAudio(int sample_index)
{
  Stmt s(db_, "SELECT audio IS NOT NULL FROM samples WHERE idx=?1;");
  s.Int(1, sample_index);
  return s.Step() && s.ColInt(0) != 0;
}

juce::MemoryBlock DeviceDb::GetAudio(int sample_index)
{
  Stmt s(db_, "SELECT audio FROM samples WHERE idx=?1;");
  s.Int(1, sample_index);
  juce::MemoryBlock out;
  if (s.Step()) {
    const void* data = sqlite3_column_blob(s.raw(), 0);
    const int bytes = sqlite3_column_bytes(s.raw(), 0);
    if (data != nullptr && bytes > 0) {
      out.append(data, static_cast<size_t>(bytes));
    }
  }
  return out;
}

void DeviceDb::PutAudio(int sample_index, const void* data, size_t bytes)
{
  Stmt s(db_,
      "INSERT INTO samples(idx, audio) VALUES(?1,?2) "
      "ON CONFLICT(idx) DO UPDATE SET audio=excluded.audio;");
  s.Int(1, sample_index);
  s.Blob(2, data, bytes);
  s.RunOnce();
}

void DeviceDb::CaptureBase()
{
  Exec(db_, "BEGIN;");
  try {
    Exec(db_, "DELETE FROM kits WHERE snapshot='base';");
    Exec(db_, "DELETE FROM pads WHERE snapshot='base';");
    Exec(db_, "INSERT INTO kits(snapshot, idx, name) "
              "SELECT 'base', idx, name FROM kits WHERE snapshot='current';");
    Exec(db_, "INSERT INTO pads SELECT 'base', kit_idx, pad_idx, mode, "
              "fade_point, fade_end, dynamics, curve, fixed_velocity, "
              "hihat_vol, hihat_fadein, hihat_decay, trigger_reserve, "
              "top_device, top_local, bottom_device, bottom_local "
              "FROM pads WHERE snapshot='current';");
  } catch (...) {
    Exec(db_, "ROLLBACK;");
    throw;
  }
  Exec(db_, "COMMIT;");
}

}  // namespace spdsx
