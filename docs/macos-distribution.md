# Distributing the macOS app

`build_for_dist.py` builds a Release app, signs it with a Developer ID
Application certificate and the hardened runtime, creates a disk image,
submits that disk image to Apple's notarization service, staples the ticket,
and asks Gatekeeper to assess the finished artifact.

This is direct distribution outside the Mac App Store. It requires membership
in the Apple Developer Program.

## One-time setup

1. Install the current Xcode and select it:

   ```sh
   sudo xcode-select --switch /Applications/Xcode.app
   ```

2. In Xcode, open **Settings > Accounts**, add the Apple ID belonging to the
   developer team, select the team, open **Manage Certificates**, and create or
   import a **Developer ID Application** certificate.

   Confirm that the certificate and its private key are available:

   ```sh
   security find-identity -v -p codesigning
   ```

3. Create an app-specific password for the Apple ID, then store the notarization
   credentials in the login Keychain. The script never needs the password
   itself:

   ```sh
   xcrun notarytool store-credentials "spdsx-patchedit-notary" \
     --apple-id "you@example.com" \
     --team-id "YOUR_TEAM_ID" \
     --password "YOUR_APP_SPECIFIC_PASSWORD"
   ```

   `notarytool` validates the credentials before saving them. A Keychain
   profile backed by an App Store Connect API key also works.

4. If the Keychain contains more than one Developer ID Application identity,
   give the script the complete identity shown by `security`:

   ```sh
   export MACOS_SIGNING_IDENTITY='Developer ID Application: Your Name (TEAMID)'
   ```

   If a different Keychain profile name was used:

   ```sh
   export MACOS_NOTARY_PROFILE='your-profile-name'
   ```

## Make a release

Commit the release source, then run:

```sh
./build_for_dist.py
```

This bumps the patch component of `project(VERSION ...)` in `CMakeLists.txt`,
commits and pushes that bump (`jj push-main`, so the presubmit gates it),
packages, publishes a GitHub release tagged `v<version>` with the DMG
attached, and finishes by revealing the DMG in Finder. `--versioning minor`
or `--versioning major` bumps a different component; `--package-only` skips
the bump, commit, push, and GitHub release, and just packages the working
copy at its current version.

If packaging fails after the push, fix the cause and re-run with
`--package-only`, then publish the release by hand with `gh release create
v<version> dist/<the dmg> --target main`.

The finished file is written to `dist/`, with its version and CPU architecture
in the filename.

The release is Apple silicon only — there is no Intel build. The release
preset and its vcpkg triplet set the deployment target to macOS 26.0 for both
the app and every static dependency. This intentionally supports only macOS
releases from roughly the past year; revisit the floor when Apple ships the
next annual macOS release.

## Check the exact downloaded artifact

The best final test is on a Mac that has never run a development copy. After
downloading the DMG through the same website users will use:

```sh
spctl --assess --type open --context context:primary-signature \
  --verbose=2 ~/Downloads/spdsx-patchedit-*.dmg
```

Opening the DMG and double-clicking the app should show the normal first-launch
identified-developer confirmation, not an unidentified-developer or damaged-app
warning.

Browsers normally add the `com.apple.quarantine` extended attribute to
downloads. That is expected and should not be stripped: the Developer ID
signature and notarization ticket are what allow Gatekeeper to approve the
quarantined download.

## Common failures

- **No signing identity:** the certificate or its private key is missing from
  the login Keychain.
- **Invalid notarization:** inspect the submission listed by
  `xcrun notarytool history --keychain-profile spdsx-patchedit-notary`, then use
  `xcrun notarytool log SUBMISSION_ID --keychain-profile
  spdsx-patchedit-notary`.
- **Works locally but not on another Mac:** test the DMG after downloading it,
  verify its architecture with `lipo -archs`, and check the minimum OS version
  with `xcrun vtool -show-build` on the executable.
