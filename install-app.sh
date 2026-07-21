#!/bin/bash
# Builds the app and installs it to /Applications as a normal
# double-clickable macOS app (RelWithDebInfo, same preset as everything
# else, so breakpoints still work against the installed copy's dSYM-free
# binary via the build tree).
set -euo pipefail
cd "$(dirname "$0")"

cmake --preset default >/dev/null
cmake --build --preset default --target spdsx-patchedit

APP="build/spdsx-patchedit_artefacts/RelWithDebInfo/spdsx-patchedit.app"
DEST="/Applications/spdsx-patchedit.app"
rm -rf "$DEST"
ditto "$APP" "$DEST"
# Nudge LaunchServices so the icon and the .spdsx association show up
# without a logout.
/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister \
    -f "$DEST" 2>/dev/null || true
echo "installed $DEST"
