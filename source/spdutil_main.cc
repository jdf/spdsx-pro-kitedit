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
//   selftest    offline byte-exact message checks (no device needed)
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

using spdsx::device::Bytes;
using spdsx::device::BulkBlock;
using spdsx::device::ObjectKind;

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

int HexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

Bytes FromHex(const std::string& s) {
  Bytes b;
  int hi = -1;
  for (char c : s) {
    const int v = HexVal(c);
    if (v < 0) {
      continue;
    }
    if (hi < 0) {
      hi = v;
    } else {
      b.push_back(static_cast<uint8_t>(hi * 16 + v));
      hi = -1;
    }
  }
  return b;
}

const char* KindName(ObjectKind kind) {
  return kind == ObjectKind::kPad ? "pad" : "trig";
}

// Checks built messages against captures lifted from spdsx.py's __main__.
int RunSelfTest() {
  bool all_ok = true;

  struct KitCase {
    int kit;
    const char* hex;
  };
  const KitCase kit_cases[] = {
      {3, "f0 41 10 00 00 00 00 16 12 00 00 00 00 00 00 00 02 7e f7"},
      {128, "f0 41 10 00 00 00 00 16 12 00 00 00 00 00 00 07 0f 6a f7"},
      {129, "f0 41 10 00 00 00 00 16 12 00 00 00 00 00 00 08 00 78 f7"},
      {130, "f0 41 10 00 00 00 00 16 12 00 00 00 00 00 00 08 01 77 f7"},
      {131, "f0 41 10 00 00 00 00 16 12 00 00 00 00 00 00 08 02 76 f7"},
      {200, "f0 41 10 00 00 00 00 16 12 00 00 00 00 00 00 0c 07 6d f7"},
  };
  for (const auto& c : kit_cases) {
    const Bytes built =
        spdsx::device::Dt1(spdsx::device::kKitSelectAddr,
            spdsx::device::EncodeKit(c.kit));
    const bool ok = built == FromHex(c.hex);
    all_ok = all_ok && ok;
    std::printf("kit %3d: %-8s %s\n", c.kit, ok ? "MATCH" : "MISMATCH",
        ToHex(built).c_str());
  }

  std::printf("\n--- pad-link captures (kit-encoded address) ---\n");
  struct LinkCase {
    int kit;
    ObjectKind kind;
    int index;
    int group;
    const char* hex;
  };
  const LinkCase link_cases[] = {
      {5, ObjectKind::kTrig, 7, 3,
          "f0 41 10 00 00 00 00 16 12 04 08 2f 0c 03 36 f7"},
      {10, ObjectKind::kTrig, 7, 5,
          "f0 41 10 00 00 00 00 16 12 04 12 2f 0c 05 2a f7"},
      {20, ObjectKind::kTrig, 7, 5,
          "f0 41 10 00 00 00 00 16 12 04 26 2f 0c 05 16 f7"},
      {200, ObjectKind::kTrig, 7, 1,
          "f0 41 10 00 00 00 00 16 12 07 0e 2f 0c 01 2f f7"},
      {200, ObjectKind::kPad, 7, 11,
          "f0 41 10 00 00 00 00 16 12 07 0e 26 0d 0b 2d f7"},
  };
  for (const auto& c : link_cases) {
    const Bytes built = spdsx::device::Dt1(
        spdsx::device::PadLinkAddr(c.kind, c.index, c.kit),
        {static_cast<uint8_t>(c.group)});
    const bool ok = built == FromHex(c.hex);
    all_ok = all_ok && ok;
    std::printf("kit %3d %s%d grp%2d: %-8s %s\n", c.kit, KindName(c.kind),
        c.index, c.group, ok ? "MATCH" : "MISMATCH", ToHex(built).c_str());
  }

  std::printf("\n--- parameter writes (kit name, pad wave) ---\n");
  auto check = [&](bool ok, const char* what, const Bytes& built) {
    all_ok = all_ok && ok;
    std::printf("%-8s %-22s %s\n", ok ? "MATCH" : "MISMATCH", what,
        ToHex(built).c_str());
  };
  // Bulk-read request (captured: stream-a-bank on the 41 6c family).
  check(spdsx::device::BulkReadRequest(0x10)
          == FromHex("f0 41 6c 03 05 00 00 00 00 10 00 00 00 00 00 00 f7"),
      "bulk read bank 0x10", spdsx::device::BulkReadRequest(0x10));
  check(spdsx::device::BulkReadRequest(0x20)
          == FromHex("f0 41 6c 03 05 00 00 00 00 20 00 00 00 00 00 00 f7"),
      "bulk read bank 0x20", spdsx::device::BulkReadRequest(0x20));
  // Nibble encoding (captured: sample 127 -> 00 00 07 0f, 203 -> 00 00 0c 0b).
  check(spdsx::device::NibbleEncode(127) == FromHex("00 00 07 0f"),
      "nibble(127)", spdsx::device::NibbleEncode(127));
  check(spdsx::device::NibbleEncode(203) == FromHex("00 00 0c 0b"),
      "nibble(203)", spdsx::device::NibbleEncode(203));
  // Kit-name char writes (kit 129 -> 'Z' at index 0 and 15).
  auto name_msg = [](int i) {
    return spdsx::device::Dt1(spdsx::device::KitNameAddr(i), {0x5a});
  };
  check(name_msg(0) == FromHex("f0 41 10 00 00 00 00 16 12 06 00 00 00 5a 20 f7"),
      "name[0]='Z'", name_msg(0));
  check(name_msg(15) == FromHex("f0 41 10 00 00 00 00 16 12 06 00 00 0f 5a 11 f7"),
      "name[15]='Z'", name_msg(15));
  // Pad-7 focus and top/bottom wave assignment (127 / 203).
  const Bytes focus = spdsx::device::Dt1(spdsx::device::kObjectSelectAddr,
      {spdsx::device::SelectValue(ObjectKind::kPad, 7)});
  check(focus == FromHex("f0 41 10 00 00 00 00 16 12 28 00 00 00 06 52 f7"),
      "focus pad7", focus);
  const Bytes top = spdsx::device::Dt1(
      spdsx::device::PadWaveAddr(spdsx::device::PadSlot::kTop),
      spdsx::device::NibbleEncode(127));
  check(top == FromHex("f0 41 10 00 00 00 00 16 12 06 00 4c 01 00 00 07 0f 17 f7"),
      "pad7 top wave 127", top);
  const Bytes bot = spdsx::device::Dt1(
      spdsx::device::PadWaveAddr(spdsx::device::PadSlot::kBottom),
      spdsx::device::NibbleEncode(203));
  check(bot == FromHex("f0 41 10 00 00 00 00 16 12 06 00 4d 01 00 00 0c 0b 15 f7"),
      "pad7 bottom wave 203", bot);

  std::printf("\n--- bulk image split ---\n");
  // A synthetic two-block image: bank 0x10 then 0x20, so the splitter's
  // boundary + bank-id logic is checked without the cached image file.
  Bytes img = FromHex("f0 41 6c 02 00 00 00 00 10 aa bb cc f7");
  const Bytes second = FromHex("f0 41 6c 02 00 00 00 00 20 de ad f7");
  img.insert(img.end(), second.begin(), second.end());
  const auto blocks = spdsx::device::SplitBulkImage(img);
  const bool split_ok = blocks.size() == 2 && blocks[0].bank == 0x10
      && blocks[0].offset == 0 && blocks[0].length == 13
      && blocks[1].bank == 0x20 && blocks[1].offset == 13
      && blocks[1].length == 12;
  all_ok = all_ok && split_ok;
  std::printf("%-8s split: %zu blocks, banks 0x%02x/0x%02x\n",
      split_ok ? "OK" : "FAIL", blocks.size(),
      blocks.empty() ? 0 : blocks[0].bank,
      blocks.size() < 2 ? 0 : blocks[1].bank);

  std::printf("\n--- clean image + kit parse ---\n");
  {
    // Two synthetic block frames; CleanBulkImage should strip the 14-byte
    // headers + trailing f7 and concatenate the data.
    auto make_block = [](uint8_t fill, size_t data_len) {
      Bytes blk = FromHex("f0 41 6c 02 00 00 00 00 10 00 00 04 00 00");
      blk.insert(blk.end(), data_len, fill);
      blk.push_back(0xf7);
      return blk;
    };
    Bytes raw = make_block(0xaa, 100);
    const Bytes b2 = make_block(0xbb, 50);
    raw.insert(raw.end(), b2.begin(), b2.end());
    const Bytes clean = spdsx::device::CleanBulkImage(raw);
    const bool clean_ok = clean.size() == 150 && clean[0] == 0xaa
        && clean[99] == 0xaa && clean[100] == 0xbb && clean[149] == 0xbb;
    all_ok = all_ok && clean_ok;
    std::printf("%-8s clean strips framing (%zu bytes)\n",
        clean_ok ? "OK" : "FAIL", clean.size());

    // A clean image with names planted at record 0 and record 128.
    Bytes img(spdsx::device::kKitArrayBase
        + 129 * spdsx::device::kKitRecordStride,
        0x00);
    auto put = [&](int kit, const char* name) {
      const size_t rec = spdsx::device::kKitArrayBase
          + static_cast<size_t>(kit) * spdsx::device::kKitRecordStride;
      const std::string padded = std::string(name)
          + std::string(spdsx::device::kKitNameLen - std::string(name).size(),
              ' ');
      for (size_t i = 0; i < spdsx::device::kKitNameLen; ++i) {
        img[rec + i] = static_cast<uint8_t>(padded[i]);
      }
    };
    put(0, "Dance");
    put(128, "ZZZZZZZZZZZZZZZZ");
    // Plant kit 129 pad 1 params (XFADE 100/120 dyn LOUD3 fv50 trig ON)
    // and pad 3 layer mode (SWITCH), at the mapped offsets.
    const size_t rec129 = spdsx::device::kKitArrayBase
        + 128 * spdsx::device::kKitRecordStride;
    const size_t p1 = rec129 + spdsx::device::kPadTableBase;
    img[p1 + spdsx::device::kPadLayerMode] = 3;    // XFADE
    img[p1 + spdsx::device::kPadFadePoint] = 100;
    img[p1 + spdsx::device::kPadFadeEnd] = 120;
    img[p1 + spdsx::device::kPadDynamics] = 1;
    img[p1 + spdsx::device::kPadDynCurve] = 3;      // LOUD3
    img[p1 + spdsx::device::kPadFixedVel] = 50;
    img[p1 + spdsx::device::kPadTrigReserve] = 1;
    const size_t p3 = rec129 + spdsx::device::kPadTableBase
        + 2 * spdsx::device::kPadBlockStride;
    img[p3 + spdsx::device::kPadLayerMode] = 4;     // SWITCH
    // Pad 1's waves in the layer table (u16 LE; bottom one block on).
    const size_t l1 = rec129 + spdsx::device::kLayerTableBase;
    img[l1] = 127;
    img[l1 + spdsx::device::kLayerBlockStride] = 203;
    const auto kits = spdsx::device::ParseKits(img);
    const bool parse_ok = kits.size() >= 129 && kits[0].name == "Dance"
        && kits[128].name == "ZZZZZZZZZZZZZZZZ";
    all_ok = all_ok && parse_ok;
    std::printf("%-8s parse: kit1=%s kit129=%s\n", parse_ok ? "OK" : "FAIL",
        kits.empty() ? "?" : kits[0].name.c_str(),
        kits.size() < 129 ? "?" : kits[128].name.c_str());
    if (kits.size() >= 129) {
      const auto& pp = kits[128].pads[0];
      const bool pad_ok = pp.layer_mode == 3 && pp.fade_point == 100
          && pp.fade_end == 120 && pp.dynamics == 1
          && pp.dynamics_curve == 3 && pp.fixed_velocity == 50
          && pp.trigger_reserve == 1 && kits[128].pads[2].layer_mode == 4
          && pp.wave_top == 127 && pp.wave_bottom == 203;
      all_ok = all_ok && pad_ok;
      std::printf("%-8s pad params: pad1 mode=%d fp=%d ... pad3 mode=%d\n",
          pad_ok ? "OK" : "FAIL", pp.layer_mode, pp.fade_point,
          kits[128].pads[2].layer_mode);
    }
  }

  std::printf("\n--- sample directory parse ---\n");
  {
    // A synthetic clean image: junk, then a directory whose record 1
    // anchors on "PRELOAD 00001". Records 2 and 4 named, 3 empty.
    const size_t stride = spdsx::device::kSampleRecordStride;
    Bytes img(64 + stride * 6, 0x20);
    const size_t base = 64;
    auto put = [&](int idx, const char* wavename, const char* filename,
                   uint32_t frames, uint32_t category) {
      const size_t rec = base + static_cast<size_t>(idx) * stride;
      std::memcpy(&img[rec], wavename, std::strlen(wavename));
      std::memcpy(&img[rec + 0x10], filename, std::strlen(filename));
      for (int b = 0; b < 4; ++b) {
        img[rec + 0x94 + static_cast<size_t>(b)] =
            static_cast<uint8_t>(frames >> (8 * b));
        img[rec + 0xa0 + static_cast<size_t>(b)] =
            static_cast<uint8_t>(category >> (8 * b));
      }
    };
    put(1, "Solid K", "PRELOAD 00001", 56773, 1);
    put(2, "Warm K", "PRELOAD 00002", 76712, 1);
    put(4, "Bongo_Hi_CR78", "Bongo_Hi_CR78.wav", 11111, 15);
    const auto dir = spdsx::device::ParseSampleDir(img);
    const bool dir_ok = dir.size() == 3 && dir[0].index == 1
        && dir[0].wavename == "Solid K" && dir[0].frames == 56773
        && dir[1].index == 2 && dir[2].index == 4
        && dir[2].filename == "Bongo_Hi_CR78.wav" && dir[2].category == 15
        && spdsx::device::SampleCategoryName(dir[2].category)
            == "Percussion";
    all_ok = all_ok && dir_ok;
    std::printf("%-8s sample dir: %zu records, [0]=%s [2]=%s\n",
        dir_ok ? "OK" : "FAIL", dir.size(),
        dir.empty() ? "?" : dir[0].wavename.c_str(),
        dir.size() < 3 ? "?" : dir[2].wavename.c_str());

    // Remote path derivation + RFWV header (wave-export protocol).
    const bool path_ok = spdsx::device::RemoteWavePath(1554)
        == "/SPDSXREMOTE//Roland/SPD-SXPRO/WAVE/DATA/D015/W01554.SMP";
    const Bytes rfwv = FromHex(
        "52 46 57 56 14 4d 00 00 80 bb 00 00 01 00 00 00 "
        "10 00 00 00 00 00 00 00");
    const auto hdr = spdsx::device::ParseRfwvHeader(rfwv);
    const bool rfwv_ok = hdr.valid && hdr.data_bytes == 0x4d14
        && hdr.sample_rate == 48000 && hdr.channels == 1
        && hdr.bits_per_sample == 16;
    all_ok = all_ok && path_ok && rfwv_ok;
    std::printf("%-8s remote path\n", path_ok ? "OK" : "FAIL");
    std::printf("%-8s RFWV header: %u Hz, %u ch, %u-bit, %u data bytes\n",
        rfwv_ok ? "OK" : "FAIL", hdr.sample_rate, hdr.channels,
        hdr.bits_per_sample, hdr.data_bytes);

    // RfwvToWav: 512-byte header + 8 PCM bytes -> a 52-byte WAV with a
    // RIFF/WAVE/fmt/data structure and the 8 PCM bytes preserved.
    Bytes smp = rfwv;
    smp.resize(spdsx::device::kRfwvHeaderSize, 0);
    const Bytes tail = {1, 0, 2, 0, 3, 0, 4, 0};
    smp.insert(smp.end(), tail.begin(), tail.end());
    const Bytes wav = spdsx::device::RfwvToWav(smp);
    const bool wav_ok = wav.size() == 44 + 8
        && std::equal(wav.begin(), wav.begin() + 4, "RIFF")
        && wav[8] == 'W' && wav[9] == 'A' && wav[22] == 1  // channels
        && std::equal(wav.end() - 8, wav.end(), tail.begin());
    all_ok = all_ok && wav_ok;
    std::printf("%-8s RfwvToWav: %zu-byte WAV\n", wav_ok ? "OK" : "FAIL",
        wav.size());
  }

  std::printf("\n%s\n", all_ok ? "ALL MATCH" : "SOME MISMATCH");
  return all_ok ? 0 : 1;
}

// Resolves an explicit --port, or scans /dev/cu.usbmodem* and pings each
// candidate until one answers. Throws if nothing responds.
std::string ResolvePort(const std::string& requested) {
  if (!requested.empty()) {
    return requested;
  }
  const std::vector<std::string> candidates =
      spdsx::device::ListUsbModemPorts();
  if (candidates.empty()) {
    throw std::runtime_error(
        "no /dev/cu.usbmodem* ports found (device plugged in?)");
  }
  for (const auto& path : candidates) {
    std::printf("trying %s... ", path.c_str());
    std::fflush(stdout);
    try {
      spdsx::device::SpdsxDevice dev(path);
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
  std::fprintf(stderr,
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
      "  readwave <N> read user wave N's .SMP off the device (--out FILE\n"
      "              to save the raw RFWV); preloads aren't exportable\n"
      "  padlink     put triggers/pads into a pad-link group:\n"
      "                --group N        link group (required)\n"
      "                --trigger N      link trigger N\n"
      "                --pad N          link pad N\n"
      "                --range A[-B]    kits to touch (repeatable;\n"
      "                                 default all, 1-200)\n"
      "                --dry-run        print, send nothing\n"
      "                --verbose        show device replies\n"
      "  selftest    offline byte-exact message checks\n"
      "\n"
      "with no --port, scans /dev/cu.usbmodem* and pings each node\n");
  return 2;
}

int RunPing(const std::string& port_arg) {
  const std::string port = ResolvePort(port_arg);
  spdsx::device::SpdsxDevice dev(port);
  std::printf("opened %s\n", port.c_str());
  const Bytes reply = dev.Ping();
  if (reply.empty()) {
    std::printf("ping: NO REPLY (device connected? official app closed?)\n");
    return 1;
  }
  std::printf("ping reply (%zu bytes): %s\n", reply.size(),
      ToHex(reply).c_str());
  return 0;
}

int RunInfo(const std::string& port_arg) {
  const std::string port = ResolvePort(port_arg);
  spdsx::device::SpdsxDevice dev(port);
  std::printf("port:     %s\n", port.c_str());
  const Bytes pong = dev.Ping();
  std::printf("status:   %s\n", pong.empty() ? "no reply" : "connected");
  if (pong.empty()) {
    return 1;
  }
  const std::string ver = dev.FirmwareField(0);   // "2.00"
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
    std::printf("  bank 0x%02x (%-13s): %3d block(s), %8zu bytes\n", bank,
        BankName(bank), stats.first, stats.second);
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

int RunDump(const std::string& port_arg, const std::vector<uint8_t>& banks,
    const std::string& out_path, const std::string& verify_path) {
  // Offline: just report an existing image's structure.
  if (!verify_path.empty()) {
    ReportBlocks(ReadFile(verify_path));
    return 0;
  }

  const std::string port = ResolvePort(port_arg);
  spdsx::device::SpdsxDevice dev(port);
  std::printf("opened %s\n", port.c_str());

  Bytes image;
  for (uint8_t bank : banks) {
    std::printf("streaming bank 0x%02x (%s)... ", bank, BankName(bank));
    std::fflush(stdout);
    int blocks = 0;
    size_t bytes = 0;
    const Bytes bank_image = dev.DumpBank(bank,
        [&](const Bytes& block)
        {
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
    spdsx::device::SpdsxDevice dev(port);
    std::printf("opened %s, streaming bank 0x10...\n", port.c_str());
    raw = dev.DumpBank(spdsx::device::kBankKits);
  }
  const Bytes clean = spdsx::device::CleanBulkImage(raw);
  const auto kits = spdsx::device::ParseKits(clean);
  std::printf("%zu raw bytes -> %zu clean bytes -> %zu kits\n", raw.size(),
      clean.size(), kits.size());
  for (size_t i = 0; i < kits.size(); ++i) {
    std::printf("  %3zu  %s\n", i + 1, kits[i].name.c_str());
  }
  return kits.empty() ? 1 : 0;
}

int RunReadWave(const std::string& port_arg, int index,
    const std::string& out_path) {
  const std::string port = ResolvePort(port_arg);
  spdsx::device::SpdsxDevice dev(port);
  std::printf("opened %s, reading wave %d (%s)...\n", port.c_str(), index,
      spdsx::device::RemoteWavePath(index).c_str());
  const Bytes smp = dev.ReadRemoteWave(index,
      [](size_t done, size_t total) {
        std::printf("\r  %zu / %zu bytes", done, total);
        std::fflush(stdout);
      });
  std::printf("\n%zu bytes read\n", smp.size());
  const auto hdr = spdsx::device::ParseRfwvHeader(smp);
  const size_t pcm = smp.size() > spdsx::device::kRfwvHeaderSize
      ? smp.size() - spdsx::device::kRfwvHeaderSize
      : 0;
  std::printf("RFWV: valid=%d  %u Hz  %u ch  %u-bit  %zu PCM bytes"
      "  (%.2f s)\n", hdr.valid, hdr.sample_rate, hdr.channels,
      hdr.bits_per_sample,
      pcm, hdr.channels ? pcm / 2.0 / hdr.channels / 48000.0 : 0.0);
  if (!out_path.empty()) {
    // A .wav path gets the converted WAV; anything else the raw .SMP.
    const bool as_wav = out_path.size() > 4
        && out_path.compare(out_path.size() - 4, 4, ".wav") == 0;
    const Bytes data = as_wav ? spdsx::device::RfwvToWav(smp) : smp;
    if (data.empty() || !WriteFile(out_path, data)) {
      std::fprintf(stderr, "couldn't write %s\n", out_path.c_str());
      return 1;
    }
    std::printf("wrote %s (%s)\n", out_path.c_str(),
        as_wav ? "wav" : "raw smp");
  }
  return smp.empty() ? 1 : 0;
}

int RunSamples(const std::string& port_arg, const std::string& from_path) {
  Bytes raw;
  if (!from_path.empty()) {
    raw = ReadFile(from_path);
  } else {
    const std::string port = ResolvePort(port_arg);
    spdsx::device::SpdsxDevice dev(port);
    std::printf("opened %s, streaming bank 0x20...\n", port.c_str());
    raw = dev.DumpBank(spdsx::device::kBankSamples);
  }
  const Bytes clean = spdsx::device::CleanBulkImage(raw);
  const auto samples = spdsx::device::ParseSampleDir(clean);
  std::printf("%zu raw bytes -> %zu clean bytes -> %zu samples\n",
      raw.size(), clean.size(), samples.size());
  std::printf("%6s  %-16s  %-16s  %8s  %s\n", "index", "wavename",
      "category", "seconds", "filename");
  for (const auto& s : samples) {
    const auto cat = spdsx::device::SampleCategoryName(s.category);
    std::printf("%6d  %-16s  %-16.*s  %8.2f  %s\n", s.index,
        s.wavename.c_str(), static_cast<int>(cat.size()), cat.data(),
        static_cast<double>(s.frames) / 48000.0, s.filename.c_str());
  }
  return samples.empty() ? 1 : 0;
}

const char* kModeNames[] = {"MIX", "FADE1", "FADE2", "XFADE", "SWITCH",
    "SW(MONO)", "ALTERNATE", "HI-HAT"};
const char* kCurveNames[] = {"LINEAR", "LOUD1", "LOUD2", "LOUD3"};

// Prints one kit's pads with the mapped params decoded.
int RunKit(const std::string& port_arg, const std::string& from_path,
    int kit) {
  if (kit < 1 || kit > spdsx::device::kBankKitCount) {
    std::fprintf(stderr, "kit must be 1-%d\n", spdsx::device::kBankKitCount);
    return 2;
  }
  Bytes raw;
  if (!from_path.empty()) {
    raw = ReadFile(from_path);
  } else {
    const std::string port = ResolvePort(port_arg);
    spdsx::device::SpdsxDevice dev(port);
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
  std::printf("  pad  mode      fadeP fadeE  dyn curve   fixVel trigRsv"
              "  top   bottom\n");
  for (int pad = 0; pad < spdsx::device::kPadsPerKit; ++pad) {
    const auto& p = k.pads[static_cast<size_t>(pad)];
    const char* mode = p.layer_mode < 8 ? kModeNames[p.layer_mode] : "?";
    const char* curve =
        p.dynamics_curve < 4 ? kCurveNames[p.dynamics_curve] : "?";
    std::printf("  %3d  %-9s %5d %5d  %-3s %-7s %5d  %-7s %5d %5d\n",
        pad + 1, mode, p.fade_point, p.fade_end, p.dynamics ? "ON" : "OFF",
        curve, p.fixed_velocity, p.trigger_reserve ? "ON" : "OFF",
        p.wave_top, p.wave_bottom);
  }
  return 0;
}

int RunPadLink(const std::string& port_arg, int group,
    const std::vector<std::pair<ObjectKind, int>>& objects,
    std::vector<KitRange> ranges, bool dry_run, bool verbose) {
  if (ranges.empty()) {
    ranges.push_back({1, 200});
  }

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
    owned = std::make_unique<spdsx::device::SpdsxDevice>(port);
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
        const Bytes foc = spdsx::device::Dt1(spdsx::device::kObjectSelectAddr,
            {spdsx::device::SelectValue(kind, index)});
        const Bytes wr = spdsx::device::Dt1(
            spdsx::device::PadLinkAddr(kind, index, kit),
            {static_cast<uint8_t>(group & 0x7F)});
        std::printf("        focus %s%d : %s\n", KindName(kind), index,
            ToHex(foc).c_str());
        std::printf("        write %s%d : %s\n", KindName(kind), index,
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
      dry_run ? "(dry run \xe2\x80\x94 nothing sent)" : "Done.", messages);
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
  int kit_arg = 0;
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
        objects.emplace_back(ObjectKind::kPad, std::atoi(next().c_str()));
      } else if (arg == "--range") {
        ranges.push_back(ParseRange(next()));
      } else if (arg == "--bank") {
        banks.push_back(
            static_cast<uint8_t>(std::strtol(next().c_str(), nullptr, 0)));
      } else if (arg == "--all") {
        banks = {spdsx::device::kBankKits, spdsx::device::kBankSamples,
            spdsx::device::kBankMeta, spdsx::device::kBankConfig};
      } else if (arg == "--out") {
        out_path = next();
      } else if (arg == "--verify") {
        verify_path = next();
      } else if (arg == "--from") {
        from_path = next();
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

    if (command == "selftest") {
      return RunSelfTest();
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
    if (command == "padlink") {
      if (group < 0) {
        std::fprintf(stderr, "padlink needs --group\n");
        return 2;
      }
      if (objects.empty()) {
        std::fprintf(stderr,
            "padlink needs at least one --trigger or --pad\n");
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
