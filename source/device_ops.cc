#include "device_ops.h"

#include <algorithm>
#include <chrono>
#include <thread>

#include "device/protocol.h"

namespace spdsx::ops {
namespace {

// The device takes the no-ack parameter writes as fast as this; the
// official app paces them the same way.
constexpr double kPaceSeconds = 0.02;

void Pace() {
  std::this_thread::sleep_for(std::chrono::duration<double>(kPaceSeconds));
}

std::string Quoted(const std::string& text) {
  const bool needs =
      text.empty() || text.find_first_of(" \t\"'\\$`") != std::string::npos;
  if (!needs) {
    return text;
  }
  std::string out = "\"";
  for (const char c : text) {
    if (c == '"' || c == '\\') {
      out += '\\';
    }
    out += c;
  }
  out += '"';
  return out;
}

}  // namespace

std::string KitSpecText(const std::vector<spdutil::KitRange>& kits) {
  std::string out;
  for (const spdutil::KitRange& range : kits) {
    if (!out.empty()) {
      out += ",";
    }
    out += std::to_string(range.first);
    if (range.last != range.first) {
      out += "-" + std::to_string(range.last);
    }
  }
  return out;
}

std::vector<ModeChange> PlanSetMode(const std::vector<device::KitRecord>& kits,
                                    const SetModeRequest& request) {
  std::array<bool, 9> wanted {};
  wanted.fill(request.pads.empty());
  for (const int pad : request.pads) {
    if (pad >= 1 && pad <= 9) {
      wanted[static_cast<size_t>(pad - 1)] = true;
    }
  }
  std::vector<ModeChange> plan;
  for (const spdutil::KitRange& range : request.kits) {
    for (int kit = range.first; kit <= range.last; ++kit) {
      if (kit < 1 || static_cast<size_t>(kit) > kits.size()) {
        continue;
      }
      const auto& record = kits[static_cast<size_t>(kit - 1)];
      for (int pad = 1; pad <= 9; ++pad) {
        if (!wanted[static_cast<size_t>(pad - 1)]) {
          continue;
        }
        const auto current = static_cast<LayerMode>(std::clamp(
            static_cast<int>(
                record.pads[static_cast<size_t>(pad - 1)].layer_mode),
            0,
            kLayerModeCount - 1));
        if (current == request.target
            || (request.has_if_mode && current != request.if_mode)) {
          continue;
        }
        plan.push_back({.kit = kit, .pad = pad, .from = current});
      }
    }
  }
  return plan;
}

SetModeResult SetMode(device::SpdsxDevice& dev,
                      const SetModeRequest& request,
                      ProgressFn progress,
                      AbortFn should_abort) {
  SetModeResult result;
  progress({.done = 0, .total = 0, .note = "reading the kits bank"});
  const auto kits = device::ParseKits(
      device::CleanBulkImage(dev.DumpBank(device::kBankKits)));
  if (kits.empty()) {
    throw std::runtime_error("couldn't read the kits bank");
  }
  const std::vector<ModeChange> plan = PlanSetMode(kits, request);
  result.changed = static_cast<int>(plan.size());
  const int total = result.changed;
  progress({.done = 0,
            .total = total,
            .note = std::to_string(total) + " pad(s) to change"});
  if (request.dry_run || plan.empty()) {
    return result;
  }

  // Focus once per pad number (the write address is kit-absolute), then
  // stream the one-byte mode writes.
  int done = 0;
  for (int pad = 1; pad <= 9; ++pad) {
    bool focused = false;
    for (const ModeChange& change : plan) {
      if (change.pad != pad) {
        continue;
      }
      if (should_abort()) {
        result.aborted = true;
        return result;
      }
      if (!focused) {
        dev.SelectObject(device::ObjectKind::kPad, pad);
        Pace();
        focused = true;
      }
      dev.Send(device::Dt1(
          device::PadParamAddr({.kit = change.kit, .pad = change.pad}, 0x00),
          {static_cast<uint8_t>(request.target)}));
      result.wrote = true;
      Pace();
      ++done;
      progress({.done = done,
                .total = total,
                .note = "kit " + std::to_string(change.kit) + " pad "
                    + std::to_string(change.pad)});
    }
  }
  if (request.commit) {
    progress({.done = done, .total = total, .note = "committing to flash"});
    result.committed = dev.Commit(should_abort);
  } else {
    dev.Ping();  // delivery barrier for the fire-and-forget tail
  }
  return result;
}

SetNameResult SetName(device::SpdsxDevice& dev,
                      const SetNameRequest& request,
                      ProgressFn progress,
                      AbortFn should_abort) {
  SetNameResult result;
  if (request.dry_run) {
    progress({.done = 0, .total = 1, .note = "nothing sent"});
    return result;
  }
  progress({.done = 0,
            .total = 1,
            .note = "naming kit " + std::to_string(request.kit)});
  dev.SetKitName(request.kit, request.name, kPaceSeconds);
  result.wrote = true;
  if (request.commit) {
    progress({.done = 1, .total = 1, .note = "committing to flash"});
    result.committed = dev.Commit(should_abort);
  } else {
    dev.Ping();
  }
  progress({.done = 1, .total = 1, .note = "done"});
  return result;
}

std::string CommandLine(const SetModeRequest& request) {
  std::string out = "spdutil setmode --kits " + KitSpecText(request.kits)
      + " --mode " + std::string(LayerModeName(request.target));
  for (const int pad : request.pads) {
    out += " --pad " + std::to_string(pad);
  }
  if (request.has_if_mode) {
    out += " --if-mode " + std::string(LayerModeName(request.if_mode));
  }
  if (request.dry_run) {
    out += " --dry-run";
  }
  if (request.commit) {
    out += " --commit";
  }
  return out;
}

std::string CommandLine(const SetNameRequest& request) {
  std::string out = "spdutil setname --kits " + std::to_string(request.kit)
      + " --name " + Quoted(request.name);
  if (request.dry_run) {
    out += " --dry-run";
  }
  if (request.commit) {
    out += " --commit";
  }
  return out;
}

}  // namespace spdsx::ops
