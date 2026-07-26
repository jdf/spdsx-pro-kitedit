// Which flags each spdutil command accepts, and the commands themselves.
//
// spdutil parses every flag into one flat set of locals and each command
// reads the ones it cares about. That made a flag meaningless to the
// chosen command parse cleanly and vanish: "setmode --pad 9" swept all
// nine pads, and "--dry-run" on a write command wrote. The parser now
// records which flags it saw and rejects any the command does not use.
//
// JUCE-free and device-free so it can be tested without hardware.
#ifndef SPDSX_PATCHEDIT_SOURCE_SPDUTIL_ARGS_H_
#define SPDSX_PATCHEDIT_SOURCE_SPDUTIL_ARGS_H_

#include <string>
#include <string_view>
#include <vector>

namespace spdsx::spdutil {

// Every command the dispatcher understands.
const std::vector<std::string>& Commands();

// True if name is one of them.
bool IsCommand(std::string_view name);

// The closest command to a mistyped one, or "" if nothing is close
// enough to suggest.
std::string NearestCommand(std::string_view typo);

// The flags command accepts, without the leading dashes, excluding
// --port and --version which every command takes. Empty for an unknown
// command.
std::vector<std::string> AllowedFlags(std::string_view command);

// True if command accepts a positional kit/index number.
bool TakesPositionalNumber(std::string_view command);

// True if command writes to kits and must be told which ones. Leaving
// the kit out used to mean kit 1 — or, for the sweeps, every kit — so a
// forgotten argument wrote somewhere the user never named.
bool RequiresExplicitKit(std::string_view command);

// The first flag in `seen` (names without dashes) that `command` does
// not accept, or "" when they are all fine. Callers report this as an
// error rather than silently dropping the flag.
std::string UnacceptedFlag(std::string_view command,
                           const std::vector<std::string>& seen);

// A one-line explanation for rejecting flag on command — why it does
// nothing here, and what to reach for instead.
std::string FlagRejectionHint(std::string_view command, std::string_view flag);

}  // namespace spdsx::spdutil

#endif  // SPDSX_PATCHEDIT_SOURCE_SPDUTIL_ARGS_H_
