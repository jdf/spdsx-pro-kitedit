# spdsx-patchedit

A desktop tool for interactively designing Roland SPD-SX kits. C++20,
JUCE 8 for UI and audio, built with CMake and vcpkg.

The main window is a 3x3 grid mirroring the SPD-SX's pad layout, with a
top and a bottom sample slot per pad — 18 slots in all. Drag audio
files (wav/aiff/flac/ogg/mp3) from Finder onto a slot to assign them;
each loaded sample shows as a spectrogram rendered by
[specgram](../specs).

## Interaction

Focus follows the mouse: point at a slot and press **space** to trigger
its sample, or to stop it if it's already playing. The playing slot gets
a green border; a file dragged over a slot gets an amber one.

For testing without the mouse, slots can be pre-filled from the command
line (slot indices are `(row * 3 + col) * 2`, `+1` for the bottom slot):

```
spdsx-patchedit --load 0 kick.wav --load 1 snare.wav
```

## Building

Requires CMake ≥ 3.25, Ninja, and a [vcpkg](https://vcpkg.io) checkout
(set `VCPKG_ROOT` or keep it at `~/vcpkg`):

```
cmake --preset default
cmake --build --preset default
./build/spdsx-patchedit_artefacts/RelWithDebInfo/spdsx-patchedit.app/Contents/MacOS/spdsx-patchedit
```

The first configure builds the vcpkg dependencies (JUCE and specgram's
kfr/libsndfile/cairo), which takes a while; builds are incremental
afterwards.

The spectrogram renderer is consumed as a vcpkg dependency through the
overlay port in [ports/specgram](ports/specgram), which builds a pinned
commit of the sibling `specs` checkout (expected at
`/Users/jdf/hax/specs`; adjust the `URL` and `REF` in the portfile to
taste).

## History

The first iteration used Slint for the UI (with miniaudio for playback
and a Cocoa shim for file drops, since Slint can't yet receive OS-level
drags); it was ported to JUCE for first-class drag-and-drop and the
built-in audio engine. The Slint version lives in this repo's history
up to the "port the shell from Slint to JUCE" commit.

## License

The code here is MIT (see [LICENSE](LICENSE)); note that JUCE itself is
AGPLv3/commercial, which governs distribution of built binaries.
