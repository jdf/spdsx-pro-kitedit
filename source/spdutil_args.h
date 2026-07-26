// Which flags each spdutil command accepts, and the commands themselves.
//
// spdutil parses every flag into one flat set of locals and each command
// reads the ones it cares about, so nothing stops a flag meaningless to
// the chosen command from parsing cleanly and vanishing — "setmode --pad
// 9" would sweep all nine pads, "--dry-run" on a write would write. The
// parser records which flags it saw and rejects any the command does not
// use, which is what these tables are for.
//
// JUCE-free and device-free so it can be tested without hardware.
#ifndef SPDSX_PATCHEDIT_SOURCE_SPDUTIL_ARGS_H_
#define SPDSX_PATCHEDIT_SOURCE_SPDUTIL_ARGS_H_

#include <string>
#include <string_view>
#include <vector>

namespace spdsx::spdutil {

// A closed kit range: {126, 126} for "126", {129, 134} for "129-134".
struct KitRange {
  int first = 0;
  int last = 0;

  bool operator==(const KitRange&) const = default;
};

inline constexpr int kFirstKit = 1;
inline constexpr int kLastKit = 200;

// Parses a --kits spec: comma-separated ranges, each a single kit or
// FIRST-LAST inclusive ("108", "1,5,10-20", "108-200"). Returns false and
// fills *error with a one-line explanation on anything malformed or out
// of the 1-200 range. Ranges come back in the order written.
bool ParseKitSpec(std::string_view spec,
                  std::vector<KitRange>* out,
                  std::string* error);

// How many kits a parsed spec covers, counting overlaps once.
int KitCount(const std::vector<KitRange>& ranges);

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

// True if command writes to kits and so requires --kits: a write never
// guesses which kits it touches.
bool RequiresKitSpec(std::string_view command);

// True if command writes exactly one kit, so its --kits spec may name
// only one (setmode and padlink sweep, and accept any spec).
bool TakesSingleKit(std::string_view command);

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
