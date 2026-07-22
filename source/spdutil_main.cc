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

namespace {

using spdsx::device::BulkBlock;
using spdsx::device::Bytes;
using spdsx::device::ObjectKind;
using spdsx::device::PadSlot;

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

// A closed kit range; --range 126 is {126,126}, --range 129-134 is
// {129,134}.
struct KitRange {
  int first;
  int last;
};

KitRange ParseRange(const std::string& s) {
  const auto dash = s.find('-');
  KitRange r {};
  if (dash == std::string::npos) {
    r.first = r.last = std::atoi(s.c_str());
  } else {
    r.first = std::atoi(s.substr(0, dash).c_str());
    r.last = std::atoi(s.substr(dash + 1).c_str());
  }
  if (r.first < 1 || r.last > 200 || r.first > r.last) {
    throw std::runtime_error("bad --range '" + s + "' (kits are 1-200)");
  }
  return r;
}

int Usage() {
  std::fprintf(
      stderr,
      "usage: spdutil [--port <dev>] <command> [options]\n"
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
      "  deletewave <N>  delete sample N from the pool + commit\n"
      "                  (DESTRUCTIVE, not undoable on the device)\n"
      "  sendwave <N> --from F.smp [--from G.smp ...] [--name X.wav]\n"
      "                  upload one or more waves on one connection to\n"
      "                  consecutive indices from N: write each file AND\n"
      "                  register it in the pool, then read every one back\n"
      "                  and report MATCH/FAIL (use a fresh index; --name\n"
      "                  applies only to a single file)\n"
      "  assign [K] --sample N --pad P.S   assign pool sample N to kit K\n"
      "                  (default 1), pad P (1-9), slot S (0 top/1 bottom);\n"
      "                  e.g. --pad 2.1 = pad 2 bottom. Working state only,\n"
      "                  not committed (revert with a power cycle)\n"
      "  padlink     put triggers/pads into a pad-link group:\n"
      "                --group N        link group (required)\n"
      "                --trigger N      link trigger N\n"
      "                --pad N          link pad N\n"
      "                --range A[-B]    kits to touch (repeatable;\n"
      "                                 default all, 1-200)\n"
      "                --dry-run        print, send nothing\n"
      "                --verbose        show device replies\n"
      "\n"
      "with no --port, scans /dev/cu.usbmodem* and pings each node\n");
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

int RunDump(const std::string& port_arg,
            const std::vector<uint8_t>& banks,
            const std::string& out_path,
            const std::string& verify_path) {
  // Offline: just report an existing image's structure.
  if (!verify_path.empty()) {
    ReportBlocks(ReadFile(verify_path));
    return 0;
  }

  const std::string port = ResolvePort(port_arg);
  const std::unique_ptr<spdsx::device::SerialPort> serial =
      spdsx::device::PlatformPorts().Open(port);
  spdsx::device::SpdsxDevice dev(serial.get());
  std::printf("opened %s\n", port.c_str());

  Bytes image;
  for (uint8_t bank : banks) {
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
  if (!out_path.empty()) {
    if (!WriteFile(out_path, image)) {
      std::fprintf(stderr, "error: couldn't write %s\n", out_path.c_str());
      return 1;
    }
    std::printf("wrote %s\n", out_path.c_str());
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

int RunReadWave(const std::string& port_arg,
                int index,
                const std::string& out_path) {
  const std::string port = ResolvePort(port_arg);
  const std::unique_ptr<spdsx::device::SerialPort> serial =
      spdsx::device::PlatformPorts().Open(port);
  spdsx::device::SpdsxDevice dev(serial.get());
  std::printf("opened %s, reading wave %d (%s)...\n",
              port.c_str(),
              index,
              spdsx::device::RemoteWavePath(index).c_str());
  const Bytes smp = dev.ReadRemoteWave(index, [](size_t done, size_t total) {
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
  if (!out_path.empty()) {
    // A .wav path gets the converted WAV; anything else the raw .SMP.
    const bool as_wav = out_path.size() > 4
        && out_path.compare(out_path.size() - 4, 4, ".wav") == 0;
    const Bytes data = as_wav ? spdsx::device::RfwvToWav(smp) : smp;
    if (data.empty() || !WriteFile(out_path, data)) {
      std::fprintf(stderr, "couldn't write %s\n", out_path.c_str());
      return 1;
    }
    std::printf(
        "wrote %s (%s)\n", out_path.c_str(), as_wav ? "wav" : "raw smp");
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
int RunSendWave(const std::string& port_arg,
                int index,
                const std::vector<std::string>& from_paths,
                const std::string& name_arg) {
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

  const std::string port = ResolvePort(port_arg);
  const std::unique_ptr<spdsx::device::SerialPort> serial =
      spdsx::device::PlatformPorts().Open(port);
  spdsx::device::SpdsxDevice dev(serial.get());
  std::printf("opened %s: uploading %zu file(s) from index %d...\n",
              port.c_str(),
              from_paths.size(),
              index);

  // Phase 1: upload every file on the one connection (as the GUI sync
  // does), all into working state, then ONE flash commit — the official
  // app's batch model. No per-file readback: a file read immediately after
  // its own commit can miss before the flash settles.
  dev.PrepareUploadBatch();
  for (size_t i = 0; i < from_paths.size(); ++i) {
    const int idx = index + static_cast<int>(i);
    const auto [wavename, filename] = DeriveWaveNames(
        from_paths[i], from_paths.size() == 1 ? name_arg : std::string());
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
  // The flash commit's duration scales with the batch; give it headroom.
  if (!dev.Commit(30.0 + 6.0 * static_cast<double>(from_paths.size()))) {
    std::fprintf(stderr, "commit did not confirm\n");
    return 1;
  }

  // Phase 2: read each back and compare. A wave that didn't persist either
  // has no exportable file (ReadRemoteWave throws) or reads back short.
  std::printf("verifying...\n");
  int failures = 0;
  for (size_t i = 0; i < from_paths.size(); ++i) {
    const int idx = index + static_cast<int>(i);
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

int RunAssign(const std::string& port_arg,
              int kit,
              int sample,
              const std::string& pad_spec,
              bool commit) {
  if (sample < 0 || pad_spec.empty()) {
    std::fprintf(stderr,
                 "assign needs --sample <index> and --pad <P.S> "
                 "(e.g. --pad 2.1 = pad 2 bottom slot)\n");
    return 2;
  }
  int pad = 0;
  PadSlot slot = PadSlot::kTop;
  if (!ParsePadSpec(pad_spec, &pad, &slot)) {
    std::fprintf(stderr,
                 "bad --pad \"%s\"; want P.S with P 1-9, S 0(top)"
                 "/1(bottom)\n",
                 pad_spec.c_str());
    return 2;
  }
  const std::string port = ResolvePort(port_arg);
  const std::unique_ptr<spdsx::device::SerialPort> serial =
      spdsx::device::PlatformPorts().Open(port);
  spdsx::device::SpdsxDevice dev(serial.get());
  std::printf("opened %s: kit %d pad %d %s <- sample %d%s\n",
              port.c_str(),
              kit,
              pad,
              slot == PadSlot::kTop ? "top" : "bottom",
              sample,
              commit ? " (committing)" : " (working state)");
  dev.SetPadWave(kit, pad, slot, sample);
  if (commit) {
    dev.Commit();
  }
  std::printf("done\n");
  return 0;
}

// Parses a comma list "mode,fp,fe,dyn,curve,fixvel,hhvol,hhfadein,hhdecay,
// trig" into pad params. Returns false if the count is wrong.
bool ParseParamList(const std::string& spec,
                    spdsx::device::PadDeviceParams* p) {
  std::vector<int> v;
  size_t start = 0;
  while (start <= spec.size()) {
    const size_t comma = spec.find(',', start);
    const std::string tok = spec.substr(
        start, comma == std::string::npos ? std::string::npos : comma - start);
    v.push_back(std::atoi(tok.c_str()));
    if (comma == std::string::npos) {
      break;
    }
    start = comma + 1;
  }
  if (v.size() != 10) {
    return false;
  }
  p->layer_mode = static_cast<uint8_t>(v[0]);
  p->fade_point = static_cast<uint8_t>(v[1]);
  p->fade_end = static_cast<uint8_t>(v[2]);
  p->dynamics = static_cast<uint8_t>(v[3]);
  p->dynamics_curve = static_cast<uint8_t>(v[4]);
  p->fixed_velocity = static_cast<uint8_t>(v[5]);
  p->hi_hat_volume = static_cast<uint8_t>(v[6]);
  p->hi_hat_fade_in = static_cast<uint8_t>(v[7]);
  p->hi_hat_decay = static_cast<uint8_t>(v[8]);
  p->trigger_reserve = static_cast<uint8_t>(v[9]);
  return true;
}

int RunSetParams(const std::string& port_arg,
                 int kit,
                 int pad,
                 const std::string& params_spec,
                 bool commit) {
  if (pad < 1 || pad > 9 || params_spec.empty()) {
    std::fprintf(stderr,
                 "setparams needs --pad <1-9> and --params "
                 "mode,fp,fe,dyn,curve,fixvel,hhvol,hhfadein,hhdecay,trig\n");
    return 2;
  }
  spdsx::device::PadDeviceParams p;
  if (!ParseParamList(params_spec, &p)) {
    std::fprintf(stderr, "--params needs 10 comma-separated values\n");
    return 2;
  }
  const std::string port = ResolvePort(port_arg);
  const std::unique_ptr<spdsx::device::SerialPort> serial =
      spdsx::device::PlatformPorts().Open(port);
  spdsx::device::SpdsxDevice dev(serial.get());
  std::printf(
      "opened %s: kit %d pad %d params <- mode=%d fp=%d fe=%d "
      "dyn=%d curve=%d fixvel=%d hh(%d,%d,%d) trig=%d%s\n",
      port.c_str(),
      kit,
      pad,
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
      commit ? " (committing)" : "");
  dev.SetPadLayerParams(kit, pad, p);
  if (commit) {
    dev.Commit();
  }
  std::printf("done\n");
  return 0;
}

int RunSetName(const std::string& port_arg,
               int kit,
               const std::string& name,
               bool commit) {
  if (name.empty()) {
    std::fprintf(stderr, "setname needs --name <text>\n");
    return 2;
  }
  const std::string port = ResolvePort(port_arg);
  const std::unique_ptr<spdsx::device::SerialPort> serial =
      spdsx::device::PlatformPorts().Open(port);
  spdsx::device::SpdsxDevice dev(serial.get());
  std::printf("opened %s: kit %d name <- \"%s\"%s\n",
              port.c_str(),
              kit,
              name.c_str(),
              commit ? " (committing)" : " (working state)");
  dev.SetKitName(kit, name);
  if (commit) {
    dev.Commit();
  }
  std::printf("done\n");
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
int RunKit(const std::string& port_arg, const std::string& from_path, int kit) {
  if (kit < 1 || kit > spdsx::device::kBankKitCount) {
    std::fprintf(stderr, "kit must be 1-%d\n", spdsx::device::kBankKitCount);
    return 2;
  }
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
      "  hhVol hhFadeIn hhDecay  top   bottom\n");
  for (int pad = 0; pad < spdsx::device::kPadsPerKit; ++pad) {
    const auto& p = k.pads[static_cast<size_t>(pad)];
    const char* mode = p.layer_mode < 8 ? kModeNames[p.layer_mode] : "?";
    const char* curve =
        p.dynamics_curve < 4 ? kCurveNames[p.dynamics_curve] : "?";
    std::printf(
        "  %3d  %-9s %5d %5d  %-3s %-7s %5d  %-7s %5d %8d %7d %5d %5d\n",
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
        p.wave_top,
        p.wave_bottom);
  }
  return 0;
}

int RunPadLink(const std::string& port_arg,
               int group,
               const std::vector<std::pair<ObjectKind, int>>& objects,
               std::vector<KitRange> ranges,
               bool dry_run,
               bool verbose) {
  if (ranges.empty()) {
    ranges.push_back({1, 200});
  }

  // Declared before the device so it outlives it: the device only borrows.
  std::unique_ptr<spdsx::device::SerialPort> serial;
  spdsx::device::SpdsxDevice* dev = nullptr;
  std::unique_ptr<spdsx::device::SpdsxDevice> owned;
  if (!dry_run) {
    const std::string port = ResolvePort(port_arg);
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
            spdsx::device::Dt1(spdsx::device::PadLinkAddr(kind, index, kit),
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
  bool commit_flag = false;
  bool dry_run = false;
  bool verbose = false;

  try {
    for (int i = 1; i < argc; ++i) {
      const std::string arg = argv[i];
      auto next = [&]() -> std::string {
        if (i + 1 >= argc) {
          throw std::runtime_error(arg + " needs a value");
        }
        return argv[++i];
      };
      if (arg == "--port") {
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
        ranges.push_back(ParseRange(next()));
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
      return RunDump(port, banks, out_path, verify_path);
    }
    if (command == "kits") {
      return RunKits(port, from_path);
    }
    if (command == "samples") {
      return RunSamples(port, from_path);
    }
    if (command == "kit") {
      return RunKit(port, from_path, kit_arg);
    }
    if (command == "readwave") {
      if (kit_arg <= 0) {
        std::fprintf(stderr, "readwave needs a sample index\n");
        return 2;
      }
      return RunReadWave(port, kit_arg, out_path);
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
      return RunSendWave(port, kit_arg, from_paths, name_arg);
    }
    if (command == "assign") {
      return RunAssign(
          port, kit_arg > 0 ? kit_arg : 1, sample_arg, pad_spec, commit_flag);
    }
    if (command == "setname") {
      return RunSetName(port, kit_arg > 0 ? kit_arg : 1, name_arg, commit_flag);
    }
    if (command == "setparams") {
      return RunSetParams(
          port, kit_arg > 0 ? kit_arg : 1, pad_num, params_spec, commit_flag);
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
      return RunPadLink(port, group, objects, ranges, dry_run, verbose);
    }
    return Usage();
  } catch (const std::exception& e) {
    std::fprintf(stderr, "error: %s\n", e.what());
    return 1;
  }
}
