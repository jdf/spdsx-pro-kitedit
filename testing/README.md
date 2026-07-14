# testing — unit-test infrastructure

Test-only code: fixtures, helpers, the aggregating runner, and the coverage
script. The tests themselves live next to the code they cover, in `source/`.

## Convention

For every `source/foo.h` (and `source/device/foo.h`) there is a
`source/foo_test.h` holding that header's `TEST()`s. They are `.h` files,
compiled in a single translation unit — [`all_tests.cc`](all_tests.cc) —
because gtest registers tests through static objects, so each suite must be
included exactly once. When you add a suite, add one `#include` line to
`all_tests.cc` (kept sorted).

A `foo_test.h` includes its header under test and any helpers here:

```cpp
#ifndef SPDSX_PATCHEDIT_SOURCE_FOO_TEST_H_
#define SPDSX_PATCHEDIT_SOURCE_FOO_TEST_H_
#include <gtest/gtest.h>
#include "foo.h"
TEST(Foo, DoesThing) { /* ... */ }
#endif  // SPDSX_PATCHEDIT_SOURCE_FOO_TEST_H_
```

## Helpers here

- [`juce_test_environment.h`](juce_test_environment.h) — a gtest global
  environment that brings up the JUCE message manager for the session, so
  JUCE-backed code under test works. Registered once in `all_tests.cc`.
- [`temp_dir.h`](temp_dir.h) — RAII temp directory for on-disk tests
  (`DeviceDb`, `DeviceDocument`).
- [`coverage.sh`](coverage.sh) — runs the suite under LLVM source-based
  coverage and prints a report scoped to `source/`.

## Building and running

Tests build with the normal `default` preset (they are `ON` by default):

```
cmake --preset default && cmake --build --preset default --target spdsx_tests
ctest --test-dir build            # or run the binary directly
```

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
