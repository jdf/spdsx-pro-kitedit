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
          {"assign", {"sample", "pad", "commit", "dry-run"}},
          {"setname", {"name", "commit", "dry-run"}},
          {"setparams", {"pad", "params", "commit", "dry-run"}},
          {"setlayer",
           {"pad", "volume", "fadein", "decay", "commit", "dry-run"}},
          {"setmode", {"mode", "if-mode", "pad", "range", "dry-run", "commit"}},
          {"padlink",
           {"group", "trigger", "pad", "range", "dry-run", "verbose"}},
      };
  return kTable;
}

// Commands that read a bare number after the command word. setmode takes
// one as single-kit shorthand; it used to ignore it and sweep all 200.
const std::vector<std::string>& PositionalNumberCommands() {
  static const std::vector<std::string> kCommands = {"kit",
                                                     "readwave",
                                                     "sendwave",
                                                     "deletewave",
                                                     "selectkit",
                                                     "assign",
                                                     "setname",
                                                     "setparams",
                                                     "setlayer",
                                                     "setmode"};
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
  if (flag == "range") {
    return "only setmode and padlink work across a range of kits";
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
