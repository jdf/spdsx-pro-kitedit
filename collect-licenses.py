#!/usr/bin/env python3
"""Gather every shipped dependency's license into one LICENSES.txt.

The binary statically links its dependencies, and the GPL/AGPL/LGPL/Apache
licenses among them require that their texts travel with it. vcpkg already
records one `copyright` file per package, so this concatenates ours plus
all of theirs into a single file for Contents/Resources.

  collect-licenses.py <vcpkg-installed-dir> <triplet> <version> <out-file>
"""

import pathlib
import sys

SOURCE_URL = "https://github.com/jdf/spdsx-pro-kitedit"

# Packages that do not contribute code to the shipping binary: the test
# framework and vcpkg's own build tooling. Everything else vcpkg installed
# is assumed linked in — over-inclusion costs a few paragraphs, omission
# is a license violation.
NOT_SHIPPED = {"gtest", "gperf"}
NOT_SHIPPED_PREFIXES = ("vcpkg-",)


def main() -> None:
    if len(sys.argv) != 5:
        sys.exit(__doc__)
    installed, triplet, version, out_path = sys.argv[1:]
    share = pathlib.Path(installed) / triplet / "share"
    root = pathlib.Path(__file__).resolve().parent

    chunks = [
        f"SPD-SX PROgram {version}",
        "",
        f"Source code: {SOURCE_URL}",
        "",
        "This application statically links the open-source components listed",
        "below. Each component's license follows. The application's own source",
        "is under the MIT license; because the combined binary includes",
        "AGPL- and GPL-licensed components (JUCE and KFR), distribution of the",
        "binary as a whole is governed by those terms, and the complete",
        "corresponding source is available at the URL above.",
        "",
    ]

    packages = []
    if share.is_dir():
        for pkg_dir in sorted(share.iterdir(), key=lambda p: p.name.lower()):
            name = pkg_dir.name
            if name in NOT_SHIPPED or name.startswith(NOT_SHIPPED_PREFIXES):
                continue
            if (pkg_dir / "copyright").is_file():
                packages.append(pkg_dir)
    if not packages:
        sys.exit(f"error: no copyright files under {share}")

    # JUCE vendors third-party code that compiles into the binary
    # (harfbuzz, sheenbidi, pnglib, zlib, oggvorbis...); its vcpkg
    # `copyright` covers only JUCE itself.
    vendored = {}  # label -> path
    for juce_root in sorted((pathlib.Path(installed) / triplet / "include")
                            .glob("JUCE-*/modules")):
        for pattern in ("**/LICENSE", "**/LICENSE.*", "**/COPYING"):
            for path in juce_root.glob(pattern):
                rel = path.parent.relative_to(juce_root)
                vendored[f"juce, vendored: {rel}"] = path

    own = root / "LICENSE"
    names = ["SPD-SX PROgram"] + [p.name for p in packages]
    chunks.append("Components: " + ", ".join(names))
    chunks.append("")

    def section(title: str, body: str) -> str:
        rule = "=" * 72
        return f"{rule}\n{title}\n{rule}\n\n{body.rstrip()}\n"

    if own.is_file():
        chunks.append(section("SPD-SX PROgram", own.read_text()))
    for pkg_dir in packages:
        text = (pkg_dir / "copyright").read_text(errors="replace")
        chunks.append(section(pkg_dir.name, text))

    # Full texts that the per-package files only reference by URL — JUCE's
    # copyright points at the AGPL rather than reproducing it.
    for extra in sorted((root / "licenses").glob("*.txt")):
        chunks.append(section(extra.stem, extra.read_text()))

    for label in sorted(vendored):
        chunks.append(
            section(label, vendored[label].read_text(errors="replace")))

    out = pathlib.Path(out_path)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text("\n".join(chunks))
    print(f"licenses: {len(packages) + 1} packages + {len(vendored)} vendored "
          f"-> {out}")


if __name__ == "__main__":
    main()
