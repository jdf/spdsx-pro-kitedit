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
ctest --test-dir build --output-on-failure -j "$(sysctl -n hw.ncpu)"

echo
echo "presubmit OK"
