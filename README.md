# spdsx-patchedit

A desktop tool for interactively designing Roland SPD-SX kits. C++20,
Slint for the UI, built with CMake and vcpkg.

The main window is a 3x3 grid mirroring the SPD-SX's pad layout, with a
top and a bottom sample slot per pad — 18 slots in all. Drag `.wav`
files (anything libsndfile/miniaudio can decode, really) from Finder
onto a slot to assign them; each loaded sample shows as a spectrogram
rendered by [specgram](../specs).

## Interaction

Focus follows the mouse: point at a slot and press **space** to trigger
its sample, or to stop it if it's already playing. The playing slot gets
a green border.

For testing without the mouse, slots can be pre-filled from the command
line (slot indices are `(row * 3 + col) * 2`, `+1` for the bottom slot):

```
spdsx-patchedit --load 0 kick.wav --load 1 snare.wav
```

## Building

Requires CMake ≥ 3.25, Ninja, a [vcpkg](https://vcpkg.io) checkout
(set `VCPKG_ROOT` or keep it at `~/vcpkg`), and — because Slint ships no
prebuilt C++ binaries for macOS arm64 — a Rust toolchain on PATH to
build Slint from source:

```
brew install rust
cmake --preset default
cmake --build --preset default
./build/spdsx-patchedit
```

The first configure builds the vcpkg dependencies and the first build
compiles the Slint runtime with cargo; both take a while and are
incremental afterwards.

The spectrogram renderer is consumed as a vcpkg dependency through the
overlay port in [ports/specgram](ports/specgram), which builds a pinned
commit of the sibling `specs` checkout (expected at
`/Users/jdf/hax/specs`; adjust the `URL` and `REF` in the portfile to
taste).

## Platform notes

macOS-only for the moment: Slint 1.17 can't yet receive OS-level file
drops (its `DropArea` is app-internal), so Finder drag-and-drop is
implemented by an Objective-C++ shim over the native window in
[source/macdrop.mm](source/macdrop.mm). Everything else is portable;
when Slint learns external file drops the shim can be replaced with a
`DropArea` and the app should build anywhere Slint does.

## License

MIT — see [LICENSE](LICENSE).
