#!/usr/bin/env bash
#
# Presubmit: build everything and run the unit tests. Exits nonzero on the
# first failure, so it can gate a push.
#
#   ./presubmit.sh
#
# `jj push-main` runs this automatically (see the alias in ~/.config/jj/
# config.toml) -- jj does not run git hooks, so the gate lives in the alias.
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

# Ninja re-runs CMake by itself when the build files are stale, so only a
# missing build tree needs an explicit configure.
if [ ! -d build ]; then
    echo "==> configure"
    cmake --preset default
fi

echo "==> build"
cmake --build --preset default

echo "==> test"
# getconf rather than `sysctl -n hw.ncpu`: same answer, without the BSD-ism.
ctest --test-dir build --output-on-failure \
    -j "$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"

echo
echo "presubmit OK"
