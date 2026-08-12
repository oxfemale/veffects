// veffects_plugin.h -- public ABI for visualization plugins.
//
// A plugin is a shared library (.dylib / .so / .dll) loaded by the player at
// runtime. Each plugin draws ONE scene algorithm into a shared HDR frame buffer;
// the player then applies its own post-processing (bloom, chromatic aberration,
// beat shake, film grain).
//
// A plugin MUST export (C linkage):
//   const VfxPluginInfo* vfx_plugin_info(void);
//   void* vfx_plugin_create(int width, int height);   // scene state, or NULL
//   void  vfx_plugin_destroy(void* state);
//   void  vfx_plugin_render(void* state, const VfxCanvas* canvas, const VfxParams* p);
//
// Draw either way (mix freely):
//   1) canvas primitives: canvas->add_glow(...), add_line(...), add_px(...);
//   2) directly into canvas->fb -- a linear additive width*height*3 (RGB) buffer,
//      row-major, 3 floats per pixel. Contributions are summed (HDR); the player
//      tone-maps. Always multiply your contribution by p->alpha (scene crossfade).

#pragma once
#include <cstdint>

#define VFX_PLUGIN_ABI 2

#ifdef __cplusplus
extern "C" {
#endif

// Per-frame parameters: audio + time. Energies are normalized to [0..1].
typedef struct VfxParams {
    double time;        // track time, seconds
    double dt;          // seconds since previous frame
    float  alpha;       // crossfade weight [0..1] -- multiply your output by it

    float  bass, mid, treble;   // low / mid / high band energy (smoothed)
    float  rms;                 // overall loudness (smoothed)
    float  loud;                // peak loudness (fast)
    float  centroid;            // spectral centroid ("brightness" of timbre)
    float  flux;                // spectral flux
    float  beat;                // beat strength (smoothed envelope)
    float  onset;               // instantaneous onset for this frame (peaks = beats)

    float  bpm;                 // tempo estimate (0 if unknown)
    float  duration;            // track length, seconds
    int    frameNo;             // render frame number (for pseudo-randomness)

    int          bandCount;     // number of spectrum bands
    const float* bands;         // bandCount smoothed band energies [0..1]

    int    width, height;       // frame dimensions
} VfxParams;

// Canvas: buffer + primitives. impl is engine-internal, do not touch.
typedef struct VfxCanvas {
    int    width, height;
    float* fb;                  // width*height*3 floats, additive HDR, RGB row-major
    void*  impl;                // engine-internal

    // Primitives (pixel coordinates; k = brightness/weight; r,g,b linear [0..~]).
    void (*add_px)  (const struct VfxCanvas*, int x, int y,
                     float r, float g, float b, float k);
    void (*add_glow)(const struct VfxCanvas*, float x, float y,
                     float r, float g, float b, float k, float rad);
    void (*add_line)(const struct VfxCanvas*, float x0, float y0, float x1, float y1,
                     float r, float g, float b, float k, float w);
    // Utility: HSV(h,s,v) -> linear RGB. h wraps.
    void (*hsv)(float h, float s, float v, float* r, float* g, float* b);
} VfxCanvas;

typedef struct VfxPluginInfo {
    int         abi;            // = VFX_PLUGIN_ABI
    const char* name;           // scene name for the list / GUI
    const char* author;
    const char* description;
} VfxPluginInfo;

// Export function signatures (for the player side).
typedef const VfxPluginInfo* (*vfx_info_fn)(void);
typedef void* (*vfx_create_fn)(int, int);
typedef void  (*vfx_destroy_fn)(void*);
typedef void  (*vfx_render_fn)(void*, const VfxCanvas*, const VfxParams*);

#ifdef __cplusplus
} // extern "C"
#endif

// Cross-platform export macro for plugin implementations.
#if defined(_WIN32)
  #ifdef __cplusplus
    #define VFX_EXPORT extern "C" __declspec(dllexport)
  #else
    #define VFX_EXPORT __declspec(dllexport)
  #endif
#else
  #ifdef __cplusplus
    #define VFX_EXPORT extern "C" __attribute__((visibility("default")))
  #else
    #define VFX_EXPORT __attribute__((visibility("default")))
  #endif
#endif
