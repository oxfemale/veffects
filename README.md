# veffects

**Turn an mp3 into real-time, math-only visuals.** `veffects` analyzes a track into a
compact `.veffects` "score" of audio features, then a cross-platform player renders
procedural scenes from it -- no textures, no assets, just code reacting to the music.
Every scene is a hot-loadable **plugin**, so the visual style is fully extensible.

![build](../../actions/workflows/ci.yml/badge.svg)

<p align="center">
  <img src="docs/screenshots/ghost_in_the_shell.png" width="32%" alt="Ghost in the Shell">
  <img src="docs/screenshots/blade_runner.png" width="32%" alt="Blade Runner">
  <img src="docs/screenshots/neuromancer.png" width="32%" alt="Neuromancer"><br>
  <img src="docs/screenshots/data_network.png" width="32%" alt="Data Network">
  <img src="docs/screenshots/dogs_by_the_river.png" width="32%" alt="Dogs by the River">
  <img src="docs/screenshots/image_world_3d.png" width="32%" alt="Image World 3D"><br>
  <img src="docs/screenshots/lego_city.png" width="32%" alt="Lego City">
  <img src="docs/screenshots/microbes.png" width="32%" alt="Microbes">
  <img src="docs/screenshots/nirvana.png" width="32%" alt="Nirvana">
</p>

## Features

- **mp3 -> visuals**, end to end: open a track in the GUI and it analyzes and plays.
- A tiny custom **`.veffects`** score format (~9.6 KB/s) -- see [docs/FORMAT.md](docs/FORMAT.md).
- **Plugin scenes** (`.dylib`/`.so`/`.dll`) discovered at runtime -- add your own by
  dropping a file in `plugins/`. See [docs/PLUGINS.md](docs/PLUGINS.md).
- **Feed an image** (jpg/png/bmp) to generate a world from it: a 2D scene that
  scatters the picture into music-reactive particles, and a 3D isometric voxel
  world (brightness -> height, color -> tile) with an orbiting camera, Sims-style.
- Cross-platform **Dear ImGui** control panel: Open, scene dropdown, a **Mute original
  audio** toggle (watch the visuals silently), a **seek slider**, transport, and
  drag-and-drop of an mp3.
- One-click **Export to mp4** from the panel (renders the selected scene with audio
  via ffmpeg), or headless `--export out.mp4`.
- Cinematic post-processing baked into the engine: bloom, chromatic aberration,
  beat-driven camera shake, film grain.
- Headless **offline render** mode that pipes raw frames to `ffmpeg` for an mp4.
- Builds on **macOS, Linux and Windows** via CMake; CI builds all three.

## Bundled scenes

**Themed worlds**

| Scene | Look |
|-------|------|
| **Ghost in the Shell** | Green katakana digital rain, a wireframe cyber-skull in a HUD reticle, scanlines, beat glitch. |
| **Tachikoma** | The cute blue AI spider-tank: multi-lens eye cluster, articulated legs, cyan HUD. |
| **Blade Runner** | Rain-soaked neon megacity: glowing billboards, sweeping searchlights, flying-car streaks. |
| **Neuromancer** | Gibson's cyberspace: an endless data-grid flythrough with glowing wireframe constructs. |
| **Nirvana** | Magenta glitch dreamscape: corrupted grid, datamosh tears, digital decay. |
| **Lego City** | Bright daytime brick city on a studded baseplate; primary-color towers and minifigs. |
| **Naruto** | A swirling Rasengan chakra orb, Uzumaki spiral, flying leaves and shuriken. |
| **Death Note** | Gothic notebook writing itself in red, Shinigami eyes, a falling apple, rain. |
| **Data Network** | A graph of routers and hosts with data packets streaming along the links. |
| **Dogs by the River** | A wholesome meadow: sun, clouds, a shimmering river and trotting dogs. |
| **Microbes** | A microscope view of translucent bacteria, flagella, dividing cells. |
| **Akira** | Neo-Tokyo psychic blast: crackling energy sphere, shockwave, Kaneda's light-trail bike. |
| **Tron** | The Grid: a neon perspective floor with cyan/orange light-cycle ribbon walls. |
| **Matrix** | The iconic dense green code rain with bright white leaders and parallax depth. |
| **Underwater** | A serene ocean: caustic light, fish schools, a jellyfish, swaying kelp, bubbles. |
| **Spaceport** | A lit space station over a planet, docking beacons and ships on engine trails. |
| **Cyberpunk 2077** | Night City: a dense neon skyline of holographic billboards, AV traffic, rain, holo-glitch. |
| **Dune** | Arrakis: a colossal sandworm breaching rolling dunes under twin moons, spice glitter. |
| **Volcano** | A nighttime eruption: lava fountain, glowing lava rivers, ash plume and embers. |
| **Winter Forest** | A snowy night: moon, shimmering aurora, snow-laden firs and falling snow. |
| **Jungle** | A lush rainforest: layered foliage, vines, god-rays, fireflies and tropical birds. |
| **Savanna** | An African sunset: acacia silhouettes, golden grass, giraffes and gazelles. |
| **Heaven** | A celestial paradise: luminous clouds, god-rays, rising light orbs and doves. |
| **Hell** | An infernal underworld: lava lakes, towering flames, embers and a horned demon. |
| **Black Hole** | A lensed starfield warping around an event horizon, photon ring and accretion disk. |
| **Galaxy** | A tilted spiral galaxy: glowing core, star-filled arms and nebula dust. |

**Image-driven** (load an image first -- Open image, drag a file in, or `--image`)

| Scene | Look |
|-------|------|
| **Photo Particles** | The image becomes a field of particles that scatter on beats and reform the picture (2D). |
| **Image World 3D** | The image becomes an isometric voxel world -- brightness is height, color is tint (3D, Sims-like). |

Scenes auto-crossfade over the track, or pick one from the dropdown (keys `1`-`9`,
`0` = auto). Auto rotation has two modes: **Timed** (fixed interval) and
**Reactive**, which changes scenes at the track's energy shifts (build-ups and
drops) derived from the score.

## Build

Requires a C++17 compiler, CMake >= 3.16, and SDL2. Dear ImGui, tinyfiledialogs and
minimp3 are vendored in `third_party/`.

### macOS

```bash
brew install cmake sdl2
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

### Linux

```bash
sudo apt-get install -y cmake libsdl2-dev   # Debian/Ubuntu
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

### Windows

No system SDL2 needed -- fetch and build it as part of the configure step:

```bat
cmake -S . -B build -DVEFFECTS_FETCH_SDL=ON
cmake --build build --config Release --parallel
```

Outputs land in `build/bin/` (`veffects_gen`, `veffects_play`) and
`build/bin/plugins/` (the scene libraries).

## Usage

### GUI

```bash
./build/bin/veffects_play
```

Then **Open mp3...** (or drag an mp3 onto the window). It analyzes the track and
starts playing. Use the **Scene** dropdown to switch scenes and the **Mute original
audio** checkbox to run the visuals without sound. For the image-driven scenes,
click **Open image...** (or drop a jpg/png/bmp on the window), then pick *Photo
Particles* or *Image World 3D*. Drag the **seek slider** to scrub, and **Export
mp4...** renders the current scene (with audio) to a file via ffmpeg. `Space`
pauses, `M` mutes, `Esc` quits.

### Command line

```bash
# analyze a track to a .veffects score
./build/bin/veffects_gen track.mp3 track.veffects

# play a score with synced audio, forcing a scene
./build/bin/veffects_play track.veffects --audio track.mp3 --scene-name "Blade Runner"

# or just hand the player the mp3 and let it analyze in-process
./build/bin/veffects_play track.mp3

# build a world from an image
./build/bin/veffects_play track.mp3 --image photo.jpg --scene-name "Image World 3D"
```

### Offline render to mp4

```bash
./build/bin/veffects_play track.veffects --render --fps 30 --start 0 --end 60 \
    --scene-name "Ghost in the Shell" \
  | ffmpeg -f rawvideo -pix_fmt rgb24 -s 640x480 -r 30 -i - \
           -ss 0 -t 60 -i track.mp3 \
           -c:v libx264 -pix_fmt yuv420p -c:a aac -shortest out.mp4
```

`veffects_play --list-scenes` prints the scenes it can find.

## Repository layout

```
include/    veffects_format.h   .veffects structs + load/save
            veffects_plugin.h   plugin ABI (the scene contract)
            veffects_analyze.h  in-process mp3 -> features
src/        veffects_gen.cpp    CLI analyzer
            veffects_play.cpp   player / plugin host + ImGui GUI
plugins/    one .cpp per scene  (built into build/bin/plugins/)
third_party/ imgui, tinyfiledialogs, minimp3 (vendored)
docs/       FORMAT.md, PLUGINS.md, screenshots
.github/    CI workflow (macOS + Linux + Windows)
```

## Writing your own scene

See [docs/PLUGINS.md](docs/PLUGINS.md). In short: implement four C functions, draw
into an additive HDR buffer with the provided primitives, multiply by `p->alpha`,
and derive motion from `p->time`. Drop the `.cpp` in `plugins/`, rebuild, and it
appears in the dropdown.

## License

MIT -- see [LICENSE](LICENSE). Bundled third-party code keeps its own licenses
(Dear ImGui: MIT, minimp3: CC0, tinyfiledialogs: zlib).
