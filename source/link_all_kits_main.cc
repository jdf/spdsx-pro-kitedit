// link_all_kits — C++ port of the spdsx-py verification utility. Puts
// Trigger 7 and Pad 7 into a pad-link group for a range of kits on a Roland
// SPD-SX PRO over its USB serial port.
//
// Verifies the ported protocol:
//   --selftest  builds messages and checks them byte-for-byte against the
//               captures embedded in spdsx-py (no device needed)
//   --dry-run   prints every message it would send, opening nothing
//
// Notes for live runs: close the SPD-SX PRO App first (one program per port),
// and back the unit up before a full run. With no --port, the tool scans
// /dev/cu.usbmodem* and pings each node until the device answers (the node
// number changes every time the device is plugged in).

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "device/protocol.h"
#include "device/spdsx_device.h"

namespace {

using spdsx::device::Bytes;
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

  std::printf("\n%s\n", all_ok ? "ALL MATCH" : "SOME MISMATCH");
  return all_ok ? 0 : 1;
}

// Builds (and, when dev != nullptr, sends) the link messages for each kit,
// returning a log line per message — mirroring spdsx.py's bulk_link_all_kits
// so its output can be diffed against the Python's.
std::vector<std::string> BulkLinkAllKits(spdsx::device::SpdsxDevice* dev,
    int group, int first, int last, bool verbose, double pace_seconds = 0.02) {
  std::vector<std::string> lines;
  const std::pair<ObjectKind, int> pairs[] = {
      {ObjectKind::kTrig, 7}, {ObjectKind::kPad, 7}};
  char buf[64];

  if (dev != nullptr) {
    const Bytes r = dev->Ping();
    lines.push_back(r.empty() ? "ping: NO REPLY" : "ping ok: " + ToHex(r));
    if (r.empty()) {
      throw std::runtime_error("device did not respond to ping");
    }
  }

  for (int kit = first; kit <= last; ++kit) {
    const Bytes sel = spdsx::device::Dt1(spdsx::device::kKitSelectAddr,
        spdsx::device::EncodeKit(kit));
    std::snprintf(buf, sizeof(buf), "kit %3d select   : ", kit);
    lines.push_back(buf + ToHex(sel));
    if (dev != nullptr) {
      const Bytes rep = dev->Command(sel);
      if (verbose) {
        lines.push_back("    <- " + (rep.empty() ? "(no reply)" : ToHex(rep)));
      }
    }
    for (const auto& [kind, index] : pairs) {
      const Bytes foc = spdsx::device::Dt1(spdsx::device::kObjectSelectAddr,
          {spdsx::device::SelectValue(kind, index)});
      const Bytes wr = spdsx::device::Dt1(
          spdsx::device::PadLinkAddr(kind, index, kit),
          {static_cast<uint8_t>(group & 0x7F)});
      std::snprintf(buf, sizeof(buf), "        focus %s%d : ", KindName(kind),
          index);
      lines.push_back(buf + ToHex(foc));
      if (dev != nullptr) {
        const Bytes rep = dev->Command(foc);
        if (verbose) {
          lines.push_back(
              "    <- " + (rep.empty() ? "(no reply)" : ToHex(rep)));
        }
      }
      std::snprintf(buf, sizeof(buf), "        write %s%d : ", KindName(kind),
          index);
      lines.push_back(buf + ToHex(wr));
      if (dev != nullptr) {
        dev->Send(wr);
        std::this_thread::sleep_for(
            std::chrono::duration<double>(pace_seconds));
      }
    }
  }
  return lines;
}

int ArgInt(const char* v, int fallback) {
  if (v == nullptr) return fallback;
  return std::atoi(v);
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

}  // namespace

// Read-only live test: opens the port and pings. Exercises the whole C++
// transport (serial open + IOSSIOSPEED baud, frame wrap/unwrap, round-trip)
// without mutating the device.
int Probe(const std::string& port) {
  try {
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
  } catch (const std::exception& e) {
    std::fprintf(stderr, "error: %s\n", e.what());
    return 1;
  }
}

int main(int argc, char** argv) {
  std::string port;  // empty = auto-detect via ResolvePort
  int group = 11;
  int only = 0;
  int first = 1;
  int last = 200;
  bool dry_run = false;
  bool verbose = false;
  bool probe = false;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    auto next = [&](int fallback) {
      return (i + 1 < argc) ? ArgInt(argv[++i], fallback) : fallback;
    };
    if (arg == "--selftest") {
      return RunSelfTest();
    } else if (arg == "--probe") {
      probe = true;
    } else if (arg == "--dry-run") {
      dry_run = true;
    } else if (arg == "--verbose") {
      verbose = true;
    } else if (arg == "--port" && i + 1 < argc) {
      port = argv[++i];
    } else if (arg == "--group") {
      group = next(group);
    } else if (arg == "--only") {
      only = next(only);
    } else if (arg == "--first") {
      first = next(first);
    } else if (arg == "--last") {
      last = next(last);
    } else {
      std::fprintf(stderr,
          "usage: link_all_kits [--selftest] [--dry-run] [--verbose]\n"
          "                     [--port <dev>] [--group N] [--only K]\n"
          "                     [--first K] [--last K]\n"
          "with no --port, scans /dev/cu.usbmodem* and pings each node\n");
      return 2;
    }
  }

  if (probe || !dry_run) {
    try {
      port = ResolvePort(port);
    } catch (const std::exception& e) {
      std::fprintf(stderr, "error: %s\n", e.what());
      return 1;
    }
  }

  if (probe) {
    return Probe(port);
  }

  if (only != 0) {
    first = only;
    last = only;
  }

  spdsx::device::SpdsxDevice* dev = nullptr;
  std::unique_ptr<spdsx::device::SpdsxDevice> owned;
  if (!dry_run) {
    if (only != 0) {
      std::printf("About to WRITE to kit %d on %s (trig7 + pad7 -> group %d).\n",
          only, port.c_str(), group);
    } else {
      std::printf(
          "About to WRITE to kits %d-%d on %s (trig7 + pad7 -> group %d).\n",
          first, last, port.c_str(), group);
    }
    std::printf("Type 'yes' to proceed: ");
    std::fflush(stdout);
    std::string answer;
    std::getline(std::cin, answer);
    std::transform(answer.begin(), answer.end(), answer.begin(), ::tolower);
    // trim
    while (!answer.empty() && std::isspace((unsigned char)answer.back())) {
      answer.pop_back();
    }
    if (answer != "yes") {
      std::printf("Aborted.\n");
      return 1;
    }
    try {
      owned = std::make_unique<spdsx::device::SpdsxDevice>(port);
      dev = owned.get();
    } catch (const std::exception& e) {
      std::fprintf(stderr, "error opening device: %s\n", e.what());
      return 1;
    }
  }

  std::vector<std::string> lines;
  try {
    lines = BulkLinkAllKits(dev, group, first, last, verbose);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "error: %s\n", e.what());
    return 1;
  }
  for (const auto& ln : lines) {
    std::printf("%s\n", ln.c_str());
  }
  std::printf("\n%s  %zu messages.\n",
      dry_run ? "(dry run \xe2\x80\x94 nothing sent)" : "Done.", lines.size());
  return 0;
}
