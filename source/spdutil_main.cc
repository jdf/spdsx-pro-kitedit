// spdutil — command-line utility for the Roland SPD-SX PRO over its USB
// serial port. Successor to link_all_kits (this does more than linking).
//
//   spdutil [--port <dev>] <command> [options]
//
//   ping        open the port and ping the device (read-only)
//   info        serial port, ping status, firmware version query
//   dump        stream a memory bank (or all) to an image file; or
//               --verify an existing image's block structure offline
//   kits        list every kit name (live dump of bank 0x10, or --from
//               a saved dump file)
//   samples     list the device wave pool from the bank 0x20 sample
//               directory (live or --from); metadata only, no audio
//   padlink     put triggers/pads into a pad-link group across kits
//
// With no --port, scans /dev/cu.usbmodem* and pings each node until the
// device answers (the node number changes every time it's plugged in).
// Close the SPD-SX PRO App first (one program per port), and back the
// unit up before a padlink run.

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "device/kit_image.h"
#include "device/protocol.h"
#include "device/sample_image.h"
#include "device/spdsx_device.h"
#include "device_ops.h"
#include "layers.h"
#include "spdutil_args.h"

namespace {

using spdsx::device::BulkBlock;
using spdsx::device::Bytes;
using spdsx::device::ObjectKind;
using spdsx::device::PadSlot;
using spdsx::spdutil::KitRange;

std::string ToHex(const Bytes& b) {
  std::string s;
  char buf[4];
  for (size_t i = 0; i < b.size(); ++i) {
    std::snprintf(buf, sizeof(buf), "%02x", b[i]);
    if (i != 0) {
      s += ' ';
    }
    s += buf;
  }
  return s;
}

// The printable characters of a reply, for eyeballing ASCII fields like
// version strings.
std::string ToPrintable(const Bytes& b) {
  std::string s;
  for (uint8_t c : b) {
    s += (c >= 0x20 && c < 0x7F) ? static_cast<char>(c) : '.';
  }
  return s;
}

const char* KindName(ObjectKind kind) {
  return kind == ObjectKind::kPad ? "pad" : "trig";
}

// Resolves an explicit --port, or scans /dev/cu.usbmodem* and pings each
// candidate until one answers. Throws if nothing responds.
std::string ResolvePort(const std::string& requested) {
  if (!requested.empty()) {
    return requested;
  }
  const std::vector<std::string> candidates =
      spdsx::device::PlatformPorts().ListCandidates();
  if (candidates.empty()) {
    throw std::runtime_error(
        "no /dev/cu.usbmodem* ports found (device plugged in?)");
  }
  for (const auto& path : candidates) {
    std::printf("trying %s... ", path.c_str());
    std::fflush(stdout);
    try {
      const std::unique_ptr<spdsx::device::SerialPort> serial =
          spdsx::device::PlatformPorts().Open(path);
      spdsx::device::SpdsxDevice dev(serial.get());
      const Bytes reply = dev.Ping();
      if (!reply.empty()) {
        std::printf("ping ok\n");
        return path;
      }
      std::printf("no reply\n");
    } catch (const std::exception& e) {
      std::printf("%s\n", e.what());
    }
  }
  throw std::runtime_error(
      "no port answered the ping (official app closed? device on?)");
}

int UnknownCommand(const std::string& command) {
  std::fprintf(stderr, "unknown command \"%s\"", command.c_str());
  const std::string best = spdsx::spdutil::NearestCommand(command);
  if (!best.empty()) {
    std::fprintf(stderr, " — did you mean \"%s\"?", best.c_str());
  }
  std::fprintf(stderr, "\n(run spdutil with no command for the full usage)\n");
  return 2;
}

int Usage() {
  std::fprintf(
      stderr,
      "usage: spdutil [--port <dev>] <command> [options]\n"
      "       spdutil --version\n"
      "\n"
      "  ping        open the port and ping the device (read-only)\n"
      "  info        serial port, ping status, firmware version\n"
      "  dump        stream device memory to an image file:\n"
      "                --bank 0xNN      0x10 kits, 0x20 samples,\n"
      "                                 0x30 meta, 0x40 config (repeatable)\n"
      "                --all            all four banks\n"
      "                --out FILE       write the reassembled image\n"
      "                --verify FILE    offline: report a file's blocks\n"
      "  kits        list all kit names (live, or --from <dump file>)\n"
      "  kit <N>     show kit N's pad params (live, or --from <dump>)\n"
      "  samples     list the device wave pool (live, or --from <dump>;\n"
      "              directory only — the dump carries no audio)\n"
      "  readwave <N> read wave N's audio off the device (--out FILE:\n"
      "              .wav = converted, else raw .SMP)\n"
      "  setlayer --kits K --pad P.S [--volume dB] [--fadein N]\n"
      "           [--decay N]\n"
      "                  write one layer's volume (dB, e.g. -3.5), fade-in\n"
      "                  (0-127) and decay (0-127, 127 = none); whatever\n"
      "                  you leave out keeps its current value; --commit\n"
      "                  to persist\n"
      "  selectkit <N>   switch the device's playback kit (1-200)\n"
      "  currentkit      print the device's active kit\n"
      "  setmode --kits SPEC --mode M [--pad N] [--if-mode M]\n"
      "          [--dry-run]\n"
      "                  set pads' layer mode in the given kits (names:\n"
      "                  MIX FADE1 FADE2 XFADE SWITCH SW(MONO) ALTERNATE\n"
      "                  HI-HAT); --pad restricts to those pads 1-9\n"
      "                  (repeatable, default all nine); --if-mode\n"
      "                  touches only pads currently in that mode;\n"
      "                  --commit to persist\n"
      "  deletewave <N>  delete sample N from the pool + commit\n"
      "                  (DESTRUCTIVE, not undoable on the device)\n"
      "  sendwave <N> --from F.smp [--from G.smp ...] [--name X.wav]\n"
      "                  upload one or more waves on one connection to\n"
      "                  consecutive indices from N: write each file AND\n"
      "                  register it in the pool, then read every one back\n"
      "                  and report MATCH/FAIL (use a fresh index; --name\n"
      "                  applies only to a single file)\n"
      "  assign --kits K --sample N --pad P.S   assign pool sample N to\n"
      "                  kit K, pad P (1-9), slot S (0 top/1 bottom);\n"
      "                  e.g. --pad 2.1 = pad 2 bottom. Working state only,\n"
      "                  not committed (revert with a power cycle)\n"
      "  setname --kits K --name TEXT   set kit K's name (16 chars,\n"
      "                  space-padded); --commit to persist\n"
      "  setparams --kits K --pad N --params m,fp,fe,dyn,curve,fixvel,\n"
      "                  hhvol,hhfadein,hhdecay,trig   write pad N's ten\n"
      "                  hit-response params. Mode and curve are NAMES\n"
      "                  (MIX FADE1 FADE2 XFADE SWITCH SW(MONO)\n"
      "                  ALTERNATE HI-HAT; LINEAR LOUD1 LOUD2 LOUD3);\n"
      "                  dynamics and trig are ON or OFF; --commit\n"
      "                  to persist\n"
      "  padlink     put triggers/pads into a pad-link group:\n"
      "                --group N        link group (required)\n"
      "                --trigger N      link trigger N\n"
      "                --pad N          link pad N\n"
      "                --kits SPEC      kits to touch (required)\n"
      "                --dry-run        print, send nothing\n"
      "                --verbose        show device replies\n"
      "\n"
      "\n"
      "--kits SPEC is comma-separated ranges: 108, 108-200, 1,5,10-20\n"
      "(1-200 is every kit). Required by assign, setname, setparams,\n"
      "setlayer, setmode and padlink; the first four take exactly one\n"
      "kit.\n"
      "\n"
      "With no --port, scans /dev/cu.usbmodem* and pings each node.\n");
  return 2;
}

int RunPing(const std::string& port_arg) {
  const std::string port = ResolvePort(port_arg);
  const std::unique_ptr<spdsx::device::SerialPort> serial =
      spdsx::device::PlatformPorts().Open(port);
  spdsx::device::SpdsxDevice dev(serial.get());
  std::printf("opened %s\n", port.c_str());
  const Bytes reply = dev.Ping();
  if (reply.empty()) {
    std::printf("ping: NO REPLY (device connected? official app closed?)\n");
    return 1;
  }
  std::printf(
      "ping reply (%zu bytes): %s\n", reply.size(), ToHex(reply).c_str());
  return 0;
}

int RunInfo(const std::string& port_arg) {
  const std::string port = ResolvePort(port_arg);
  const std::unique_ptr<spdsx::device::SerialPort> serial =
      spdsx::device::PlatformPorts().Open(port);
  spdsx::device::SpdsxDevice dev(serial.get());
  std::printf("port:     %s\n", port.c_str());
  const Bytes pong = dev.Ping();
  std::printf("status:   %s\n", pong.empty() ? "no reply" : "connected");
  if (pong.empty()) {
    return 1;
  }
  const std::string ver = dev.FirmwareField(0);  // "2.00"
  const std::string build = dev.FirmwareField(3);  // "0094"
  if (ver.empty()) {
    std::printf("version:  (no reply)\n");
  } else {
    std::printf("version:  %s", ver.c_str());
    if (!build.empty()) {
      std::printf("  (build %s)", build.c_str());
    }
    std::printf("\n");
  }
  return 0;
}

const char* BankName(uint8_t bank) {
  switch (bank) {
    case spdsx::device::kBankKits:
      return "kits/settings";
    case spdsx::device::kBankSamples:
      return "sample audio";
    case spdsx::device::kBankMeta:
      return "metadata";
    case spdsx::device::kBankConfig:
      return "config";
    default:
      return "unknown";
  }
}

// Reports an image's block structure: count and total bytes per bank.
void ReportBlocks(const Bytes& image) {
  const std::vector<BulkBlock> blocks = spdsx::device::SplitBulkImage(image);
  std::printf("%zu bytes, %zu block(s):\n", image.size(), blocks.size());
  std::map<uint8_t, std::pair<int, size_t>> by_bank;  // bank -> (count, bytes)
  for (const auto& b : blocks) {
    auto& e = by_bank[b.bank];
    e.first += 1;
    e.second += b.length;
  }
  for (const auto& [bank, stats] : by_bank) {
    std::printf("  bank 0x%02x (%-13s): %3d block(s), %8zu bytes\n",
                bank,
                BankName(bank),
                stats.first,
                stats.second);
  }
}

bool WriteFile(const std::string& path, const Bytes& data) {
  std::ofstream out(path, std::ios::binary);
  out.write(reinterpret_cast<const char*>(data.data()),
            static_cast<std::streamsize>(data.size()));
  return out.good();
}

Bytes ReadFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  if (!in) {
    throw std::runtime_error("cannot open " + path);
  }
  const auto size = in.tellg();
  in.seekg(0);
  Bytes data(static_cast<size_t>(size));
  in.read(reinterpret_cast<char*>(data.data()), size);
  return data;
}

// Command argument structs: every Run* with more than a port-and-one-value
// signature takes one of these, filled with designated initializers at the
// dispatch site, so positional argument mixups cannot happen silently.
struct DumpArgs {
  std::string port;
  std::vector<uint8_t> banks;
  std::string out_path;
  std::string verify_path;
};

int RunDump(const DumpArgs& args) {
  // Offline: just report an existing image's structure.
  if (!args.verify_path.empty()) {
    ReportBlocks(ReadFile(args.verify_path));
    return 0;
  }

  const std::string port = ResolvePort(args.port);
  const std::unique_ptr<spdsx::device::SerialPort> serial =
      spdsx::device::PlatformPorts().Open(port);
  spdsx::device::SpdsxDevice dev(serial.get());
  std::printf("opened %s\n", port.c_str());

  Bytes image;
  for (uint8_t bank : args.banks) {
    std::printf("streaming bank 0x%02x (%s)... ", bank, BankName(bank));
    std::fflush(stdout);
    int blocks = 0;
    size_t bytes = 0;
    const Bytes bank_image = dev.DumpBank(bank, [&](const Bytes& block) {
      ++blocks;
      bytes += block.size();
    });
    std::printf("%d block(s), %zu bytes\n", blocks, bytes);
    if (bank_image.empty()) {
      std::printf(
          "  (no data — streaming protocol may need work; see notes)\n");
    }
    image.insert(image.end(), bank_image.begin(), bank_image.end());
  }

  std::printf("\n");
  ReportBlocks(image);
  if (!args.out_path.empty()) {
    if (!WriteFile(args.out_path, image)) {
      std::fprintf(stderr, "error: couldn't write %s\n", args.out_path.c_str());
      return 1;
    }
    std::printf("wrote %s\n", args.out_path.c_str());
  }
  return image.empty() ? 1 : 0;
}

int RunKits(const std::string& port_arg, const std::string& from_path) {
  Bytes raw;
  if (!from_path.empty()) {
    raw = ReadFile(from_path);
  } else {
    const std::string port = ResolvePort(port_arg);
    const std::unique_ptr<spdsx::device::SerialPort> serial =
        spdsx::device::PlatformPorts().Open(port);
    spdsx::device::SpdsxDevice dev(serial.get());
    std::printf("opened %s, streaming bank 0x10...\n", port.c_str());
    raw = dev.DumpBank(spdsx::device::kBankKits);
  }
  const Bytes clean = spdsx::device::CleanBulkImage(raw);
  const auto kits = spdsx::device::ParseKits(clean);
  std::printf("%zu raw bytes -> %zu clean bytes -> %zu kits\n",
              raw.size(),
              clean.size(),
              kits.size());
  for (size_t i = 0; i < kits.size(); ++i) {
    std::printf("  %3zu  %s\n", i + 1, kits[i].name.c_str());
  }
  return kits.empty() ? 1 : 0;
}

struct ReadWaveArgs {
  std::string port;
  int index = 0;
  std::string out_path;
};

int RunReadWave(const ReadWaveArgs& args) {
  const std::string port = ResolvePort(args.port);
  const std::unique_ptr<spdsx::device::SerialPort> serial =
      spdsx::device::PlatformPorts().Open(port);
  spdsx::device::SpdsxDevice dev(serial.get());
  std::printf("opened %s, reading wave %d (%s)...\n",
              port.c_str(),
              args.index,
              spdsx::device::RemoteWavePath(args.index).c_str());
  const Bytes smp =
      dev.ReadRemoteWave(args.index, [](size_t done, size_t total) {
        std::printf("\r  %zu / %zu bytes", done, total);
        std::fflush(stdout);
      });
  std::printf("\n%zu bytes read\n", smp.size());
  const auto hdr = spdsx::device::ParseRfwvHeader(smp);
  const size_t pcm = smp.size() > spdsx::device::kRfwvHeaderSize
      ? smp.size() - spdsx::device::kRfwvHeaderSize
      : 0;
  std::printf(
      "RFWV: valid=%d  %u Hz  %u ch  %u-bit  %zu PCM bytes"
      "  (%.2f s)\n",
      hdr.valid,
      hdr.sample_rate,
      hdr.channels,
      hdr.bits_per_sample,
      pcm,
      hdr.channels ? pcm / 2.0 / hdr.channels / 48000.0 : 0.0);
  if (!args.out_path.empty()) {
    // A .wav path gets the converted WAV; anything else the raw .SMP.
    const std::string& out = args.out_path;
    const bool as_wav =
        out.size() > 4 && out.compare(out.size() - 4, 4, ".wav") == 0;
    const Bytes data = as_wav ? spdsx::device::RfwvToWav(smp) : smp;
    if (data.empty() || !WriteFile(out, data)) {
      std::fprintf(stderr, "couldn't write %s\n", out.c_str());
      return 1;
    }
    std::printf("wrote %s (%s)\n", out.c_str(), as_wav ? "wav" : "raw smp");
  }
  return smp.empty() ? 1 : 0;
}

// Derives (wavename, filename) for a pool record from a source path.
// --name overrides the filename field; the wavename is that without an
// extension, capped at 16 chars.
std::pair<std::string, std::string> DeriveWaveNames(
    const std::string& from_path, const std::string& name_override) {
  std::string filename = name_override;
  if (filename.empty()) {
    const size_t slash = from_path.find_last_of('/');
    filename =
        slash == std::string::npos ? from_path : from_path.substr(slash + 1);
  }
  std::string wavename = filename;
  const size_t dot = wavename.find_last_of('.');
  if (dot != std::string::npos) {
    wavename = wavename.substr(0, dot);
  }
  if (wavename.size() > 16) {
    wavename = wavename.substr(0, 16);
  }
  return {wavename, filename};
}

// Uploads one or more `.smp` files to consecutive pool indices starting at
// `index`, all over a SINGLE device connection — mirroring how the GUI
// sync pushes a whole kit's samples at once. --name applies only when
// there is a single file; otherwise each name comes from its own basename.
struct SendWaveArgs {
  std::string port;
  int index = 0;
  std::vector<std::string> from_paths;
  std::string name;  // filename override, single-file only
};

int RunSendWave(const SendWaveArgs& args) {
  const std::vector<std::string>& from_paths = args.from_paths;
  if (from_paths.empty()) {
    std::fprintf(stderr, "sendwave needs --from <file.smp> (repeatable)\n");
    return 2;
  }
  // Read + validate every file before opening the port, so a bad file
  // fails fast with nothing sent.
  std::vector<Bytes> smps;
  smps.reserve(from_paths.size());
  for (const std::string& path : from_paths) {
    const Bytes smp = ReadFile(path);
    if (smp.size() <= 512) {
      std::fprintf(
          stderr, "smp too small (%zu bytes): %s\n", smp.size(), path.c_str());
      return 1;
    }
    smps.push_back(smp);
  }

  const std::string port = ResolvePort(args.port);
  const std::unique_ptr<spdsx::device::SerialPort> serial =
      spdsx::device::PlatformPorts().Open(port);
  spdsx::device::SpdsxDevice dev(serial.get());
  std::printf("opened %s: uploading %zu file(s) from index %d...\n",
              port.c_str(),
              from_paths.size(),
              args.index);

  // Phase 1: upload every file on the one connection (as the GUI sync
  // does), all into working state, then ONE flash commit — the official
  // app's batch model. No per-file readback: a file read immediately after
  // its own commit can miss before the flash settles.
  dev.PrepareUploadBatch();
  for (size_t i = 0; i < from_paths.size(); ++i) {
    const int idx = args.index + static_cast<int>(i);
    const auto [wavename, filename] = DeriveWaveNames(
        from_paths[i], from_paths.size() == 1 ? args.name : std::string());
    std::printf("  [%zu/%zu] index %d <- %zu bytes as \"%s\" / \"%s\"\n",
                i + 1,
                from_paths.size(),
                idx,
                smps[i].size(),
                wavename.c_str(),
                filename.c_str());
    std::fflush(stdout);
    dev.UploadWave(idx, smps[i], wavename, filename);
  }
  std::printf("committing the batch to flash...\n");
  std::fflush(stdout);
  // Polls until the device reports done, no time limit (Ctrl-C to bail).
  if (!dev.CommitUploadBatch()) {
    std::fprintf(stderr, "commit did not confirm\n");
    return 1;
  }

  // Phase 2: read each back and compare. A wave that didn't persist either
  // has no exportable file (ReadRemoteWave throws) or reads back short.
  std::printf("verifying...\n");
  int failures = 0;
  for (size_t i = 0; i < from_paths.size(); ++i) {
    const int idx = args.index + static_cast<int>(i);
    std::printf("  index %d: ", idx);
    try {
      const Bytes back = dev.ReadRemoteWave(idx);
      if (back == smps[i]) {
        std::printf("MATCH\n");
      } else {
        std::printf(
            "MISMATCH (read %zu of %zu bytes)\n", back.size(), smps[i].size());
        ++failures;
      }
    } catch (const std::exception& e) {
      std::printf("FAIL (%s)\n", e.what());
      ++failures;
    }
  }
  std::printf(
      "%zu uploaded, %d failed verification\n", from_paths.size(), failures);
  return failures == 0 ? 0 : 1;
}

// Parses a "P.S" pad spec: pad 1-9 before the dot, slot after (0 = top,
// 1 = bottom). Returns false on a malformed spec.
bool ParsePadSpec(const std::string& spec, int* pad, PadSlot* slot) {
  const size_t dot = spec.find('.');
  if (dot == std::string::npos) {
    return false;
  }
  const int p = std::atoi(spec.substr(0, dot).c_str());
  const int s = std::atoi(spec.substr(dot + 1).c_str());
  if (p < 1 || p > spdsx::device::kPadsPerKit || (s != 0 && s != 1)) {
    return false;
  }
  *pad = p;
  *slot = s == 0 ? PadSlot::kTop : PadSlot::kBottom;
  return true;
}

struct AssignArgs {
  std::string port;
  int kit = 1;
  int sample = -1;
  std::string pad_spec;
  bool dry_run = false;
  bool commit = false;
};

int RunAssign(const AssignArgs& args) {
  if (args.sample < 0 || args.pad_spec.empty()) {
    std::fprintf(stderr,
                 "assign needs --sample <index> and --pad <P.S> "
                 "(e.g. --pad 2.1 = pad 2 bottom slot)\n");
    return 2;
  }
  int pad = 0;
  PadSlot slot = PadSlot::kTop;
  if (!ParsePadSpec(args.pad_spec, &pad, &slot)) {
    std::fprintf(stderr,
                 "bad --pad \"%s\"; want P.S with P 1-9, S 0(top)"
                 "/1(bottom)\n",
                 args.pad_spec.c_str());
    return 2;
  }
  if (args.dry_run) {
    std::printf("dry run: kit %d pad %d %s <- sample %d (nothing sent)\n",
                args.kit,
                pad,
                slot == PadSlot::kTop ? "top" : "bottom",
                args.sample);
    return 0;
  }
  const std::string port = ResolvePort(args.port);
  const std::unique_ptr<spdsx::device::SerialPort> serial =
      spdsx::device::PlatformPorts().Open(port);
  spdsx::device::SpdsxDevice dev(serial.get());
  std::printf("opened %s: kit %d pad %d %s <- sample %d%s\n",
              port.c_str(),
              args.kit,
              pad,
              slot == PadSlot::kTop ? "top" : "bottom",
              args.sample,
              args.commit ? " (committing)" : " (working state)");
  dev.SetPadWave(
      {.kit = args.kit, .pad = pad, .slot = slot, .sample = args.sample});
  if (args.commit) {
    dev.Commit();
  }
  std::printf("done\n");
  return 0;
}

// Parses a comma list "mode,fp,fe,dyn,curve,fixvel,hhvol,hhfadein,hhdecay,
// trig" into pad params. Returns false if the count is wrong.
struct SetParamsArgs {
  std::string port;
  int kit = 1;
  int pad = 0;
  std::string params_spec;
  bool dry_run = false;
  bool commit = false;
};

int RunSetParams(const SetParamsArgs& args) {
  if (args.pad < 1 || args.pad > 9 || args.params_spec.empty()) {
    std::fprintf(stderr,
                 "setparams needs --pad <1-9> and --params "
                 "mode,fp,fe,dyn,curve,fixvel,hhvol,hhfadein,hhdecay,trig\n");
    return 2;
  }
  spdsx::device::PadDeviceParams p;
  std::string params_error;
  if (!spdsx::spdutil::ParsePadParams(args.params_spec, &p, &params_error)) {
    std::fprintf(stderr, "--params: %s\n", params_error.c_str());
    return 2;
  }
  if (args.dry_run) {
    std::printf(
        "dry run: kit %d pad %d params <- mode=%d fp=%d fe=%d "
        "dyn=%d curve=%d fixvel=%d hh(%d,%d,%d) trig=%d "
        "(nothing sent)\n",
        args.kit,
        args.pad,
        p.layer_mode,
        p.fade_point,
        p.fade_end,
        p.dynamics,
        p.dynamics_curve,
        p.fixed_velocity,
        p.hi_hat_volume,
        p.hi_hat_fade_in,
        p.hi_hat_decay,
        p.trigger_reserve);
    return 0;
  }
  const std::string port = ResolvePort(args.port);
  const std::unique_ptr<spdsx::device::SerialPort> serial =
      spdsx::device::PlatformPorts().Open(port);
  spdsx::device::SpdsxDevice dev(serial.get());
  std::printf(
      "opened %s: kit %d pad %d params <- mode=%d fp=%d fe=%d "
      "dyn=%d curve=%d fixvel=%d hh(%d,%d,%d) trig=%d%s\n",
      port.c_str(),
      args.kit,
      args.pad,
      p.layer_mode,
      p.fade_point,
      p.fade_end,
      p.dynamics,
      p.dynamics_curve,
      p.fixed_velocity,
      p.hi_hat_volume,
      p.hi_hat_fade_in,
      p.hi_hat_decay,
      p.trigger_reserve,
      args.commit ? " (committing)" : "");
  dev.SetPadLayerParams({.kit = args.kit, .pad = args.pad, .params = p});
  if (args.commit) {
    dev.Commit();
  }
  std::printf("done\n");
  return 0;
}

struct SetNameArgs {
  std::string port;
  int kit = 1;
  std::string name;
  bool dry_run = false;
  bool commit = false;
};

int RunSetName(const SetNameArgs& args) {
  if (args.name.empty()) {
    std::fprintf(stderr, "setname needs --name <text>\n");
    return 2;
  }
  if (args.dry_run) {
    std::printf("dry run: kit %d name <- \"%s\" (nothing sent)\n",
                args.kit,
                args.name.c_str());
    return 0;
  }
  const std::string port = ResolvePort(args.port);
  const std::unique_ptr<spdsx::device::SerialPort> serial =
      spdsx::device::PlatformPorts().Open(port);
  spdsx::device::SpdsxDevice dev(serial.get());
  std::printf("opened %s: kit %d name <- \"%s\"%s\n",
              port.c_str(),
              args.kit,
              args.name.c_str(),
              args.commit ? " (committing)" : " (working state)");
  const spdsx::ops::SetNameResult result =
      spdsx::ops::SetName(dev,
                          {.kit = args.kit,
                           .name = args.name,
                           .commit = args.commit,
                           .dry_run = false});
  if (args.commit && !result.committed) {
    std::fprintf(stderr, "commit did not confirm\n");
    return 1;
  }
  std::printf("done\n");
  return 0;
}

struct SetLayerArgs {
  std::string port;
  int kit = 1;
  std::string pad_spec;
  double volume_db = 0.0;
  int fade_in = 0;
  int decay = 127;
  // Which of the three the user actually gave. The rest are read off the
  // device and written back unchanged, so naming one does not reset the
  // other two.
  bool have_volume = false;
  bool have_fade_in = false;
  bool have_decay = false;
  bool dry_run = false;
  bool commit = false;
};

int RunSetLayer(const SetLayerArgs& args) {
  int pad = 0;
  PadSlot slot = PadSlot::kTop;
  if (args.pad_spec.empty() || !ParsePadSpec(args.pad_spec, &pad, &slot)) {
    std::fprintf(stderr,
                 "setlayer needs --pad <P.S> (P 1-9, S 0 top/1 bottom)\n");
    return 2;
  }
  if (args.fade_in < 0 || args.fade_in > 127 || args.decay < 0
      || args.decay > 127) {
    std::fprintf(stderr, "--fadein and --decay want 0-127\n");
    return 2;
  }
  // The device stores volume as a signed 16-bit in 0.1 dB units.
  int volume_db10 = static_cast<int>(std::lround(args.volume_db * 10.0));
  int fade_in = args.fade_in;
  int decay = args.decay;
  const bool complete =
      args.have_volume && args.have_fade_in && args.have_decay;
  if (args.dry_run) {
    char buf[64];
    std::string wanted;
    if (args.have_volume) {
      std::snprintf(buf, sizeof(buf), " volume %.1f dB", volume_db10 / 10.0);
      wanted += buf;
    }
    if (args.have_fade_in) {
      wanted += " fade-in " + std::to_string(fade_in);
    }
    if (args.have_decay) {
      wanted += " decay " + std::to_string(decay);
    }
    std::printf("dry run: kit %d pad %d %s <-%s%s (nothing sent)\n",
                args.kit,
                pad,
                slot == PadSlot::kTop ? "top" : "bottom",
                wanted.c_str(),
                complete ? "" : ", other fields left as they are");
    return 0;
  }
  const std::string port = ResolvePort(args.port);
  const std::unique_ptr<spdsx::device::SerialPort> serial =
      spdsx::device::PlatformPorts().Open(port);
  spdsx::device::SpdsxDevice dev(serial.get());
  // SetPadLayerMix writes all three, so anything the user left out has to
  // be read back first or it would be overwritten with a default.
  if (!complete) {
    dev.RequireSupportedFirmware();  // before the bank read, not after
    std::printf("opened %s, reading kit %d's current layer mix...\n",
                port.c_str(),
                args.kit);
    const auto kits = spdsx::device::ParseKits(
        spdsx::device::CleanBulkImage(dev.DumpBank(spdsx::device::kBankKits)));
    if (static_cast<size_t>(args.kit) > kits.size() || args.kit < 1) {
      std::fprintf(stderr, "couldn't read kit %d\n", args.kit);
      return 1;
    }
    const auto& pad_rec = kits[static_cast<size_t>(args.kit - 1)]
                              .pads[static_cast<size_t>(pad - 1)];
    const auto& mix =
        slot == PadSlot::kTop ? pad_rec.mix_top : pad_rec.mix_bottom;
    if (!args.have_volume) {
      volume_db10 = mix.volume_db10;
    }
    if (!args.have_fade_in) {
      fade_in = mix.fade_in;
    }
    if (!args.have_decay) {
      decay = mix.decay;
    }
  }
  std::printf(
      "%s: kit %d pad %d %s <- volume %.1f dB, fade-in %d, "
      "decay %d%s\n",
      port.c_str(),
      args.kit,
      pad,
      slot == PadSlot::kTop ? "top" : "bottom",
      volume_db10 / 10.0,
      fade_in,
      decay,
      args.commit ? " (committing)" : " (working state)");
  dev.SetPadLayerMix({.kit = args.kit,
                      .pad = pad,
                      .slot = slot,
                      .volume_db10 = volume_db10,
                      .fade_in = fade_in,
                      .decay = decay});
  if (args.commit) {
    dev.Commit();
  } else {
    dev.Ping();  // delivery barrier: fire-and-forget writes vs port close
  }
  std::printf("done\n");
  return 0;
}

struct SetModeArgs {
  std::string port;
  std::string mode;  // target layer mode name (MIX, FADE1, ... HI-HAT)
  std::string if_mode;  // only touch pads currently in this mode; "" = any
  std::vector<KitRange> ranges;  // default: all kits
  std::vector<int> pads;  // 1-9, repeatable; empty = every pad
  bool dry_run = false;
  bool commit = false;
};

// Sets the layer mode of pads in the given kits. The planning and the
// writing live in device_ops, shared with the bulk-operations window, so
// the two front ends cannot drift.
int RunSetMode(const SetModeArgs& args) {
  using spdsx::LayerMode;
  const auto parse_mode = [](const std::string& name, LayerMode* out) {
    const LayerMode m = spdsx::ParseLayerMode(name, LayerMode::kMix);
    if (spdsx::LayerModeName(m) != name) {
      return false;
    }
    *out = m;
    return true;
  };
  spdsx::ops::SetModeRequest request;
  if (!parse_mode(args.mode, &request.target)) {
    std::fprintf(stderr,
                 "setmode needs --mode of MIX, FADE1, FADE2, XFADE, SWITCH, "
                 "SW(MONO), ALTERNATE or HI-HAT\n");
    return 2;
  }
  request.has_if_mode = !args.if_mode.empty();
  if (request.has_if_mode && !parse_mode(args.if_mode, &request.if_mode)) {
    std::fprintf(stderr, "bad --if-mode \"%s\"\n", args.if_mode.c_str());
    return 2;
  }
  for (const int pad : args.pads) {
    if (pad < 1 || pad > 9) {
      std::fprintf(stderr, "bad --pad %d (pads are 1-9)\n", pad);
      return 2;
    }
  }
  request.kits = args.ranges;
  request.pads = args.pads;
  request.commit = args.commit;
  request.dry_run = args.dry_run;

  const std::string port = ResolvePort(args.port);
  const std::unique_ptr<spdsx::device::SerialPort> serial =
      spdsx::device::PlatformPorts().Open(port);
  spdsx::device::SpdsxDevice dev(serial.get());
  dev.RequireSupportedFirmware();  // before the bank read, not after
  std::printf("opened %s, reading the kits bank...\n", port.c_str());
  int last_done = -1;
  const spdsx::ops::SetModeResult result = spdsx::ops::SetMode(
      dev, request, [&last_done](const spdsx::ops::Progress& p) {
        if (p.total <= 0 || p.done == last_done) {
          return;
        }
        last_done = p.done;
        std::printf("\r  %d/%d written", p.done, p.total);
        std::fflush(stdout);
      });
  if (request.dry_run) {
    std::printf("%d pad(s) to set to %s (dry run, nothing sent)\n",
                result.changed,
                args.mode.c_str());
    return 0;
  }
  if (result.changed == 0) {
    std::printf("0 pad(s) to set to %s\n", args.mode.c_str());
    return 0;
  }
  std::printf("\n");
  if (args.commit && !result.committed) {
    std::fprintf(stderr, "commit did not confirm\n");
    return 1;
  }
  if (!args.commit) {
    std::printf("working state only (power cycle reverts; --commit to keep)\n");
  }
  std::printf("done\n");
  return 0;
}

int RunCurrentKit(const std::string& port_arg) {
  const std::string port = ResolvePort(port_arg);
  const std::unique_ptr<spdsx::device::SerialPort> serial =
      spdsx::device::PlatformPorts().Open(port);
  spdsx::device::SpdsxDevice dev(serial.get());
  const int kit = dev.CurrentKit();
  if (kit <= 0) {
    std::fprintf(stderr, "couldn't read the current kit\n");
    return 1;
  }
  std::printf("current kit: %d\n", kit);
  return 0;
}

int RunSelectKit(const std::string& port_arg, int kit) {
  // Deliberately the app's kit-follow shape in miniature: open, one
  // fire-and-forget DT1, close. The write must survive the close (the
  // port drains before closing) or the unit never sees it.
  const std::string port = ResolvePort(port_arg);
  const std::unique_ptr<spdsx::device::SerialPort> serial =
      spdsx::device::PlatformPorts().Open(port);
  spdsx::device::SpdsxDevice dev(serial.get());
  std::printf("opened %s, selecting kit %d...\n", port.c_str(), kit);
  dev.SelectKit(kit);
  // A same-connection barrier: the ping's round trip proves the select was
  // delivered and gives the device time to act before the port closes.
  dev.Ping();
  return 0;
}

int RunDeleteWave(const std::string& port_arg, int index) {
  const std::string port = ResolvePort(port_arg);
  const std::unique_ptr<spdsx::device::SerialPort> serial =
      spdsx::device::PlatformPorts().Open(port);
  spdsx::device::SpdsxDevice dev(serial.get());
  std::printf("opened %s, deleting sample %d (then flash commit)...\n",
              port.c_str(),
              index);
  dev.DeleteWave(index);
  std::printf("done\n");
  return 0;
}

int RunSamples(const std::string& port_arg, const std::string& from_path) {
  Bytes raw;
  if (!from_path.empty()) {
    raw = ReadFile(from_path);
  } else {
    const std::string port = ResolvePort(port_arg);
    const std::unique_ptr<spdsx::device::SerialPort> serial =
        spdsx::device::PlatformPorts().Open(port);
    spdsx::device::SpdsxDevice dev(serial.get());
    std::printf("opened %s, streaming bank 0x20...\n", port.c_str());
    raw = dev.DumpBank(spdsx::device::kBankSamples);
  }
  const Bytes clean = spdsx::device::CleanBulkImage(raw);
  const auto samples = spdsx::device::ParseSampleDir(clean);
  std::printf("%zu raw bytes -> %zu clean bytes -> %zu samples\n",
              raw.size(),
              clean.size(),
              samples.size());
  std::printf("%6s  %-16s  %-16s  %8s  %s\n",
              "index",
              "wavename",
              "category",
              "seconds",
              "filename");
  for (const auto& s : samples) {
    const auto cat = spdsx::device::SampleCategoryName(s.category);
    std::printf("%6d  %-16s  %-16.*s  %8.2f  %s\n",
                s.index,
                s.wavename.c_str(),
                static_cast<int>(cat.size()),
                cat.data(),
                static_cast<double>(s.frames) / 48000.0,
                s.filename.c_str());
  }
  return samples.empty() ? 1 : 0;
}

const char* kModeNames[] = {"MIX",
                            "FADE1",
                            "FADE2",
                            "XFADE",
                            "SWITCH",
                            "SW(MONO)",
                            "ALTERNATE",
                            "HI-HAT"};
const char* kCurveNames[] = {"LINEAR", "LOUD1", "LOUD2", "LOUD3"};

// Prints one kit's pads with the mapped params decoded.
struct KitShowArgs {
  std::string port;
  std::string from_path;
  int kit = 0;
};

int RunKit(const KitShowArgs& args) {
  const int kit = args.kit;
  if (kit < 1 || kit > spdsx::device::kBankKitCount) {
    std::fprintf(stderr, "kit must be 1-%d\n", spdsx::device::kBankKitCount);
    return 2;
  }
  Bytes raw;
  if (!args.from_path.empty()) {
    raw = ReadFile(args.from_path);
  } else {
    const std::string port = ResolvePort(args.port);
    const std::unique_ptr<spdsx::device::SerialPort> serial =
        spdsx::device::PlatformPorts().Open(port);
    spdsx::device::SpdsxDevice dev(serial.get());
    std::printf("opened %s, streaming bank 0x10...\n", port.c_str());
    raw = dev.DumpBank(spdsx::device::kBankKits);
  }
  const auto kits =
      spdsx::device::ParseKits(spdsx::device::CleanBulkImage(raw));
  if (static_cast<size_t>(kit) > kits.size()) {
    std::fprintf(stderr, "only %zu kits in the image\n", kits.size());
    return 1;
  }
  const auto& k = kits[static_cast<size_t>(kit - 1)];
  std::printf("kit %d  \"%s\"\n", kit, k.name.c_str());
  std::printf(
      "  pad  mode      fadeP fadeE  dyn curve   fixVel trigRsv"
      "  hhVol hhFadeIn hhDecay  link  top   bottom\n");
  for (int pad = 0; pad < spdsx::device::kPadsPerKit; ++pad) {
    const auto& p = k.pads[static_cast<size_t>(pad)];
    const char* mode = p.layer_mode < 8 ? kModeNames[p.layer_mode] : "?";
    const char* curve =
        p.dynamics_curve < 4 ? kCurveNames[p.dynamics_curve] : "?";
    std::printf(
        "  %3d  %-9s %5d %5d  %-3s %-7s %5d  %-7s %5d %8d %7d %5d %5d %5d\n",
        pad + 1,
        mode,
        p.fade_point,
        p.fade_end,
        p.dynamics ? "ON" : "OFF",
        curve,
        p.fixed_velocity,
        p.trigger_reserve ? "ON" : "OFF",
        p.hi_hat_volume,
        p.hi_hat_fade_in,
        p.hi_hat_decay,
        p.pad_link,
        p.wave_top,
        p.wave_bottom);
  }
  return 0;
}

struct PadLinkArgs {
  std::string port;
  int group = 0;
  std::vector<std::pair<ObjectKind, int>> objects;
  std::vector<KitRange> ranges;
  bool dry_run = false;
  bool verbose = false;
};

int RunPadLink(const PadLinkArgs& args) {
  const int group = args.group;
  const auto& objects = args.objects;
  const bool dry_run = args.dry_run;
  const bool verbose = args.verbose;
  const std::vector<KitRange>& ranges = args.ranges;

  // Declared before the device so it outlives it: the device only borrows.
  std::unique_ptr<spdsx::device::SerialPort> serial;
  spdsx::device::SpdsxDevice* dev = nullptr;
  std::unique_ptr<spdsx::device::SpdsxDevice> owned;
  if (!dry_run) {
    const std::string port = ResolvePort(args.port);
    std::printf("About to WRITE to %s (group %d):", port.c_str(), group);
    for (const auto& [kind, index] : objects) {
      std::printf(" %s%d", KindName(kind), index);
    }
    std::printf(" in kits");
    for (const auto& r : ranges) {
      if (r.first == r.last) {
        std::printf(" %d", r.first);
      } else {
        std::printf(" %d-%d", r.first, r.last);
      }
    }
    std::printf(".\nType 'yes' to proceed: ");
    std::fflush(stdout);
    std::string answer;
    std::getline(std::cin, answer);
    std::transform(answer.begin(), answer.end(), answer.begin(), ::tolower);
    while (!answer.empty() && std::isspace((unsigned char)answer.back())) {
      answer.pop_back();
    }
    if (answer != "yes") {
      std::printf("Aborted.\n");
      return 1;
    }
    serial = spdsx::device::PlatformPorts().Open(port);
    owned = std::make_unique<spdsx::device::SpdsxDevice>(serial.get());
    dev = owned.get();
    const Bytes r = dev->Ping();
    if (r.empty()) {
      throw std::runtime_error("device did not respond to ping");
    }
    std::printf("ping ok: %s\n", ToHex(r).c_str());
  }

  int messages = 0;
  char buf[64];
  for (const auto& range : ranges) {
    for (int kit = range.first; kit <= range.last; ++kit) {
      const Bytes sel = spdsx::device::Dt1(spdsx::device::kKitSelectAddr,
                                           spdsx::device::EncodeKit(kit));
      std::snprintf(buf, sizeof(buf), "kit %3d select   : ", kit);
      std::printf("%s%s\n", buf, ToHex(sel).c_str());
      ++messages;
      if (dev != nullptr) {
        const Bytes rep = dev->Command(sel);
        if (verbose) {
          std::printf("    <- %s\n",
                      rep.empty() ? "(no reply)" : ToHex(rep).c_str());
        }
      }
      for (const auto& [kind, index] : objects) {
        const Bytes foc =
            spdsx::device::Dt1(spdsx::device::kObjectSelectAddr,
                               {spdsx::device::SelectValue(kind, index)});
        const Bytes wr =
            spdsx::device::Dt1(spdsx::device::PadLinkAddr(
                                   {.kind = kind, .index = index, .kit = kit}),
                               {static_cast<uint8_t>(group & 0x7F)});
        std::printf("        focus %s%d : %s\n",
                    KindName(kind),
                    index,
                    ToHex(foc).c_str());
        std::printf("        write %s%d : %s\n",
                    KindName(kind),
                    index,
                    ToHex(wr).c_str());
        messages += 2;
        if (dev != nullptr) {
          const Bytes rep = dev->Command(foc);
          if (verbose) {
            std::printf("    <- %s\n",
                        rep.empty() ? "(no reply)" : ToHex(rep).c_str());
          }
          dev->Send(wr);
          std::this_thread::sleep_for(std::chrono::duration<double>(0.02));
        }
      }
    }
  }
  std::printf("\n%s  %d messages.\n",
              dry_run ? "(dry run \xe2\x80\x94 nothing sent)" : "Done.",
              messages);
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  std::string port;  // empty = auto-detect via ResolvePort
  std::string command;
  int group = -1;
  std::vector<std::pair<ObjectKind, int>> objects;
  std::vector<KitRange> ranges;
  std::vector<uint8_t> banks;
  std::string out_path;
  std::string verify_path;
  std::string from_path;
  std::vector<std::string> from_paths;
  std::string name_arg;
  std::string pad_spec;
  std::string params_spec;
  int sample_arg = -1;
  int pad_num = 0;
  int kit_arg = 0;
  std::string mode_arg;
  std::string if_mode_arg;
  double volume_arg = 0.0;
  bool have_volume = false;
  int fadein_arg = 0;
  int decay_arg = 127;
  bool commit_flag = false;
  bool dry_run = false;
  bool verbose = false;

  // Every flag the parse loop saw, so the command can reject any it does
  // not read.
  std::vector<std::string> seen_flags;

  try {
    for (int i = 1; i < argc; ++i) {
      const std::string arg = argv[i];
      auto next = [&]() -> std::string {
        if (i + 1 >= argc) {
          throw std::runtime_error(arg + " needs a value");
        }
        return argv[++i];
      };
      if (arg.rfind("--", 0) == 0) {
        seen_flags.push_back(arg.substr(2));
      }
      if (arg == "--version") {
        std::printf("spdutil %s (SPD-SX PROgram)\n", SPDSX_VERSION);
        return 0;
      } else if (arg == "--port") {
        port = next();
      } else if (arg == "--group") {
        group = std::atoi(next().c_str());
      } else if (arg == "--trigger") {
        objects.emplace_back(ObjectKind::kTrig, std::atoi(next().c_str()));
      } else if (arg == "--pad") {
        // Dotted "P.S" is the assign pad+slot spec; a bare number is a
        // padlink pad object.
        const std::string v = next();
        if (v.find('.') != std::string::npos) {
          pad_spec = v;
        } else {
          pad_num = std::atoi(v.c_str());  // setparams uses a plain pad num
          objects.emplace_back(ObjectKind::kPad, pad_num);
        }
      } else if (arg == "--range") {
        throw std::runtime_error(
            "--range is now --kits, which takes comma-separated ranges: "
            "--kits 108-200, --kits 1,5,10-20");
      } else if (arg == "--kits") {
        std::string spec_error;
        std::vector<KitRange> parsed;
        if (!spdsx::spdutil::ParseKitSpec(next(), &parsed, &spec_error)) {
          throw std::runtime_error("--kits: " + spec_error);
        }
        ranges.insert(ranges.end(), parsed.begin(), parsed.end());
      } else if (arg == "--bank") {
        banks.push_back(
            static_cast<uint8_t>(std::strtol(next().c_str(), nullptr, 0)));
      } else if (arg == "--all") {
        banks = {spdsx::device::kBankKits,
                 spdsx::device::kBankSamples,
                 spdsx::device::kBankMeta,
                 spdsx::device::kBankConfig};
      } else if (arg == "--out") {
        out_path = next();
      } else if (arg == "--verify") {
        verify_path = next();
      } else if (arg == "--from") {
        from_path = next();
        from_paths.push_back(from_path);
      } else if (arg == "--name") {
        name_arg = next();
      } else if (arg == "--sample") {
        sample_arg = std::atoi(next().c_str());
      } else if (arg == "--params") {
        params_spec = next();
      } else if (arg == "--mode") {
        mode_arg = next();
      } else if (arg == "--if-mode") {
        if_mode_arg = next();
      } else if (arg == "--volume") {
        volume_arg = std::atof(next().c_str());
        have_volume = true;
      } else if (arg == "--fadein") {
        fadein_arg = std::atoi(next().c_str());
      } else if (arg == "--decay") {
        decay_arg = std::atoi(next().c_str());
      } else if (arg == "--commit") {
        commit_flag = true;
      } else if (arg == "--dry-run") {
        dry_run = true;
      } else if (arg == "--verbose") {
        verbose = true;
      } else if (!arg.empty() && arg[0] != '-' && command.empty()) {
        command = arg;
      } else if (!arg.empty() && (std::isdigit((unsigned char)arg[0]))
                 && kit_arg == 0) {
        kit_arg = std::atoi(arg.c_str());  // positional kit number
      } else {
        return Usage();
      }
    }

    // The one kit a single-kit write targets (validated just below).
    const int single_kit = ranges.empty() ? 0 : ranges.front().first;

    const auto flag_given = [&seen_flags](const char* name) {
      return std::find(seen_flags.begin(), seen_flags.end(), name)
          != seen_flags.end();
    };

    // Reject flags this command does not read, rather than dropping them:
    // a flag that parses and vanishes is how "setmode --pad 9" came to
    // sweep all nine pads.
    if (!command.empty() && spdsx::spdutil::IsCommand(command)) {
      const std::string bad =
          spdsx::spdutil::UnacceptedFlag(command, seen_flags);
      if (!bad.empty()) {
        std::fprintf(stderr,
                     "--%s does nothing for \"%s\": %s\n",
                     bad.c_str(),
                     command.c_str(),
                     spdsx::spdutil::FlagRejectionHint(command, bad).c_str());
        return 2;
      }
      if (kit_arg > 0 && !spdsx::spdutil::TakesPositionalNumber(command)) {
        std::fprintf(stderr,
                     "\"%s\" takes no number (got %d)\n",
                     command.c_str(),
                     kit_arg);
        return 2;
      }
      // Say which kits, always: a forgotten argument must not write
      // somewhere nobody named.
      if (spdsx::spdutil::RequiresKitSpec(command) && ranges.empty()) {
        std::fprintf(
            stderr,
            "\"%s\" needs --kits: one or more kits as comma-separated "
            "ranges,\ne.g. --kits 108, --kits 108-200, --kits 1,5,10-20 "
            "(--kits 1-200 is every kit)\n",
            command.c_str());
        return 2;
      }
      // These write exactly one kit, so their spec may name only one.
      if (spdsx::spdutil::TakesSingleKit(command)
          && spdsx::spdutil::KitCount(ranges) != 1) {
        std::fprintf(stderr,
                     "\"%s\" writes one kit, but --kits named %d\n",
                     command.c_str(),
                     spdsx::spdutil::KitCount(ranges));
        return 2;
      }
    }

    if (command == "ping") {
      return RunPing(port);
    }
    if (command == "info") {
      return RunInfo(port);
    }
    if (command == "dump") {
      if (verify_path.empty() && banks.empty()) {
        banks = {spdsx::device::kBankKits};  // the useful default
      }
      return RunDump({.port = port,
                      .banks = banks,
                      .out_path = out_path,
                      .verify_path = verify_path});
    }
    if (command == "kits") {
      return RunKits(port, from_path);
    }
    if (command == "samples") {
      return RunSamples(port, from_path);
    }
    if (command == "kit") {
      return RunKit({.port = port, .from_path = from_path, .kit = kit_arg});
    }
    if (command == "readwave") {
      if (kit_arg <= 0) {
        std::fprintf(stderr, "readwave needs a sample index\n");
        return 2;
      }
      return RunReadWave(
          {.port = port, .index = kit_arg, .out_path = out_path});
    }
    if (command == "setlayer") {
      return RunSetLayer({.port = port,
                          .kit = single_kit,
                          .pad_spec = pad_spec,
                          .volume_db = volume_arg,
                          .fade_in = fadein_arg,
                          .decay = decay_arg,
                          .have_volume = have_volume,
                          .have_fade_in = flag_given("fadein"),
                          .have_decay = flag_given("decay"),
                          .dry_run = dry_run,
                          .commit = commit_flag});
    }
    if (command == "selectkit") {
      if (kit_arg <= 0) {
        std::fprintf(stderr, "selectkit needs a kit number (1-200)\n");
        return 2;
      }
      return RunSelectKit(port, kit_arg);
    }
    if (command == "currentkit") {
      return RunCurrentKit(port);
    }
    if (command == "setmode") {
      // A dotted --pad P.S lands in pad_spec, where setmode never looks;
      // it would read as "no filter" and sweep every pad.
      if (!pad_spec.empty()) {
        std::fprintf(stderr,
                     "setmode wants --pad N (a pad, 1-9), not \"%s\"\n",
                     pad_spec.c_str());
        return 2;
      }
      // --pad N is repeatable and lands in `objects`.
      std::vector<int> mode_pads;
      for (const auto& [kind, index] : objects) {
        if (kind == ObjectKind::kPad) {
          mode_pads.push_back(index);
        }
      }
      return RunSetMode({.port = port,
                         .mode = mode_arg,
                         .if_mode = if_mode_arg,
                         .ranges = ranges,
                         .pads = mode_pads,
                         .dry_run = dry_run,
                         .commit = commit_flag});
    }
    if (command == "deletewave") {
      if (kit_arg <= 0) {
        std::fprintf(stderr, "deletewave needs a sample index\n");
        return 2;
      }
      return RunDeleteWave(port, kit_arg);
    }
    if (command == "sendwave") {
      if (kit_arg <= 0) {
        std::fprintf(stderr, "sendwave needs a target index\n");
        return 2;
      }
      return RunSendWave({.port = port,
                          .index = kit_arg,
                          .from_paths = from_paths,
                          .name = name_arg});
    }
    if (command == "assign") {
      return RunAssign({.port = port,
                        .kit = single_kit,
                        .sample = sample_arg,
                        .pad_spec = pad_spec,
                        .dry_run = dry_run,
                        .commit = commit_flag});
    }
    if (command == "setname") {
      return RunSetName({.port = port,
                         .kit = single_kit,
                         .name = name_arg,
                         .dry_run = dry_run,
                         .commit = commit_flag});
    }
    if (command == "setparams") {
      return RunSetParams({.port = port,
                           .kit = single_kit,
                           .pad = pad_num,
                           .params_spec = params_spec,
                           .dry_run = dry_run,
                           .commit = commit_flag});
    }
    if (command == "padlink") {
      if (group < 0) {
        std::fprintf(stderr, "padlink needs --group\n");
        return 2;
      }
      if (objects.empty()) {
        std::fprintf(stderr, "padlink needs at least one --trigger or --pad\n");
        return 2;
      }
      return RunPadLink({.port = port,
                         .group = group,
                         .objects = objects,
                         .ranges = ranges,
                         .dry_run = dry_run,
                         .verbose = verbose});
    }
    return command.empty() ? Usage() : UnknownCommand(command);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "error: %s\n", e.what());
    return 1;
  }
}
