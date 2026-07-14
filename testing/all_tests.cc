// Aggregating test runner for spdsx-patchedit.
//
// Convention: for every source/foo.h there is a source/foo_test.h holding that
// header's TEST()s. Because gtest's TEST() macros register via static objects,
// each *_test.h must be compiled in exactly one translation unit -- this file
// is that unit. Add an #include below when a header gets its test suite (one
// per line, kept sorted). Test-only helpers and fixtures live in testing/.
//
// The binary links gtest_main, so it needs no main() of its own.

#include <gtest/gtest.h>

#include "juce_test_environment.h"

namespace {

// Stand up the JUCE message manager for the whole session (JUCE-backed code
// under test expects it). gtest owns and frees the environment.
const ::testing::Environment* const kJuceEnv =
    ::testing::AddGlobalTestEnvironment(new spdsx_testing::JuceTestEnvironment);

}  // namespace

// Smoke test: proves the harness compiles, links, and runs before any real
// coverage exists. Safe to delete once per-header suites are in place.
TEST(TestingInfra, LinksAndRuns) { SUCCEED(); }

// ---- per-header test suites (source/*_test.h), one #include each ----
// #include "device/protocol_test.h"
// #include "layers_test.h"
