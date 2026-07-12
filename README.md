# spdsx-patchedit

A desktop tool for interactively designing Roland SPD-SX PRO kits.
C++20, JUCE 8 for UI and audio, built with CMake and vcpkg.

The main window is a 3x3 grid mirroring the SPD-SX PRO's pad layout,
with a top and a bottom sample slot per pad — 18 slots in all. Drag
audio files (wav/aiff/flac/ogg/mp3) from Finder onto a slot to assign
them; each loaded sample shows as a spectrogram rendered by
[specgram](https://github.com/jdf/specgram).

## The device document

A document is a whole-device mirror: a `*.spdsx` folder (a macOS
package) holding all 200 kits, with a kit chooser in the header and
per-kit undo history. There is no Save command — every edit autosaves,
and the app reopens your last document on launch.

Each pad emulates the device's shared pad properties: the eight layer
modes (MIX, FADE1, FADE2, XFADE, SWITCH, SW(MONO), ALTERNATE, HI-HAT)
governing how the top and bottom layers split a hit, plus dynamics
on/off with the four dynamics curves (or a fixed strike level when
dynamics is off).

## Interaction

Focus follows the mouse: point at a slot and press **space** to trigger
its sample, or to stop it if it's already playing. The playing slot gets
a green border; a file dragged over a slot gets an amber one.

Whole-pad hits go through the pad's layer mode: click a pad (cursor
height sets velocity — bottom soft, top hard), press keys **1**–**9**
(velocity from the header VEL slider), or play MIDI notes 60–68 on
channel 10. Hold **H** for the hi-hat pedal (MIDI CC4 works too).

## Talking to the hardware

The SPD-SX PRO speaks Roland DT1 SysEx wrapped in a proprietary frame
over USB serial; the protocol is being reverse-engineered by sniffing
the official app (tooling and notes in [re/](re/README.md)). A JUCE-free
library (`source/device/`) implements what's mapped so far, and the
`spdutil` CLI exercises it against the hardware: ping the device, dump
its memory image, and list kits and pad parameters live. Two-way kit
sync is in progress.

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
commit of [jdf/specgram](https://github.com/jdf/specgram).

## History

The first iteration used Slint for the UI (with miniaudio for playback
and a Cocoa shim for file drops, since Slint can't yet receive OS-level
drags); it was ported to JUCE for first-class drag-and-drop and the
built-in audio engine. The Slint version lives in this repo's history
up to the "port the shell from Slint to JUCE" commit.

## License

The code here is MIT (see [LICENSE](LICENSE)); note that JUCE itself is
AGPLv3/commercial, which governs distribution of built binaries.
