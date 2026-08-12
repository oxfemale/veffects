# Writing a scene plugin

A **scene** is a shared library (`.dylib` / `.so` / `.dll`) that draws one visual
algorithm. The player discovers every plugin in its `plugins/` folder at runtime,
lists them in the GUI, and hands each one a frame buffer plus the current audio
features. Add a new scene by dropping a compiled plugin into `plugins/` -- no need
to rebuild the player.

The full contract is `include/veffects_plugin.h`. A complete, well-commented
reference implementation is [`plugins/ghost_in_the_shell.cpp`](../plugins/ghost_in_the_shell.cpp).

## The ABI

A plugin must export four C functions:

```c
const VfxPluginInfo* vfx_plugin_info(void);
void* vfx_plugin_create(int width, int height);            // returns scene state (or NULL)
void  vfx_plugin_destroy(void* state);
void  vfx_plugin_render(void* state, const VfxCanvas* canvas, const VfxParams* p);
```

Use the `VFX_EXPORT` macro (handles visibility / `__declspec(dllexport)`):

```cpp
#include "veffects_plugin.h"

static VfxPluginInfo INFO = {
    VFX_PLUGIN_ABI, "My Scene", "you", "One-line description."
};
VFX_EXPORT const VfxPluginInfo* vfx_plugin_info(void) { return &INFO; }
VFX_EXPORT void* vfx_plugin_create(int W, int H) { /* ... */ return state; }
VFX_EXPORT void  vfx_plugin_destroy(void* s) { /* ... */ }
VFX_EXPORT void  vfx_plugin_render(void* s, const VfxCanvas* cv, const VfxParams* p) { /* draw */ }
```

## Drawing

`VfxCanvas` gives you a linear **additive HDR** buffer `cv->fb`
(`width*height*3` floats, row-major RGB) plus primitives:

```c
cv->add_px  (cv, x, y,        r,g,b, k);
cv->add_glow(cv, x, y,        r,g,b, k, radius);
cv->add_line(cv, x0,y0,x1,y1, r,g,b, k, width);
cv->hsv(h, s, v, &r,&g,&b);          // HSV -> linear RGB
```

Contributions are **summed**; the player tone-maps and adds bloom, chromatic
aberration, beat shake and film grain afterwards. You may also write into `cv->fb`
directly for full-frame effects (see the scanline/glitch code in the GitS scene).

## Two rules that matter

1. **Multiply every contribution by `p->alpha`.** The player crossfades between
   scenes; `alpha` is the current scene's weight `[0..1]`.
2. **Derive animation from `p->time`, not from accumulated per-frame state.** This
   keeps a scene correct under offline rendering and seeking. (If you keep state,
   make it a pure function of `p->time`, like the rain columns in the GitS scene.)

Only do destructive full-buffer operations (row shifts, multiplicative scanlines)
when `p->alpha > 0.98`, so a crossfade doesn't corrupt the other scene.

## Audio you can react to (`VfxParams`)

`time`, `dt`, `alpha`, `bass`, `mid`, `treble`, `rms`, `loud`, `centroid`, `flux`,
`beat` (smoothed), `onset` (instantaneous peak), `bpm`, `duration`, `frameNo`,
`bandCount` + `bands[]` (smoothed spectrum), `width`, `height`. All energies are
`[0..1]`.

### Optional input image

If the user loaded an image (Open image / drag a jpg-png-bmp / `--image`), these
are set (otherwise `image` is `NULL`):

`image` (8-bit pixels, row-major), `imageW`, `imageH`, `imageChannels`.

Sample it to build image-driven scenes -- see `plugins/photo_particles.cpp` (2D)
and `plugins/image_world_3d.cpp` (3D isometric heightmap) for worked examples.

## Building a plugin

With CMake, just drop the `.cpp` into `plugins/` and reconfigure -- every
`plugins/*.cpp` is built as a MODULE library into `build/bin/plugins/`.

By hand (macOS):

```bash
clang++ -O3 -std=c++17 -dynamiclib -fvisibility=hidden -Iinclude \
    -o build/bin/plugins/my_scene.dylib plugins/my_scene.cpp
```

Linux: replace `-dynamiclib` with `-shared` and the output extension with `.so`.
Windows (MSVC): build a DLL exporting the four functions.

## Testing quickly

Render a still frame straight to PNG (needs ffmpeg):

```bash
./build/bin/veffects_play track.veffects --render --fps 30 --start 30 --end 30.05 \
    --scene-name "My Scene" --plugins build/bin/plugins \
  | ffmpeg -f rawvideo -pix_fmt rgb24 -s 640x480 -r 30 -i - -frames:v 1 -y out.png
```

Or run the GUI and pick it from the **Scene** dropdown.
