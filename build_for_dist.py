#!/usr/bin/env python3
"""Cut a release: bump the version, commit, package, reveal the DMG.

Bumps project(VERSION ...) in CMakeLists.txt (--versioning major | minor |
patch, default patch), commits that change with jj, runs package-macos.sh,
and opens a Finder window showing the finished DMG.
"""

import argparse
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent
CMAKELISTS = ROOT / "CMakeLists.txt"
VERSION_RE = re.compile(r"^(\s*VERSION\s+)(\d+)\.(\d+)\.(\d+)\s*$", re.MULTILINE)


def BumpVersion(text: str, versioning: str) -> tuple[str, str]:
    """Returns (new CMakeLists text, new version string)."""
    matches = VERSION_RE.findall(text)
    if len(matches) != 1:
        sys.exit(
            f"error: expected exactly one 'VERSION x.y.z' line in "
            f"{CMAKELISTS}, found {len(matches)}"
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


def Run(*argv: str) -> None:
    print(f"+ {' '.join(argv)}")
    subprocess.run(argv, cwd=ROOT, check=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--versioning",
        choices=["major", "minor", "patch"],
        default="patch",
        help="which version component to bump (default: patch)",
    )
    args = parser.parse_args()

    text = CMAKELISTS.read_text()
    new_text, version = BumpVersion(text, args.versioning)
    CMAKELISTS.write_text(new_text)
    print(f"Version bumped to {version}.")

    Run("jj", "commit", "CMakeLists.txt", "-m", f"release: v{version}")
    Run("./package-macos.sh")

    dmgs = sorted((ROOT / "dist").glob(f"spdsx-patchedit-{version}-*.dmg"))
    if dmgs:
        Run("open", "-R", str(dmgs[0]))
    else:
        print("warning: no DMG matching this version found in dist/")
        Run("open", str(ROOT / "dist"))


if __name__ == "__main__":
    main()
