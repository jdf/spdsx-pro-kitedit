#include "spdutil_args.h"

#include <algorithm>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace spdsx::spdutil {
namespace {

// command -> the flags it reads. --port and --version are universal and
// deliberately absent. A command with no flags of its own maps to {}.
const std::map<std::string, std::vector<std::string>, std::less<>>& Table() {
  static const std::map<std::string, std::vector<std::string>, std::less<>>
      kTable = {
          {"ping", {}},
          {"info", {}},
          {"currentkit", {}},
          {"selectkit", {}},
          {"dump", {"bank", "all", "out", "verify"}},
          {"kits", {"from"}},
          {"samples", {"from"}},
          {"kit", {"from"}},
          {"readwave", {"out"}},
          // Both always commit, so --commit is honest here even though it
          // changes nothing.
          {"sendwave", {"from", "name", "commit"}},
          {"deletewave", {"commit"}},
          {"assign", {"sample", "pad", "commit", "dry-run", "kits"}},
          {"setname", {"name", "commit", "dry-run", "kits"}},
          {"setparams", {"pad", "params", "commit", "dry-run", "kits"}},
          {"setlayer",
           {"pad", "volume", "fadein", "decay", "commit", "dry-run", "kits"}},
          {"setmode", {"mode", "if-mode", "pad", "kits", "dry-run", "commit"}},
          {"padlink",
           {"group", "trigger", "pad", "kits", "dry-run", "verbose"}},
      };
  return kTable;
}

// Commands that read a bare number after the command word: a kit to
// show or select, or a sample index. Everything that writes kits takes
// --kits instead, so there is one spelling for "which kits".
const std::vector<std::string>& PositionalNumberCommands() {
  static const std::vector<std::string> kCommands = {
      "kit", "readwave", "sendwave", "deletewave", "selectkit"};
  return kCommands;
}

// Commands that only read the device, for explaining a rejected flag.
bool IsReadOnly(std::string_view command) {
  static const std::vector<std::string> kReadOnly = {"ping",
                                                     "info",
                                                     "currentkit",
                                                     "dump",
                                                     "kits",
                                                     "kit",
                                                     "samples",
                                                     "readwave"};
  return std::find(kReadOnly.begin(), kReadOnly.end(), command)
      != kReadOnly.end();
}

size_t EditDistance(std::string_view a, std::string_view b) {
  std::vector<size_t> row(b.size() + 1);
  for (size_t j = 0; j <= b.size(); ++j) {
    row[j] = j;
  }
  for (size_t i = 1; i <= a.size(); ++i) {
    size_t diag = row[0];
    row[0] = i;
    for (size_t j = 1; j <= b.size(); ++j) {
      const size_t next = std::min(
          {row[j] + 1, row[j - 1] + 1, diag + (a[i - 1] == b[j - 1] ? 0 : 1)});
      diag = row[j];
      row[j] = next;
    }
  }
  return row[b.size()];
}

}  // namespace

bool ParseKitSpec(std::string_view spec,
                  std::vector<KitRange>* out,
                  std::string* error) {
  out->clear();
  const auto fail = [&](const std::string& message) {
    *error = message;
    out->clear();
    return false;
  };
  if (spec.empty()) {
    return fail("empty kit spec; want e.g. 108, 1-20, or 1,5,10-20");
  }
  // A whole number, or nothing. std::stoi would accept "12abc".
  const auto number = [](std::string_view text, int* value) {
    if (text.empty() || text.size() > 3) {
      return false;
    }
    int n = 0;
    for (const char c : text) {
      if (c < '0' || c > '9') {
        return false;
      }
      n = n * 10 + (c - '0');
    }
    *value = n;
    return true;
  };

  size_t start = 0;
  while (start <= spec.size()) {
    const size_t comma = spec.find(',', start);
    const std::string_view part =
        spec.substr(start,
                    comma == std::string_view::npos ? std::string_view::npos
                                                    : comma - start);
    if (part.empty()) {
      return fail("empty range in kit spec \"" + std::string(spec) + "\"");
    }
    KitRange range;
    const size_t dash = part.find('-');
    if (dash == std::string_view::npos) {
      if (!number(part, &range.first)) {
        return fail("\"" + std::string(part) + "\" is not a kit number");
      }
      range.last = range.first;
    } else {
      if (!number(part.substr(0, dash), &range.first)
          || !number(part.substr(dash + 1), &range.last)) {
        return fail("\"" + std::string(part) + "\" is not a kit range");
      }
    }
    if (range.first < kFirstKit || range.last > kLastKit) {
      return fail("kit range \"" + std::string(part) + "\" is outside 1-200");
    }
    if (range.first > range.last) {
      return fail("kit range \"" + std::string(part) + "\" runs backwards");
    }
    out->push_back(range);
    if (comma == std::string_view::npos) {
      break;
    }
    start = comma + 1;
  }
  return true;
}

int KitCount(const std::vector<KitRange>& ranges) {
  std::vector<bool> seen(kLastKit + 1, false);
  int count = 0;
  for (const KitRange& range : ranges) {
    for (int kit = std::max(range.first, kFirstKit);
         kit <= std::min(range.last, kLastKit);
         ++kit) {
      if (!seen[static_cast<size_t>(kit)]) {
        seen[static_cast<size_t>(kit)] = true;
        ++count;
      }
    }
  }
  return count;
}

const std::vector<std::string>& Commands() {
  static const std::vector<std::string> kNames = [] {
    std::vector<std::string> names;
    names.reserve(Table().size());
    for (const auto& [name, flags] : Table()) {
      names.push_back(name);
    }
    return names;
  }();
  return kNames;
}

bool IsCommand(std::string_view name) {
  return Table().find(name) != Table().end();
}

std::string NearestCommand(std::string_view typo) {
  std::string best;
  size_t best_distance = 3;  // suggest a plausible slip, not a stretch
  for (const std::string& name : Commands()) {
    const size_t d = EditDistance(typo, name);
    if (d < best_distance) {
      best_distance = d;
      best = name;
    }
  }
  return best;
}

std::vector<std::string> AllowedFlags(std::string_view command) {
  const auto it = Table().find(command);
  return it == Table().end() ? std::vector<std::string>() : it->second;
}

bool TakesPositionalNumber(std::string_view command) {
  const auto& commands = PositionalNumberCommands();
  return std::find(commands.begin(), commands.end(), command) != commands.end();
}

bool RequiresKitSpec(std::string_view command) {
  static const std::vector<std::string> kNeedsKit = {
      "assign", "setname", "setparams", "setlayer", "setmode", "padlink"};
  return std::find(kNeedsKit.begin(), kNeedsKit.end(), command)
      != kNeedsKit.end();
}

bool TakesSingleKit(std::string_view command) {
  static const std::vector<std::string> kSingle = {
      "assign", "setname", "setparams", "setlayer"};
  return std::find(kSingle.begin(), kSingle.end(), command) != kSingle.end();
}

std::string UnacceptedFlag(std::string_view command,
                           const std::vector<std::string>& seen) {
  const auto allowed = AllowedFlags(command);
  for (const std::string& flag : seen) {
    if (flag == "port" || flag == "version") {
      continue;
    }
    if (std::find(allowed.begin(), allowed.end(), flag) == allowed.end()) {
      return flag;
    }
  }
  return "";
}

std::string FlagRejectionHint(std::string_view command, std::string_view flag) {
  if (flag == "dry-run") {
    return IsReadOnly(command)
        ? std::string(command) + " only reads, so there is nothing to rehearse"
        : std::string(command) + " has no dry run; it would go through";
  }
  if (flag == "commit") {
    if (command == "selectkit") {
      return "selectkit changes the playback kit only, so there is nothing "
             "to commit";
    }
    return std::string(command) + " does not write anything";
  }
  if (flag == "range" || flag == "kits") {
    return std::string(command) + " does not write to kits";
  }
  if (flag == "pad") {
    return std::string(command) + " does not act on a pad";
  }
  if (flag == "out") {
    return "only dump and readwave write a file";
  }
  return std::string(command) + " does not use --" + std::string(flag);
}

}  // namespace spdsx::spdutil
