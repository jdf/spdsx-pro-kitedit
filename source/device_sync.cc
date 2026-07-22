#include "device_sync.h"

#include <algorithm>
#include <map>

#include "layers.h"

namespace spdsx {

namespace {

// One field's three-way merge: changed on one side takes that side;
// changed on both to the same value converges; changed apart is a
// conflict — resolved toward `current` when mine_wins — and, when a
// collector is given, described with both sides' values.
template<typename T, typename Fmt>
T Merge3(const char* label,
         const T& current,
         const T& base,
         const T& theirs,
         bool mine_wins,
         juce::StringArray* conflicts,
         const Fmt& fmt) {
  if (current == base) {
    return theirs;
  }
  if (theirs == base || theirs == current) {
    return current;
  }
  if (conflicts != nullptr) {
    conflicts->add(juce::String(label) + " (yours " + fmt(current) + ", device "
                   + fmt(theirs) + ")");
  }
  return mine_wins ? current : theirs;
}

juce::String FmtInt(int v) {
  return juce::String(v);
}

juce::String FmtBool(bool v) {
  return v ? "on" : "off";
}

juce::String FmtMode(LayerMode v) {
  return juce::String(std::string(LayerModeName(v)));
}

juce::String FmtCurve(DynamicsCurve v) {
  return juce::String(std::string(DynamicsCurveName(v)));
}

juce::String FmtSample(const LayerSample& v) {
  if (v.is_device()) {
    return "wave " + juce::String(v.device_index);
  }
  if (v.is_file()) {
    return v.file.getFileName();
  }
  return "empty";
}

// Keeps a name representable in the device's space-padded ASCII fields.
std::string SanitizedAscii(const juce::String& name, size_t max_len) {
  const std::string raw = name.toStdString();
  std::string out;
  out.reserve(std::min(raw.size(), max_len));
  for (const char c : raw) {
    if (out.size() >= max_len) {
      break;
    }
    out.push_back(c >= 0x20 && c < 0x7f ? c : '_');
  }
  return out;
}

}  // namespace

KitData KitDataFromDevice(const device::KitRecord& rec) {
  KitData kit;
  if (!rec.name.empty()) {
    kit.name = juce::String(rec.name);
  }
  for (int pad = 0; pad < KitModel::kPadCount; ++pad) {
    const auto& dp = rec.pads[static_cast<size_t>(pad)];
    auto& p = kit.pads[static_cast<size_t>(pad)];
    p.params.mode = static_cast<LayerMode>(
        juce::jlimit(0, kLayerModeCount - 1, static_cast<int>(dp.layer_mode)));
    p.params.fade_point = juce::jlimit(1, 127, static_cast<int>(dp.fade_point));
    p.params.fade_end =
        juce::jmax(p.params.fade_point,
                   juce::jlimit(1, 127, static_cast<int>(dp.fade_end)));
    p.params.dynamics = dp.dynamics != 0;
    p.params.curve = static_cast<DynamicsCurve>(juce::jlimit(
        0, kDynamicsCurveCount - 1, static_cast<int>(dp.dynamics_curve)));
    p.params.fixed_velocity =
        juce::jlimit(1, 127, static_cast<int>(dp.fixed_velocity));
    p.params.trigger_reserve = dp.trigger_reserve != 0;
    p.params.hi_hat_volume =
        juce::jlimit(0, 127, static_cast<int>(dp.hi_hat_volume));
    p.params.hi_hat_fade_in =
        juce::jlimit(0, 127, static_cast<int>(dp.hi_hat_fade_in));
    p.params.hi_hat_decay =
        juce::jlimit(0, 127, static_cast<int>(dp.hi_hat_decay));
    p.samples.first =
        dp.wave_top > 0 ? LayerSample::DeviceWave(dp.wave_top) : LayerSample();
    p.samples.second = dp.wave_bottom > 0
        ? LayerSample::DeviceWave(dp.wave_bottom)
        : LayerSample();
  }
  return kit;
}

device::PadDeviceParams DeviceParamsFromPad(const Pad& pad) {
  const PadParams& pp = pad.params;
  device::PadDeviceParams dp;
  dp.layer_mode = static_cast<uint8_t>(pp.mode);
  dp.fade_point = static_cast<uint8_t>(pp.fade_point);
  dp.fade_end = static_cast<uint8_t>(pp.fade_end);
  dp.dynamics = pp.dynamics ? 1 : 0;
  dp.dynamics_curve = static_cast<uint8_t>(pp.curve);
  dp.fixed_velocity = static_cast<uint8_t>(pp.fixed_velocity);
  dp.trigger_reserve = pp.trigger_reserve ? 1 : 0;
  dp.hi_hat_volume = static_cast<uint8_t>(pp.hi_hat_volume);
  dp.hi_hat_fade_in = static_cast<uint8_t>(pp.hi_hat_fade_in);
  dp.hi_hat_decay = static_cast<uint8_t>(pp.hi_hat_decay);
  const auto wave = [](const LayerSample& s) {
    return s.is_device() ? static_cast<uint16_t>(s.device_index) : uint16_t {0};
  };
  dp.wave_top = wave(pad.samples.first);
  dp.wave_bottom = wave(pad.samples.second);
  return dp;
}

Pad MergePad(const Pad& current,
             const Pad& base,
             const Pad& theirs,
             bool mine_wins,
             juce::StringArray* conflicts) {
  const PadParams& c = current.params;
  const PadParams& b = base.params;
  const PadParams& t = theirs.params;
  Pad m;
  PadParams& p = m.params;
  const auto merge = [&](const char* label,
                         const auto& cf,
                         const auto& bf,
                         const auto& tf,
                         const auto& fmt) {
    return Merge3(label, cf, bf, tf, mine_wins, conflicts, fmt);
  };
  p.mode = merge("layer mode", c.mode, b.mode, t.mode, FmtMode);
  p.fade_point =
      merge("fade point", c.fade_point, b.fade_point, t.fade_point, FmtInt);
  p.fade_end = merge("fade end", c.fade_end, b.fade_end, t.fade_end, FmtInt);
  p.dynamics = merge("dynamics", c.dynamics, b.dynamics, t.dynamics, FmtBool);
  p.curve = merge("dynamics curve", c.curve, b.curve, t.curve, FmtCurve);
  p.fixed_velocity = merge("fixed velocity",
                           c.fixed_velocity,
                           b.fixed_velocity,
                           t.fixed_velocity,
                           FmtInt);
  p.trigger_reserve = merge("trigger reserve",
                            c.trigger_reserve,
                            b.trigger_reserve,
                            t.trigger_reserve,
                            FmtBool);
  p.hi_hat_volume = merge("hi-hat volume",
                          c.hi_hat_volume,
                          b.hi_hat_volume,
                          t.hi_hat_volume,
                          FmtInt);
  p.hi_hat_fade_in = merge("hi-hat fade in",
                           c.hi_hat_fade_in,
                           b.hi_hat_fade_in,
                           t.hi_hat_fade_in,
                           FmtInt);
  p.hi_hat_decay = merge(
      "hi-hat decay", c.hi_hat_decay, b.hi_hat_decay, t.hi_hat_decay, FmtInt);
  m.samples.first = merge("top sample",
                          current.samples.first,
                          base.samples.first,
                          theirs.samples.first,
                          FmtSample);
  m.samples.second = merge("bottom sample",
                           current.samples.second,
                           base.samples.second,
                           theirs.samples.second,
                           FmtSample);
  return m;
}

juce::StringArray PadConflicts(const Pad& current,
                               const Pad& base,
                               const Pad& theirs) {
  juce::StringArray conflicts;
  MergePad(current, base, theirs, true, &conflicts);
  return conflicts;
}

std::vector<SyncConflict> FindKitConflicts(int kit,
                                           const KitData& current,
                                           const KitData& base,
                                           const KitData& theirs) {
  std::vector<SyncConflict> out;
  const juce::String label =
      "Kit " + juce::String(kit + 1) + " \"" + current.name + "\"";
  juce::StringArray name_conflict;
  Merge3("kit name",
         current.name,
         base.name,
         theirs.name,
         true,
         &name_conflict,
         [](const juce::String& v) { return "\"" + v + "\""; });
  if (!name_conflict.isEmpty()) {
    out.push_back({kit, -1, label + ": " + name_conflict.joinIntoString("; ")});
  }
  for (int pad = 0; pad < KitModel::kPadCount; ++pad) {
    const auto p = static_cast<size_t>(pad);
    const juce::StringArray conflicts =
        PadConflicts(current.pads[p], base.pads[p], theirs.pads[p]);
    if (!conflicts.isEmpty()) {
      out.push_back({kit,
                     pad,
                     label + " pad " + juce::String(pad + 1) + ": "
                         + conflicts.joinIntoString("; ")});
    }
  }
  return out;
}

bool KitSyncPlan::WritesDevice() const {
  if (write_name) {
    return true;
  }
  for (int pad = 0; pad < KitModel::kPadCount; ++pad) {
    const auto p = static_cast<size_t>(pad);
    if (write_params[p] || write_wave[p][0] || write_wave[p][1]) {
      return true;
    }
  }
  return false;
}

KitSyncPlan PlanKitSync(
    const KitData& current,
    const KitData& base,
    const KitData& theirs,
    SyncResolution name_resolution,
    const std::array<SyncResolution, KitModel::kPadCount>& pad_resolutions) {
  KitSyncPlan plan;
  plan.new_current = current;
  plan.new_base = base;

  // The kit name: one field, same rules as a pad's.
  juce::StringArray name_conflict;
  Merge3("kit name",
         current.name,
         base.name,
         theirs.name,
         true,
         &name_conflict,
         [](const juce::String& v) { return v; });
  if (name_conflict.isEmpty() || name_resolution != SyncResolution::kSkip) {
    const juce::String merged = Merge3("kit name",
                                       current.name,
                                       base.name,
                                       theirs.name,
                                       name_resolution == SyncResolution::kMine,
                                       static_cast<juce::StringArray*>(nullptr),
                                       [](const juce::String& v) { return v; });
    plan.new_current.name = merged;
    plan.new_base.name = merged;
    plan.write_name = merged != theirs.name;
  } else {
    plan.skipped = true;
  }

  for (int pad = 0; pad < KitModel::kPadCount; ++pad) {
    const auto p = static_cast<size_t>(pad);
    const Pad& c = current.pads[p];
    const Pad& b = base.pads[p];
    const Pad& t = theirs.pads[p];
    if (!PadConflicts(c, b, t).isEmpty()
        && pad_resolutions[p] == SyncResolution::kSkip) {
      // "Do nothing": the pad and its base stay put, so it re-flags on
      // the next sync.
      plan.skipped = true;
      continue;
    }
    const Pad merged =
        MergePad(c, b, t, pad_resolutions[p] == SyncResolution::kMine);
    plan.new_current.pads[p] = merged;
    plan.new_base.pads[p] = merged;
    plan.write_params[p] = merged.params != t.params;
    plan.write_wave[p][0] = merged.samples.first != t.samples.first;
    plan.write_wave[p][1] = merged.samples.second != t.samples.second;
  }
  return plan;
}

int NextFreeSampleIndex(const std::vector<device::SampleRecord>& pool,
                        int after) {
  int candidate = std::max(1, after + 1);
  for (const device::SampleRecord& record : pool) {
    if (record.index < candidate) {
      continue;
    }
    if (record.index > candidate) {
      break;  // the pool is in index order: a gap at `candidate`
    }
    ++candidate;
  }
  return candidate < device::kSampleSlots ? candidate : 0;
}

std::vector<UploadPlan> PlanUploads(
    const std::vector<std::pair<int, KitSyncPlan>>& plans,
    const std::vector<device::SampleRecord>& pool) {
  // Distinct files, in path order so index assignment is deterministic.
  std::map<juce::String, juce::File> files;
  for (const auto& [kit, plan] : plans) {
    for (int pad = 0; pad < KitModel::kPadCount; ++pad) {
      const auto p = static_cast<size_t>(pad);
      const Pad& merged = plan.new_current.pads[p];
      const std::array<const LayerSample*, 2> layers = {&merged.samples.first,
                                                        &merged.samples.second};
      for (int layer = 0; layer < KitModel::kLayersPerPad; ++layer) {
        const LayerSample& s = *layers[static_cast<size_t>(layer)];
        if (plan.write_wave[p][static_cast<size_t>(layer)] && s.is_file()) {
          files.emplace(s.file.getFullPathName(), s.file);
        }
      }
    }
  }
  std::vector<UploadPlan> uploads;
  uploads.reserve(files.size());
  int index = 0;
  for (const auto& [path, file] : files) {
    index = NextFreeSampleIndex(pool, index);
    if (index == 0) {
      break;  // pool full; the caller sees fewer uploads than files
    }
    UploadPlan u;
    u.file = file;
    u.index = index;
    u.wavename = SanitizedAscii(file.getFileNameWithoutExtension(), 16);
    u.filename = SanitizedAscii(file.getFileName(), 84);
    uploads.push_back(std::move(u));
  }
  return uploads;
}

void SubstituteUploads(std::vector<std::pair<int, KitSyncPlan>>& plans,
                       const std::vector<UploadPlan>& uploads) {
  const auto substitute = [&uploads](LayerSample& s) {
    if (!s.is_file()) {
      return;
    }
    for (const UploadPlan& u : uploads) {
      if (u.file == s.file) {
        s = LayerSample::DeviceWave(u.index);
        return;
      }
    }
  };
  for (auto& [kit, plan] : plans) {
    for (KitData* snapshot : {&plan.new_current, &plan.new_base}) {
      for (auto& pad : snapshot->pads) {
        substitute(pad.samples.first);
        substitute(pad.samples.second);
      }
    }
  }
}

KitWrite BuildKitWrite(int kit_index, const KitSyncPlan& plan) {
  KitWrite w;
  w.kit = kit_index + 1;
  w.name = plan.write_name;
  if (plan.write_name) {
    w.kit_name = SanitizedAscii(plan.new_current.name, device::kKitNameLength);
  }
  for (int pad = 0; pad < KitModel::kPadCount; ++pad) {
    const auto p = static_cast<size_t>(pad);
    if (!plan.write_params[p] && !plan.write_wave[p][0]
        && !plan.write_wave[p][1]) {
      continue;
    }
    PadWrite pw;
    pw.pad = pad + 1;
    pw.params = plan.write_params[p];
    pw.wave = plan.write_wave[p];
    pw.dp = DeviceParamsFromPad(plan.new_current.pads[p]);
    w.pads.push_back(pw);
  }
  return w;
}

bool ExecutePush(device::SpdsxDevice& dev,
                 const std::vector<SmpUpload>& uploads,
                 const std::vector<KitWrite>& kits,
                 absl::FunctionRef<void(const SmpUpload&)> on_uploaded,
                 double pace_seconds,
                 absl::FunctionRef<bool()> should_abort) {
  // Uploads and kit writes all land in working state; ONE Commit at the end
  // flushes the whole batch to flash. Per-file commits wedge the device.
  bool wrote = false;
  if (!uploads.empty()) {
    dev.PrepareUploadBatch();
    wrote = true;
    for (const SmpUpload& u : uploads) {
      dev.UploadWave(u.index, u.smp, u.wavename, u.filename);
      on_uploaded(u);  // a no-op by default (IgnoreUpload)
    }
  }
  for (const KitWrite& kw : kits) {
    if (kw.name) {
      dev.SetKitName(kw.kit, kw.kit_name, pace_seconds);
      wrote = true;
    }
    for (const PadWrite& pw : kw.pads) {
      if (pw.wave[0]) {
        dev.SetPadWave({.kit = kw.kit,
                        .pad = pw.pad,
                        .slot = device::PadSlot::kTop,
                        .sample = pw.dp.wave_top,
                        .pace_seconds = pace_seconds});
        wrote = true;
      }
      if (pw.wave[1]) {
        dev.SetPadWave({.kit = kw.kit,
                        .pad = pw.pad,
                        .slot = device::PadSlot::kBottom,
                        .sample = pw.dp.wave_bottom,
                        .pace_seconds = pace_seconds});
        wrote = true;
      }
      if (pw.params) {
        dev.SetPadLayerParams({.kit = kw.kit,
                               .pad = pw.pad,
                               .params = pw.dp,
                               .pace_seconds = pace_seconds});
        wrote = true;
      }
    }
  }
  // Everything above (uploads + DT1 writes) is in working state; this single
  // commit makes the whole batch durable. Nothing written = nothing to
  // commit. A batch with uploads commits the way the official app ends an
  // import; kit writes alone commit the way its WRITE button does.
  if (!wrote) {
    return true;
  }
  return uploads.empty() ? dev.Commit(should_abort)
                         : dev.CommitUploadBatch(should_abort);
}

}  // namespace spdsx
