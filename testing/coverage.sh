#!/usr/bin/env bash
#
# Run the unit-test binary under LLVM source-based coverage and print a report
# scoped to our own source/ tree, plus an annotated HTML report.
#
# Normally invoked by the `coverage` CMake target, which sets the three
# SPDSX_* variables below. To run by hand after configuring a build with
# -DSPDSX_COVERAGE=ON (e.g. the `coverage` preset):
#
#   SPDSX_TESTS_BIN=build-coverage/spdsx_tests_artefacts/spdsx_tests \
#     bash testing/coverage.sh
#
# Any extra arguments are forwarded to the test binary (e.g. --gtest_filter=...).
set -euo pipefail

BIN="${SPDSX_TESTS_BIN:?set SPDSX_TESTS_BIN to the spdsx_tests binary}"
SRC="${SPDSX_SOURCE_DIR:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../source" && pwd)}"
COV="${SPDSX_COVERAGE_DIR:-coverage}"

mkdir -p "$COV"
rm -f "$COV"/*.profraw "$COV"/spdsx.profdata

# %p keeps forked/parallel runs from clobbering each other's raw profile.
LLVM_PROFILE_FILE="$COV/spdsx-%p.profraw" "$BIN" "$@"

xcrun llvm-profdata merge -sparse "$COV"/*.profraw -o "$COV/spdsx.profdata"

# Restrict to our production code: the trailing SRC path includes only that
# tree, and the ignore regex drops anything third-party or generated, plus the
# *_test.h suites themselves (their own coverage is not a useful signal).
IGNORE='(vcpkg_installed|/JUCE/|juce_|CMakeFiles|_deps/|/build|_test\.h)'

echo
echo "==== Coverage report (source/) ===="
xcrun llvm-cov report "$BIN" \
    -instr-profile="$COV/spdsx.profdata" \
    -ignore-filename-regex="$IGNORE" \
    "$SRC"

xcrun llvm-cov show "$BIN" \
    -instr-profile="$COV/spdsx.profdata" \
    -ignore-filename-regex="$IGNORE" \
    -format=html -output-dir="$COV/html" \
    "$SRC"

echo
echo "HTML report: $COV/html/index.html"
