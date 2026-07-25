#!/bin/bash
# Build, Developer ID-sign, notarize, and staple a distributable macOS DMG.
set -euo pipefail

cd "$(dirname "$0")"

APP_NAME="spdsx-patchedit"
IDENTITY="${MACOS_SIGNING_IDENTITY:-Developer ID Application}"
NOTARY_PROFILE="${MACOS_NOTARY_PROFILE:-spdsx-patchedit-notary}"

# The release is Apple silicon only.
PRESET="release"
BUILD_DIR="build-release-arm64"
APP="$BUILD_DIR/spdsx-patchedit_artefacts/Release/${APP_NAME}.app"

die() {
    echo "error: $*" >&2
    exit 1
}

command -v cmake >/dev/null || die "cmake is required"
command -v hdiutil >/dev/null || die "hdiutil is required (run this on macOS)"
xcrun --find notarytool >/dev/null 2>&1 ||
    die "notarytool is unavailable; install and select a current Xcode"
xcrun --find stapler >/dev/null 2>&1 ||
    die "stapler is unavailable; install and select a current Xcode"

security find-identity -v -p codesigning |
    grep -Fq "\"${IDENTITY}" ||
    die "no '${IDENTITY}' certificate is available in the login Keychain; see docs/macos-distribution.md"

if ! xcrun notarytool history --keychain-profile "$NOTARY_PROFILE" \
    >/dev/null 2>&1; then
    die "notary profile '${NOTARY_PROFILE}' did not answer: it is missing or invalid, or Apple's notarization service is unreachable (this check needs network access); see docs/macos-distribution.md"
fi

echo "Building the arm64 Release app..."
cmake --preset "$PRESET"
cmake --build --preset "$PRESET" --target spdsx-patchedit
[[ -d "$APP" ]] || die "build did not produce $APP"
MINOS="$(xcrun vtool -show-build "$APP/Contents/MacOS/$APP_NAME" |
    awk '/minos/{print $2; exit}')"
echo "Built for minimum macOS ${MINOS:-unknown}."

echo "Signing with ${IDENTITY}..."
# This bundle currently contains one Mach-O executable and no embedded
# frameworks or helpers. Avoid --deep: it can conceal incorrectly signed
# nested code if the bundle gains any later.
codesign --force --sign "$IDENTITY" --options runtime --timestamp "$APP"
codesign --verify --deep --strict --verbose=2 "$APP"

VERSION="$(/usr/libexec/PlistBuddy -c \
    'Print :CFBundleShortVersionString' "$APP/Contents/Info.plist")"
ARCHS="$(lipo -archs "$APP/Contents/MacOS/$APP_NAME" | tr ' ' '-')"
mkdir -p dist
DMG="dist/${APP_NAME}-${VERSION}-macos-${ARCHS}.dmg"
[[ ! -e "$DMG" ]] ||
    die "$DMG already exists; move or remove it before making this release"

STAGING="$(mktemp -d "${TMPDIR:-/tmp}/${APP_NAME}.release.XXXXXX")"
cleanup() {
    rm -rf "$STAGING"
}
trap cleanup EXIT

ditto "$APP" "$STAGING/$APP_NAME.app"
ln -s /Applications "$STAGING/Applications"

echo "Creating and signing $DMG..."
hdiutil create -quiet -fs HFS+ \
    -volname "$APP_NAME $VERSION" \
    -srcfolder "$STAGING" -format UDZO "$DMG"
codesign --force --sign "$IDENTITY" --timestamp "$DMG"
codesign --verify --strict --verbose=2 "$DMG"

echo "Submitting to Apple's notarization service..."
# notarytool can exit 0 even when the verdict is Invalid, so check the
# reported status instead of trusting the exit code.
SUBMIT_LOG="$STAGING/notarytool-submit.log"
xcrun notarytool submit "$DMG" \
    --keychain-profile "$NOTARY_PROFILE" --wait | tee "$SUBMIT_LOG"
grep -Eq '^[[:space:]]*status: Accepted$' "$SUBMIT_LOG" || {
    SUBMISSION_ID="$(awk '/^[[:space:]]*id: /{print $2; exit}' "$SUBMIT_LOG")"
    die "notarization was not accepted; inspect it with: xcrun notarytool log ${SUBMISSION_ID:-SUBMISSION_ID} --keychain-profile $NOTARY_PROFILE"
}

echo "Stapling and validating the notarization ticket..."
xcrun stapler staple "$DMG"
xcrun stapler validate "$DMG"
spctl --assess --type open --context context:primary-signature \
    --verbose=2 "$DMG"

echo "ready to distribute: $DMG"
