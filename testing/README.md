# testing — unit-test infrastructure

Test-only code: fixtures, helpers, session setup, and the coverage script. The
tests themselves live next to the code they cover, in `source/`.

## Convention

For every `source/foo.h` (and `source/device/foo.h`) there is a
`source/foo_test.cc` holding that header's `TEST()`s. Each suite is its own
translation unit, listed in the `spdsx_tests` target in the top-level
`CMakeLists.txt` — add a line there when you add a suite.

A `foo_test.cc` includes its header under test first, then anything else it
needs; test-local helpers and fixtures go in an anonymous namespace:

```cpp
#include "foo.h"

#include <gtest/gtest.h>

namespace spdsx {
namespace {

TEST(Foo, DoesThing) { /* ... */ }

}  // namespace
}  // namespace spdsx
```

Test suite names are global to the binary, so keep them distinct across
suites (`TEST(Foo, ...)` for `foo.h`).

## Helpers here

- [`juce_test_environment.h`](juce_test_environment.h) — a gtest global
  environment that brings up the JUCE message manager for the session, so
  JUCE-backed code under test works. Registered by
  [`juce_test_environment.cc`](juce_test_environment.cc), so no suite has to
  do it.
- [`temp_dir.h`](temp_dir.h) — RAII temp directory for on-disk tests
  (`DeviceDb`, `DeviceDocument`).
- [`coverage.sh`](coverage.sh) — runs the suite under LLVM source-based
  coverage and prints a report scoped to `source/`.

## Running the tests

Tests build with the normal `default` preset (they are `ON` by default). Build
then run, either through ctest or the binary itself:

```
cmake --build --preset default --target spdsx_tests

ctest --test-dir build                      # all tests
ctest --test-dir build --output-on-failure  # show output of failures
ctest --test-dir build -R 'Layers\..*'      # by regex
ctest --test-dir build -j 8                 # in parallel
```

`gtest_discover_tests` registers each `TEST()` as its own ctest entry, so
`ctest -N` lists them individually and `-R` filters at test granularity.

The binary takes the usual gtest flags directly, which is what you want for
filtering, repeating, or debugging:

```
BIN=build/spdsx_tests_artefacts/RelWithDebInfo/spdsx_tests
$BIN                                  # all tests
$BIN --gtest_list_tests               # enumerate
$BIN --gtest_filter='Layers.*'        # filter
$BIN --gtest_filter='-Slow.*'         # exclude
$BIN --gtest_repeat=100 --gtest_shuffle
$BIN --gtest_break_on_failure         # trap into the debugger on failure
```

## In VS Code

The Testing panel lists every `TEST()` individually, with run/debug buttons in
the gutter of the `source/*_test.cc` that defines it — via the **TestMate C++**
extension (a workspace recommendation; configured in `.vscode/settings.json`,
debugging through CodeLLDB). Tasks: **run tests**, **build tests**, **coverage
report**, **presubmit**. `launch.json` has `spdsx_tests` and `spdsx_tests
(filtered)` (prompts for a gtest filter) for debugging the binary directly.

## Presubmit

[`../presubmit.sh`](../presubmit.sh) builds everything and runs the suite,
exiting nonzero on failure. `jj push-main` runs it automatically and aborts the
push if it fails — jj runs no git hooks, so the gate lives in the alias
(`~/.config/jj/config.toml`), which checks for an executable `presubmit.sh` at
the repo root. Because the alias is global and the script is tracked, a fresh
clone is gated with no setup. `SKIP_PRESUBMIT=1 jj push-main` bypasses it.

Note it tests the *working copy*, not the exact revision being pushed — with a
clean working copy (the normal case, pushing `@-`) those are the same thing.

## Coverage

Coverage needs an instrumented build (`-DSPDSX_COVERAGE=ON`), which the
`coverage` preset configures in a separate `build-coverage/` tree. The
`coverage` target runs the suite and emits the report:

```
cmake --preset coverage
cmake --build --preset coverage --target coverage
```

That prints per-file line/region/function coverage for `source/` and writes an
annotated HTML report to `build-coverage/coverage/html/index.html`. It uses
`xcrun llvm-profdata`/`llvm-cov` to match the Apple-clang toolchain.
