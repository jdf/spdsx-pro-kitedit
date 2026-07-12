// spdutil — command-line utility for the Roland SPD-SX PRO over its USB
// serial port. Successor to link_all_kits (this does more than linking).
//
//   spdutil [--port <dev>] <command> [options]
//
//   ping        open the port and ping the device (read-only)
//   info        serial port, ping status, firmware version query
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
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
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
  std::printf("ping:     %s\n",
      pong.empty() ? "NO REPLY" : ToHex(pong).c_str());
  if (pong.empty()) {
    return 1;
  }
  // Category 0x17 returned the firmware version ("2.00" / "0094") in
  // captures, but the request layout hasn't been cracked yet (see
  // SpdsxDevice::StatusRequest); report whatever comes back.
  const Bytes version = dev.StatusRequest(0x17);
  if (version.empty()) {
    std::printf(
        "version:  unknown (request layout not yet captured; needs a\n"
        "          frida sniff of the official app's startup handshake)\n");
  } else {
    std::printf("version:  %s\n          \"%s\"\n", ToHex(version).c_str(),
        ToPrintable(version).c_str());
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
      } else if (arg == "--dry-run") {
        dry_run = true;
      } else if (arg == "--verbose") {
        verbose = true;
      } else if (!arg.empty() && arg[0] != '-' && command.empty()) {
        command = arg;
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
