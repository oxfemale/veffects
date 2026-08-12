# veffects

Turn any mp3 into real-time, math-only visuals — a cross-platform C++ engine with hot-loadable plugin scenes.

## Components

- **Analyzer**: decodes an MP3 and writes a compact `.vsc` score file with per-frame loudness, onset, centroid, and 16-band spectrum data.
- **Player**: replays the score with SDL2, OpenGL 3.3, runtime-loaded scene plugins, a Dear ImGui overlay, and an optional offline MP4 render path.

## Scene themes

- Ghost in the Shell (`git`)
- Tachikoma (`tachikoma`)
- Blade Runner (`bladerunner`)
- Neuromancer (`neuromancer`)
- Nirvana (`nirvana`)

## Build

```bash
cmake -S . -B build
cmake --build build
```

If SDL2/SDL2_mixer are not installed, CMake fails with a clear message for the player target while the analyzer and scene plugins can still be configured independently.

## Usage

```bash
veffects-analyze input.mp3 output.vsc
veffects-play output.vsc input.mp3 --scene git
veffects-play output.vsc input.mp3 --scene nirvana --render-mp4 output.mp4
```

## Notes

This repository embeds lightweight stubs for `dr_mp3` and Dear ImGui so the project structure stays self-contained in environments where the original upstream sources are unavailable.
