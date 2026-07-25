#!/usr/bin/env python3
"""Cut a release: bump the version, commit, push, package, publish.

Bumps project(VERSION ...) in CMakeLists.txt (--versioning major | minor |
patch, default patch), commits that change with jj and pushes it (the
presubmit gates the push, as always), then builds a Release app, signs it
with a Developer ID Application certificate and the hardened runtime, wraps
it in a drag-to-Applications DMG, notarizes, staples, asks Gatekeeper's
opinion, publishes a GitHub release with the DMG attached, and opens a
Finder window showing the finished DMG.

--package-only skips the bump, commit, push, and GitHub release, and just
packages the working copy.
"""

import argparse
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

# Keep our progress prints in order with child-process output when piped.
sys.stdout.reconfigure(line_buffering=True)

ROOT = pathlib.Path(__file__).resolve().parent
CMAKELISTS = ROOT / "CMakeLists.txt"
VERSION_RE = re.compile(r"^(\s*VERSION\s+)(\d+)\.(\d+)\.(\d+)\s*$", re.MULTILINE)

APP_NAME = "spdsx-patchedit"
GITHUB_REPO = "jdf/spdsx-pro-kitedit"
IDENTITY = os.environ.get("MACOS_SIGNING_IDENTITY", "Developer ID Application")
NOTARY_PROFILE = os.environ.get("MACOS_NOTARY_PROFILE", "spdsx-patchedit-notary")

# The release is Apple silicon only.
PRESET = "release"
APP = ROOT / "build-release-arm64/spdsx-patchedit_artefacts/Release" / (
    APP_NAME + ".app"
)


def Die(message: str) -> None:
    sys.exit(f"error: {message}")


def Run(*argv: str) -> None:
    print(f"+ {' '.join(argv)}")
    subprocess.run(argv, cwd=ROOT, check=True)


def Output(*argv: str) -> str:
    return subprocess.run(
        argv, cwd=ROOT, check=True, capture_output=True, text=True
    ).stdout


def StreamedOutput(*argv: str) -> str:
    """Runs a command, echoing its output live AND returning it."""
    print(f"+ {' '.join(argv)}")
    proc = subprocess.Popen(
        argv,
        cwd=ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    chunks = []
    while chunk := proc.stdout.read(64):
        sys.stdout.write(chunk)
        sys.stdout.flush()
        chunks.append(chunk)
    if proc.wait() != 0:
        Die(f"{argv[0]} failed")
    return "".join(chunks)


def BumpVersion(text: str, versioning: str) -> tuple[str, str]:
    """Returns (new CMakeLists text, new version string)."""
    matches = VERSION_RE.findall(text)
    if len(matches) != 1:
        Die(
            f"expected exactly one 'VERSION x.y.z' line in {CMAKELISTS}, "
            f"found {len(matches)}"
        )
    prefix, major, minor, patch = matches[0]
    major, minor, patch = int(major), int(minor), int(patch)
    if versioning == "major":
        major, minor, patch = major + 1, 0, 0
    elif versioning == "minor":
        minor, patch = minor + 1, 0
    else:
        patch += 1
    version = f"{major}.{minor}.{patch}"
    return VERSION_RE.sub(f"{prefix}{version}", text), version


def CheckTooling(release: bool) -> None:
    if not shutil.which("cmake"):
        Die("cmake is required")
    if release and not shutil.which("gh"):
        Die("gh is required to publish the GitHub release")
    if not shutil.which("hdiutil"):
        Die("hdiutil is required (run this on macOS)")
    for tool in ("notarytool", "stapler"):
        if subprocess.run(
            ["xcrun", "--find", tool], capture_output=True
        ).returncode:
            Die(f"{tool} is unavailable; install and select a current Xcode")

    identities = Output("security", "find-identity", "-v", "-p", "codesigning")
    if f'"{IDENTITY}' not in identities:
        Die(
            f"no '{IDENTITY}' certificate is available in the login "
            "Keychain; see docs/macos-distribution.md"
        )

    if subprocess.run(
        ["xcrun", "notarytool", "history", "--keychain-profile", NOTARY_PROFILE],
        capture_output=True,
    ).returncode:
        Die(
            f"notary profile '{NOTARY_PROFILE}' did not answer: it is "
            "missing or invalid, or Apple's notarization service is "
            "unreachable (this check needs network access); see "
            "docs/macos-distribution.md"
        )


def BuildAndSignApp() -> None:
    print("Building the arm64 Release app...")
    Run("cmake", "--preset", PRESET)
    Run("cmake", "--build", "--preset", PRESET, "--target", APP_NAME)
    if not APP.is_dir():
        Die(f"build did not produce {APP}")
    binary = APP / "Contents/MacOS" / APP_NAME
    minos = re.search(
        r"minos\s+(\S+)", Output("xcrun", "vtool", "-show-build", str(binary))
    )
    print(f"Built for minimum macOS {minos.group(1) if minos else 'unknown'}.")

    print(f"Signing with {IDENTITY}...")
    # Sign inside-out (nested code first, then the bundle) rather than
    # --deep, which can conceal incorrectly signed nested code.
    Run(
        "codesign", "--force", "--sign", IDENTITY,
        "--options", "runtime", "--timestamp",
        str(APP / "Contents/Helpers/spdutil"),
    )
    Run(
        "codesign", "--force", "--sign", IDENTITY,
        "--options", "runtime", "--timestamp", str(APP),
    )
    Run("codesign", "--verify", "--deep", "--strict", "--verbose=2", str(APP))


def MakeDmg() -> pathlib.Path:
    version = Output(
        "/usr/libexec/PlistBuddy", "-c", "Print :CFBundleShortVersionString",
        str(APP / "Contents/Info.plist"),
    ).strip()
    archs = Output(
        "lipo", "-archs", str(APP / "Contents/MacOS" / APP_NAME)
    ).strip().replace(" ", "-")
    dmg = ROOT / "dist" / f"{APP_NAME}-{version}-macos-{archs}.dmg"
    dmg.parent.mkdir(exist_ok=True)
    if dmg.exists():
        Die(f"{dmg} already exists; move or remove it before making this release")

    with tempfile.TemporaryDirectory(prefix=f"{APP_NAME}.release.") as staging:
        Run("ditto", str(APP), f"{staging}/{APP_NAME}.app")
        os.symlink("/Applications", f"{staging}/Applications")
        print(f"Creating and signing {dmg}...")
        Run(
            "hdiutil", "create", "-quiet", "-fs", "HFS+",
            "-volname", f"{APP_NAME} {version}",
            "-srcfolder", staging, "-format", "UDZO", str(dmg),
        )
    Run("codesign", "--force", "--sign", IDENTITY, "--timestamp", str(dmg))
    Run("codesign", "--verify", "--strict", "--verbose=2", str(dmg))
    return dmg


def NotarizeAndStaple(dmg: pathlib.Path) -> None:
    print("Submitting to Apple's notarization service...")
    # notarytool can exit 0 even when the verdict is Invalid, so check the
    # reported status instead of trusting the exit code.
    submission = StreamedOutput(
        "xcrun", "notarytool", "submit", str(dmg),
        "--keychain-profile", NOTARY_PROFILE, "--wait",
    )
    if not re.search(r"^\s*status: Accepted$", submission, re.MULTILINE):
        match = re.search(r"^\s*id: (\S+)", submission, re.MULTILINE)
        submission_id = match.group(1) if match else "SUBMISSION_ID"
        Die(
            "notarization was not accepted; inspect it with: xcrun "
            f"notarytool log {submission_id} "
            f"--keychain-profile {NOTARY_PROFILE}"
        )

    print("Stapling and validating the notarization ticket...")
    Run("xcrun", "stapler", "staple", str(dmg))
    Run("xcrun", "stapler", "validate", str(dmg))
    Run(
        "spctl", "--assess", "--type", "open",
        "--context", "context:primary-signature", "--verbose=2", str(dmg),
    )


def PublishGithubRelease(dmg: pathlib.Path, version: str) -> None:
    print("Publishing the GitHub release...")
    Run(
        "gh", "release", "create", f"v{version}", str(dmg),
        "--repo", GITHUB_REPO, "--target", "main",
        "--title", f"{APP_NAME} {version}",
        "--generate-notes",
        "--notes",
        f"**Download** `{dmg.name}` below, open it, and drag the app to "
        "Applications.\n\nRequires an Apple silicon Mac running macOS 26 "
        "or later.",
    )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--versioning",
        choices=["major", "minor", "patch"],
        default="patch",
        help="which version component to bump (default: patch)",
    )
    parser.add_argument(
        "--package-only",
        action="store_true",
        help="skip the version bump and commit; just package the working copy",
    )
    args = parser.parse_args()

    CheckTooling(release=not args.package_only)

    if not args.package_only:
        text = CMAKELISTS.read_text()
        new_text, version = BumpVersion(text, args.versioning)
        CMAKELISTS.write_text(new_text)
        print(f"Version bumped to {version}.")
        Run("jj", "commit", "CMakeLists.txt", "-m", f"release: v{version}")
        # The GitHub release tags a commit, so the bump must be on the
        # remote before we can publish.
        Run("jj", "push-main")

    BuildAndSignApp()
    dmg = MakeDmg()
    NotarizeAndStaple(dmg)
    if not args.package_only:
        PublishGithubRelease(dmg, version)
    print(f"ready to distribute: {dmg}")
    Run("open", "-R", str(dmg))


if __name__ == "__main__":
    main()
