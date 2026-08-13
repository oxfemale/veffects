// veffects_play -- cross-platform player / plugin host for .veffects visuals.
//
// The core is a pure host: it computes audio envelopes from the .veffects score,
// hands them to the selected scene plugin (which draws into a shared HDR buffer),
// then applies post-processing (bloom, chromatic aberration, beat shake, grain).
// Every scene is a plugin (.dylib/.so/.dll) discovered in a plugins/ folder.
//
// GUI: Dear ImGui over SDL2 -- Open (native file dialog), scene dropdown,
// a "Mute original audio" checkbox, and a transport bar. Opening an mp3 analyzes
// it in-process and plays. There is also a headless --render mode for offline mp4.

#define MINIMP3_IMPLEMENTATION
#include "veffects_analyze.h"     // pulls in minimp3 + veffects_format.h
#include "veffects_plugin.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"            // jpg/png/bmp loading for image-driven scenes

#if defined(_WIN32)
  #define VFX_POPEN  _popen
  #define VFX_PCLOSE _pclose
#else
  #define VFX_POPEN  popen
  #define VFX_PCLOSE pclose
#endif

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <vector>
#include <string>
#include <algorithm>
#include <thread>
#include <atomic>
#include <mutex>
#include <filesystem>
#include <set>
#include <fstream>

#ifdef USE_SDL
  #include <SDL.h>
  #include "imgui.h"
  #include "imgui_impl_sdl2.h"
  #include "imgui_impl_sdlrenderer2.h"
  #include "tinyfiledialogs.h"
#endif

// ---- cross-platform dynamic library loading ----
#if defined(_WIN32)
  #define NOMINMAX             // keep windows.h from clobbering std::min / std::max
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
  static void* dynOpen(const char* p) { return (void*)LoadLibraryA(p); }
  static void* dynSym(void* h, const char* n) { return (void*)GetProcAddress((HMODULE)h, n); }
  static void  dynClose(void* h) { if (h) FreeLibrary((HMODULE)h); }
  static const char* PLUGIN_EXT = ".dll";
#else
  #include <dlfcn.h>
  static void* dynOpen(const char* p) { return dlopen(p, RTLD_NOW | RTLD_LOCAL); }
  static void* dynSym(void* h, const char* n) { return dlsym(h, n); }
  static void  dynClose(void* h) { if (h) dlclose(h); }
  #if defined(__APPLE__)
    static const char* PLUGIN_EXT = ".dylib";
  #else
    static const char* PLUGIN_EXT = ".so";
  #endif
#endif

// ---- executable directory (for locating the plugins/ folder) ----
#if defined(_WIN32)
  static std::string exeDir() {
      char buf[4096]; DWORD n = GetModuleFileNameA(NULL, buf, sizeof(buf));
      std::string p(buf, n); auto s = p.find_last_of("\\/");
      return s == std::string::npos ? "." : p.substr(0, s);
  }
#elif defined(__APPLE__)
  #include <mach-o/dyld.h>
  static std::string exeDir() {
      char buf[4096]; uint32_t sz = sizeof(buf);
      if (_NSGetExecutablePath(buf, &sz) != 0) return ".";
      std::string p(buf); auto s = p.find_last_of('/');
      return s == std::string::npos ? "." : p.substr(0, s);
  }
#else
  #include <unistd.h>
  static std::string exeDir() {
      char buf[4096]; ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
      if (n <= 0) return ".";
      buf[n] = 0; std::string p(buf); auto s = p.find_last_of('/');
      return s == std::string::npos ? "." : p.substr(0, s);
  }
#endif

static const int W = 640, H = 480;
static const float PI2 = 6.28318530718f;

// ------------------------------------------------------------------ helpers
static inline float clampf(float x, float a, float b) { return x < a ? a : (x > b ? b : x); }
static inline float lerpf(float a, float b, float t)  { return a + (b - a) * t; }
static inline float fractf(float x) { return x - floorf(x); }
static inline float smoothstepf(float e0, float e1, float x) {
    float t = clampf((x - e0) / (e1 - e0), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}
struct RGB { float r, g, b; };
static RGB hsvf(float h, float s, float v) {
    h = h - floorf(h);
    float i = floorf(h * 6.f), f = h * 6.f - i;
    float p = v * (1 - s), q = v * (1 - f * s), t = v * (1 - (1 - f) * s);
    switch ((int)i % 6) {
        case 0: return {v, t, p}; case 1: return {q, v, p}; case 2: return {p, v, t};
        case 3: return {p, q, v}; case 4: return {t, p, v}; default: return {v, p, q};
    }
}
static inline float hash21(int x, int y) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return ((h ^ (h >> 16)) & 0xffffff) / 16777215.f;
}
static inline uint32_t hashu32(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x;
}
template <class F>
static void parallelRows(F&& f) {
    int nt = (int)std::thread::hardware_concurrency();
    nt = std::max(1, std::min(nt, 16));
    std::atomic<int> row{0};
    std::vector<std::thread> th;
    for (int i = 0; i < nt; i++)
        th.emplace_back([&]{ int y; while ((y = row.fetch_add(1)) < H) f(y); });
    for (auto& t : th) t.join();
}

// ---- image loading (jpg/png/bmp -> forced RGB) ----
static bool loadImageFile(const std::string& path, std::vector<unsigned char>& out, int& w, int& h) {
    int ch = 0;
    unsigned char* px = stbi_load(path.c_str(), &w, &h, &ch, 3);   // force 3 channels
    if (!px) return false;
    out.assign(px, px + (size_t)w * h * 3);
    stbi_image_free(px);
    return true;
}
static bool isImagePath(const std::string& p) {
    std::string e = std::filesystem::path(p).extension().string();
    for (auto& c : e) c = (char)tolower((unsigned char)c);
    return e == ".png" || e == ".jpg" || e == ".jpeg" || e == ".bmp";
}

// ---- plugin descriptor + canvas trampolines ----
static void tramp_add_px  (const VfxCanvas*, int, int, float, float, float, float);
static void tramp_add_glow(const VfxCanvas*, float, float, float, float, float, float, float);
static void tramp_add_line(const VfxCanvas*, float, float, float, float, float, float, float, float, float);
static void tramp_hsv     (float, float, float, float*, float*, float*);

struct PluginHandle {
    void*                dl      = nullptr;
    const VfxPluginInfo* info    = nullptr;
    vfx_create_fn        create  = nullptr;
    vfx_destroy_fn       destroy = nullptr;
    vfx_render_fn        render  = nullptr;
    void*                state   = nullptr;
    std::string          name;
};

// ------------------------------------------------------------------ host
struct Renderer {
    VfxData data;                          // current score (moved in on load)
    std::vector<float> fb, resolve, bloomA, bloomB;
    static const int BW = W / 4, BH = H / 4;

    int   forceScene = -1;                 // -1 = auto-rotate
    int   autoMode = 0;                    // 0 = timed, 1 = reactive (follow music), 2 = shuffle
    bool  favoritesOnly = false;           // auto-cycle only starred scenes
    std::set<std::string> favSet;          // favorite scene names
    std::vector<float> cuts;               // reactive scene-change times (from the score)
    float envBass = 0, envRms = 0, envBeat = 0, envCen = 0.3f, envMid = 0, envTre = 0;
    std::vector<float> bandEnv;
    int frameNo = 0;
    VfxCanvas canvas{};
    std::vector<PluginHandle> plugins;

    // optional input image (RGB) for image-driven scenes
    std::vector<unsigned char> imgData;
    int imgW = 0, imgH = 0, imgCh = 0;
    void setImage(std::vector<unsigned char>&& d, int w, int h, int ch) {
        imgData = std::move(d); imgW = w; imgH = h; imgCh = ch;
    }
    const unsigned char* imgPtr() const { return imgData.empty() ? nullptr : imgData.data(); }

    Renderer() : fb((size_t)W*H*3), resolve((size_t)W*H*3),
        bloomA((size_t)BW*BH*3), bloomB((size_t)BW*BH*3) {
        canvas.width = W; canvas.height = H; canvas.fb = fb.data(); canvas.impl = this;
        canvas.add_px = tramp_add_px; canvas.add_glow = tramp_add_glow;
        canvas.add_line = tramp_add_line; canvas.hsv = tramp_hsv;
    }

    bool hasData() const { return data.header.frameCount > 0 && !data.scalars.empty(); }
    void setData(VfxData&& d) {
        data = std::move(d);
        bandEnv.assign(data.header.bandCount, 0.f);
        envBass = envRms = envBeat = envMid = envTre = 0; envCen = 0.3f;
        frameNo = 0;
        computeCuts();
    }

    // Reactive mode: derive scene-change times from the track's energy structure.
    // A cut is placed where the smoothed loudness has drifted noticeably since the
    // last cut (a build-up or drop), spaced within [minGap, maxGap] seconds.
    void computeCuts() {
        cuts.clear();
        if (!hasData()) return;
        int N = (int)data.header.frameCount, fps = (int)data.header.fps;
        if (N < 2 || fps <= 0) return;
        std::vector<float> e(N);
        float acc = 0;
        for (int i = 0; i < N; i++) {
            float v = data.scalars[(size_t)i * VFX_NSCALARS + VFX_RMS];
            acc = lerpf(acc, v, 0.02f); e[i] = acc;    // ~1s smoothing
        }
        const double minGap = 12.0, maxGap = 38.0;
        cuts.push_back(0.f);
        double lastCut = 0; float refE = e[0];
        for (int i = 1; i < N; i++) {
            double ti = (double)i / fps;
            if ((fabsf(e[i] - refE) > 0.18f && ti - lastCut > minGap) ||
                (ti - lastCut > maxGap)) {
                cuts.push_back((float)ti); lastCut = ti; refE = e[i];
            }
        }
        cuts.push_back((float)data.header.duration + 1.f);
        fprintf(stderr, "reactive: %zu cut points\n", cuts.size());
    }

    // ---------- primitives ----------
    void addPx(int x, int y, RGB c, float k) {
        if ((unsigned)x >= (unsigned)W || (unsigned)y >= (unsigned)H) return;
        size_t i = ((size_t)y * W + x) * 3;
        fb[i] += c.r * k; fb[i+1] += c.g * k; fb[i+2] += c.b * k;
    }
    void addGlow(float fx, float fy, RGB c, float k, float rad) {
        int r = (int)ceilf(rad);
        for (int dy = -r; dy <= r; dy++)
            for (int dx = -r; dx <= r; dx++) {
                float q = (dx*dx + dy*dy) / (rad * rad);
                if (q > 1) continue;
                addPx((int)fx + dx, (int)fy + dy, c, k * (1 - q) * (1 - q));
            }
    }
    void addLine(float x0, float y0, float x1, float y1, RGB c, float k, float wpx) {
        float dx = x1 - x0, dy = y1 - y0;
        float len = sqrtf(dx*dx + dy*dy);
        int n = std::max(2, (int)(len * 1.1f));
        for (int i = 0; i <= n; i++) {
            float t = (float)i / n;
            addGlow(x0 + dx * t, y0 + dy * t, c, k / (0.6f * n), wpx);
        }
    }

    // ---------- scene scheduling (plugins only) ----------
    static constexpr float SCENE_LEN = 38.f, FADE = 4.f;
    int totalScenes() const { return (int)plugins.size(); }
    int sceneAt(double t, int slot) {
        int total = totalScenes(); if (total <= 0) return 0;
        long idx = (long)(t / SCENE_LEN) - slot;
        if (idx < 0) idx = 0;
        return (int)(idx % total);
    }
    VfxParams makeParams(double t, double dt, float alpha) {
        VfxParams p{};
        p.time = t; p.dt = dt; p.alpha = alpha;
        p.bass = envBass; p.mid = envMid; p.treble = envTre; p.rms = envRms;
        p.loud = data.scalarLerp(t, VFX_LOUD); p.centroid = envCen;
        p.flux = data.scalarLerp(t, VFX_FLUX); p.beat = envBeat;
        p.onset = data.scalarLerp(t, VFX_BEAT);
        p.bpm = data.header.bpm; p.duration = data.header.duration; p.frameNo = frameNo;
        p.bandCount = (int)data.header.bandCount; p.bands = bandEnv.data();
        p.width = W; p.height = H;
        p.image = imgPtr(); p.imageW = imgW; p.imageH = imgH; p.imageChannels = imgCh;
        return p;
    }
    void renderEntry(int idx, double t, double dt, float alpha) {
        if (idx < 0 || idx >= (int)plugins.size()) return;
        PluginHandle& h = plugins[idx];
        if (!h.render || !h.state) return;
        VfxParams p = makeParams(t, dt, alpha);
        h.render(h.state, &canvas, &p);
    }

    // Auto rotation: timed (fixed length) or reactive (score-driven cut points),
    // crossfading into the new scene at each segment boundary.
    // pool of scene indices to cycle: favorites only, or all
    void buildPool(std::vector<int>& pool) {
        int total = totalScenes();
        for (int i = 0; i < total; i++)
            if (!favoritesOnly || favSet.count(plugins[i].name)) pool.push_back(i);
        if (pool.empty()) for (int i = 0; i < total; i++) pool.push_back(i);
    }
    int poolPick(long k, int ps) {
        if (ps <= 0) return 0;
        if (autoMode == 2) {                      // shuffle: pseudo-random order
            int idx = (int)(hashu32((uint32_t)(k * 2654435761u)) % (uint32_t)ps);
            if (ps > 1) {
                int prev = (int)(hashu32((uint32_t)((k - 1) * 2654435761u)) % (uint32_t)ps);
                if (idx == prev) idx = (idx + 1) % ps;
            }
            return idx;
        }
        return (int)(((k % ps) + ps) % ps);
    }
    void autoRender(double t, double dt) {
        int total = totalScenes(); if (total <= 0) return;
        std::vector<int> pool; buildPool(pool);
        int ps = (int)pool.size();
        long k; double s0;
        if (autoMode == 1 && cuts.size() >= 2) {   // reactive: score-derived cut points
            k = 0;
            while (k + 1 < (long)cuts.size() && cuts[k + 1] <= t) k++;
            if (k < 0) k = 0;
            s0 = cuts[k];
        } else {                                   // timed / shuffle: fixed intervals
            k = (long)(t / SCENE_LEN); s0 = k * (double)SCENE_LEN;
        }
        int cur = pool[poolPick(k, ps)];
        float local = (float)(t - s0);
        if (local < FADE && k > 0 && ps > 1) {
            float a = smoothstepf(0.f, FADE, local);
            int prev = pool[poolPick(k - 1, ps)];
            renderEntry(prev, t, dt, 1.f - a);
            renderEntry(cur, t, dt, a);
        } else {
            renderEntry(cur, t, dt, 1.f);
        }
    }

    void frame(double t, double dt) {
        frameNo++;
        std::fill(fb.begin(), fb.end(), 0.f);
        if (hasData()) {
            float bass = data.scalarLerp(t, VFX_BASS), rms = data.scalarLerp(t, VFX_RMS);
            float mid  = data.scalarLerp(t, VFX_MID),  tre = data.scalarLerp(t, VFX_TREBLE);
            float cen  = data.scalarLerp(t, VFX_CENTROID), beat = data.scalarLerp(t, VFX_BEAT);
            auto env = [&](float& e, float v, float up, float dn) {
                e = v > e ? lerpf(e, v, up) : lerpf(e, v, dn);
            };
            env(envBass, bass, 0.5f, 0.06f);  env(envRms, rms, 0.4f, 0.05f);
            env(envMid,  mid,  0.5f, 0.07f);  env(envTre, tre, 0.5f, 0.10f);
            env(envCen,  cen,  0.05f, 0.05f); env(envBeat, beat, 0.8f, 0.10f);
            for (uint32_t b = 0; b < data.header.bandCount; b++)
                env(bandEnv[b], data.bandLerp(t, b), 0.6f, 0.12f);
        }
        int total = totalScenes();
        if (total > 0 && hasData()) {
            if (forceScene >= 0) {
                renderEntry(forceScene < total ? forceScene : 0, t, dt, 1.f);
            } else {
                autoRender(t, dt);
            }
        }
        postFX(t);
    }

    // ---------- post-processing: bloom -> shake+aberration -> resolve ----------
    void postFX(double t) {
        (void)t;
        for (int y = 0; y < BH; y++)
            for (int x = 0; x < BW; x++) {
                RGB s = {0,0,0};
                for (int dy = 0; dy < 4; dy++)
                    for (int dx = 0; dx < 4; dx++) {
                        size_t i = (((size_t)(y*4+dy)) * W + (x*4+dx)) * 3;
                        s.r += fb[i]; s.g += fb[i+1]; s.b += fb[i+2];
                    }
                size_t o = ((size_t)y * BW + x) * 3;
                float lum = (s.r + s.g + s.b) / 48.f;
                float k = smoothstepf(0.55f, 1.4f, lum);
                bloomA[o] = s.r / 16.f * k; bloomA[o+1] = s.g / 16.f * k; bloomA[o+2] = s.b / 16.f * k;
            }
        for (int pass = 0; pass < 2; pass++) {
            for (int y = 0; y < BH; y++)
                for (int x = 0; x < BW; x++) {
                    RGB s = {0,0,0};
                    for (int k = -2; k <= 2; k++) {
                        int xx = std::clamp(x + k, 0, BW - 1);
                        size_t i = ((size_t)y * BW + xx) * 3;
                        s.r += bloomA[i]; s.g += bloomA[i+1]; s.b += bloomA[i+2];
                    }
                    size_t o = ((size_t)y * BW + x) * 3;
                    bloomB[o] = s.r / 5; bloomB[o+1] = s.g / 5; bloomB[o+2] = s.b / 5;
                }
            for (int y = 0; y < BH; y++)
                for (int x = 0; x < BW; x++) {
                    RGB s = {0,0,0};
                    for (int k = -2; k <= 2; k++) {
                        int yy = std::clamp(y + k, 0, BH - 1);
                        size_t i = ((size_t)yy * BW + x) * 3;
                        s.r += bloomB[i]; s.g += bloomB[i+1]; s.b += bloomB[i+2];
                    }
                    size_t o = ((size_t)y * BW + x) * 3;
                    bloomA[o] = s.r / 5; bloomA[o+1] = s.g / 5; bloomA[o+2] = s.b / 5;
                }
        }
        float shake = 5.5f * envBeat * envBeat;
        float shx = (hash21(frameNo, 41) - 0.5f) * 2.f * shake;
        float shy = (hash21(frameNo, 42) - 0.5f) * 2.f * shake;
        float ca = 0.0025f + 0.008f * envBeat;
        auto sampleFB = [&](float fx, float fy, int c) -> float {
            int x0 = (int)floorf(fx), y0 = (int)floorf(fy);
            float ax = fx - x0, ay = fy - y0;
            x0 = std::clamp(x0, 0, W - 2); y0 = std::clamp(y0, 0, H - 2);
            size_t i00 = ((size_t)y0 * W + x0) * 3 + c;
            return lerpf(lerpf(fb[i00], fb[i00 + 3], ax),
                         lerpf(fb[i00 + (size_t)W * 3], fb[i00 + (size_t)W * 3 + 3], ax), ay);
        };
        parallelRows([&](int y) {
            for (int x = 0; x < W; x++) {
                float dx = x - W * 0.5f, dy = y - H * 0.5f;
                size_t o = ((size_t)y * W + x) * 3;
                float off[3] = { 1.f + ca, 1.f, 1.f - ca };
                for (int c = 0; c < 3; c++) {
                    float sx2 = W * 0.5f + dx * off[c] + shx;
                    float sy2 = H * 0.5f + dy * off[c] + shy;
                    resolve[o + c] = sampleFB(sx2, sy2, c);
                }
                float bx = clampf((float)x / 4.f - 0.5f, 0.f, BW - 1.001f);
                float by = clampf((float)y / 4.f - 0.5f, 0.f, BH - 1.001f);
                int bx0 = (int)bx, by0 = (int)by;
                float axc = bx - bx0, ayc = by - by0;
                size_t b00 = ((size_t)by0 * BW + bx0) * 3;
                for (int c = 0; c < 3; c++) {
                    float v = lerpf(lerpf(bloomA[b00+c], bloomA[b00+3+c], axc),
                                    lerpf(bloomA[b00+(size_t)BW*3+c], bloomA[b00+(size_t)BW*3+3+c], axc), ayc);
                    resolve[o + c] += v * 0.9f;
                }
            }
        });
    }

    // tonemap + vignette + grain -> RGB24
    void toRGB24(std::vector<uint8_t>& out) {
        out.resize((size_t)W * H * 3);
        parallelRows([&](int y) {
            float vy = (float)y / H - 0.48f;
            for (int x = 0; x < W; x++) {
                float vx = (float)x / W - 0.5f;
                float vig = clampf(1.f - 1.10f * (vx * vx + vy * vy), 0.f, 1.f);
                float grain = (hash21(x + frameNo * 613, y) - 0.5f) * 0.020f;
                size_t i = ((size_t)y * W + x) * 3;
                for (int c = 0; c < 3; c++) {
                    float v = resolve[i + c] * vig;
                    v = 1.f - expf(-v * 1.55f);
                    v = powf(v, 1.f / 2.2f) + grain;
                    out[i + c] = (uint8_t)(clampf(v, 0.f, 1.f) * 255.f + 0.5f);
                }
            }
        });
    }
};

// ---- canvas trampolines ----
static void tramp_add_px(const VfxCanvas* c, int x, int y, float r, float g, float b, float k) {
    ((Renderer*)c->impl)->addPx(x, y, RGB{r, g, b}, k);
}
static void tramp_add_glow(const VfxCanvas* c, float x, float y, float r, float g, float b, float k, float rad) {
    ((Renderer*)c->impl)->addGlow(x, y, RGB{r, g, b}, k, rad);
}
static void tramp_add_line(const VfxCanvas* c, float x0, float y0, float x1, float y1, float r, float g, float b, float k, float w) {
    ((Renderer*)c->impl)->addLine(x0, y0, x1, y1, RGB{r, g, b}, k, w);
}
static void tramp_hsv(float h, float s, float v, float* r, float* g, float* b) {
    RGB c = hsvf(h, s, v); *r = c.r; *g = c.g; *b = c.b;
}

// ---- plugin discovery/loading ----
static bool loadPluginHandle(const std::string& path, PluginHandle& out) {
    void* dl = dynOpen(path.c_str());
    if (!dl) return false;
    auto info = (vfx_info_fn)   dynSym(dl, "vfx_plugin_info");
    auto cr   = (vfx_create_fn) dynSym(dl, "vfx_plugin_create");
    auto de   = (vfx_destroy_fn)dynSym(dl, "vfx_plugin_destroy");
    auto rn   = (vfx_render_fn) dynSym(dl, "vfx_plugin_render");
    if (!info || !cr || !de || !rn) { dynClose(dl); return false; }
    const VfxPluginInfo* pi = info();
    if (!pi || pi->abi != VFX_PLUGIN_ABI) { dynClose(dl); return false; }
    out.dl = dl; out.info = pi; out.create = cr; out.destroy = de; out.render = rn;
    out.name = pi->name ? pi->name : "plugin";
    return true;
}
static bool isPluginExt(const std::string& ext) {
#if defined(_WIN32)
    return ext == ".dll";
#elif defined(__APPLE__)
    return ext == ".dylib" || ext == ".so";   // MODULE libs can be either on macOS
#else
    return ext == ".so";
#endif
}
static void scanPluginDir(const std::string& dir, std::vector<std::string>& out) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return;
    std::vector<std::string> files;
    for (auto& e : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        auto p = e.path();
        if (isPluginExt(p.extension().string())) files.push_back(p.string());
    }
    std::sort(files.begin(), files.end());
    for (auto& f : files) out.push_back(f);
}
static std::vector<PluginHandle> loadPlugins(const std::vector<std::string>& dirs) {
    std::vector<std::string> files;
    for (auto& d : dirs) scanPluginDir(d, files);
    std::vector<PluginHandle> loaded;
    for (auto& f : files) {
        PluginHandle h;
        if (loadPluginHandle(f, h)) loaded.push_back(h);
        else fprintf(stderr, "warn: skip plugin %s\n", f.c_str());
    }
    return loaded;
}
static std::vector<std::string> defaultPluginDirs(const std::vector<std::string>& user) {
    if (!user.empty()) return user;
    std::string ed = exeDir();
    return { ed + "/plugins", ed + "/../plugins", ed + "/../Resources/plugins" };
}

// ================================================================== main
static void printScenes(const std::vector<PluginHandle>& plugins) {
    for (auto& h : plugins) printf("%s\n", h.name.c_str());
    fflush(stdout);
}
static int findScene(const std::vector<PluginHandle>& plugins, const std::string& name) {
    auto lc = [](std::string s){ for (auto& ch : s) ch = (char)tolower((unsigned char)ch); return s; };
    std::string want = lc(name);
    for (size_t i = 0; i < plugins.size(); i++)
        if (lc(plugins[i].name) == want) return (int)i;
    return -1;
}

#ifdef USE_SDL
// ---- audio context (callback-driven, mute-aware, drives the master clock) ----
struct AudioCtx {
    std::vector<short> pcm;   // interleaved 16-bit
    int hz = 0, ch = 2;
    std::atomic<size_t> pos{0};
    std::atomic<bool>   muted{false};
    std::atomic<bool>   hasAudio{false};
};
static void audioCB(void* ud, Uint8* stream, int len) {
    AudioCtx* a = (AudioCtx*)ud;
    Sint16* out = (Sint16*)stream;
    int n = len / (int)sizeof(Sint16);
    size_t p = a->pos.load(std::memory_order_relaxed);
    size_t sz = a->pcm.size();
    bool m = a->muted.load(std::memory_order_relaxed);
    for (int i = 0; i < n; i++) {
        if (p < sz) { out[i] = m ? 0 : a->pcm[p]; p++; }   // advance even when muted
        else out[i] = 0;
    }
    a->pos.store(p, std::memory_order_relaxed);
}
#endif

// ---- favorites persistence (~/.veffects_favorites.txt) ----
static std::string favPath() {
    const char* h = getenv("HOME");
#if defined(_WIN32)
    if (!h || !*h) h = getenv("USERPROFILE");
#endif
    std::string base = (h && *h) ? h : ".";
    return base + "/.veffects_favorites.txt";
}
static void loadFavs(std::set<std::string>& s) {
    std::ifstream f(favPath()); std::string line;
    while (std::getline(f, line)) { if (!line.empty()) s.insert(line); }
}
static void saveFavs(const std::set<std::string>& s) {
    std::ofstream f(favPath()); for (auto& n : s) f << n << "\n";
}

// Render the whole track (one scene) to an mp4 via ffmpeg. Returns 1 ok, 2 no ffmpeg, 3 aborted.
static int exportMp4(Renderer& R, const std::string& audio, const std::string& outPath,
                     int sceneIdx, std::atomic<bool>* abort = nullptr,
                     std::atomic<int>* progress = nullptr) {
    if (!R.hasData()) return 2;
    std::string cmd = "ffmpeg -y -loglevel error -f rawvideo -pix_fmt rgb24 -s 640x480 -r 30 -i - ";
    bool ha = vfxHasExt(audio, ".mp3") || vfxHasExt(audio, ".wav") || vfxHasExt(audio, ".flac");
    if (ha) cmd += "-i \"" + audio + "\" ";
    cmd += "-c:v libx264 -pix_fmt yuv420p ";
    if (ha) cmd += "-c:a aac -shortest ";
    cmd += "\"" + outPath + "\"";
    FILE* pipe = VFX_POPEN(cmd.c_str(), "w");
    if (!pipe) return 2;
    int fps = 30, nF = (int)(R.data.header.duration * fps); double ddt = 1.0 / fps;
    int saved = R.forceScene; R.forceScene = sceneIdx;
    std::vector<uint8_t> buf;
    for (int i = 0; i < nF; i++) {
        if (abort && abort->load()) break;
        R.frame(i * ddt, ddt); R.toRGB24(buf);
        fwrite(buf.data(), 1, buf.size(), pipe);
        if (progress && (i & 15) == 0) progress->store((int)(100.0 * i / std::max(nF, 1)));
    }
    R.forceScene = saved;
    VFX_PCLOSE(pipe);
    return (abort && abort->load()) ? 3 : 1;
}

int main(int argc, char** argv) {
    std::string inPath, audioPath, sceneName, imagePath, exportPath;
    bool renderMode = false, listScenes = false, reactiveFlag = false;
    int outFps = 30, forceScene = -1;
    double tStart = 0, tEnd = -1;
    std::vector<std::string> pluginDirs;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if      (a == "--render") renderMode = true;
        else if (a == "--image"      && i + 1 < argc) imagePath = argv[++i];
        else if (a == "--audio"      && i + 1 < argc) audioPath = argv[++i];
        else if (a == "--fps"        && i + 1 < argc) outFps = atoi(argv[++i]);
        else if (a == "--start"      && i + 1 < argc) tStart = atof(argv[++i]);
        else if (a == "--end"        && i + 1 < argc) tEnd = atof(argv[++i]);
        else if (a == "--scene"      && i + 1 < argc) forceScene = atoi(argv[++i]);
        else if (a == "--scene-name" && i + 1 < argc) sceneName = argv[++i];
        else if (a == "--plugins"    && i + 1 < argc) pluginDirs.push_back(argv[++i]);
        else if (a == "--export"     && i + 1 < argc) exportPath = argv[++i];
        else if (a == "--reactive")  reactiveFlag = true;
        else if (a == "--list-scenes") listScenes = true;
        else if (inPath.empty()) inPath = a;
    }

    std::vector<PluginHandle> loaded = loadPlugins(defaultPluginDirs(pluginDirs));

    if (listScenes) { printScenes(loaded); return 0; }

    Renderer R;
    R.plugins = std::move(loaded);
    for (auto& h : R.plugins) h.state = h.create ? h.create(W, H) : nullptr;
    if (!R.plugins.empty()) fprintf(stderr, "plugins: %zu loaded\n", R.plugins.size());

    if (!sceneName.empty()) {
        int idx = findScene(R.plugins, sceneName);
        if (idx >= 0) forceScene = idx;
        else fprintf(stderr, "warn: scene '%s' not found\n", sceneName.c_str());
    }
    R.forceScene = forceScene;
    if (reactiveFlag) { R.autoMode = 1; R.forceScene = -1; }
    loadFavs(R.favSet);

    if (!imagePath.empty()) {
        std::vector<unsigned char> px; int w, h;
        if (loadImageFile(imagePath, px, w, h)) { R.setImage(std::move(px), w, h, 3);
            fprintf(stderr, "image: %s (%dx%d)\n", imagePath.c_str(), w, h);
            if (sceneName.empty() && R.forceScene < 0) {   // default to an image scene
                int iw = findScene(R.plugins, "Image World 3D");
                if (iw >= 0) R.forceScene = iw;
            } }
        else fprintf(stderr, "warn: cannot load image %s\n", imagePath.c_str());
    }

    // ---- load initial track (mp3 -> analyze, or .veffects directly) ----
    VfxAudioPCM startPcm;
    auto endsWith = [](const std::string& s, const char* suf){
        size_t n = strlen(suf); return s.size() >= n && s.compare(s.size()-n, n, suf) == 0;
    };
    if (!inPath.empty()) {
        if (endsWith(inPath, ".veffects")) {
            VfxData d;
            if (!vfxLoad(inPath, d)) { fprintf(stderr, "error: cannot load %s\n", inPath.c_str()); return 1; }
            R.setData(std::move(d));
            if (!audioPath.empty()) {
                VfxData tmp; vfxAnalyzeMp3(audioPath, tmp, &startPcm);   // decode only for playback
            }
        } else {
            VfxData d; std::string err;
            fprintf(stderr, "analyzing %s ...\n", inPath.c_str());
            if (!vfxAnalyzeMp3(inPath, d, &startPcm, &err)) { fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
            R.setData(std::move(d));
            if (audioPath.empty()) audioPath = inPath;
        }
        fprintf(stderr, "loaded: %.1f s, %u frames, bpm %.1f\n",
                R.data.header.duration, R.data.header.frameCount, R.data.header.bpm);
    }
    if (tEnd < 0) tEnd = R.hasData() ? R.data.header.duration : 0;

    // ---- headless render mode (offline mp4 via a pipe) ----
    if (renderMode) {
        if (!R.hasData()) { fprintf(stderr, "error: --render needs an input track\n"); return 1; }
        std::vector<uint8_t> rgb;
        int nFrames = (int)((tEnd - tStart) * outFps);
        double dt = 1.0 / outFps;
        for (int i = 0; i < nFrames; i++) {
            double t = tStart + i * dt;
            R.frame(t, dt);
            R.toRGB24(rgb);
            fwrite(rgb.data(), 1, rgb.size(), stdout);
            if (i % (outFps * 5) == 0) fprintf(stderr, "\rrender: %.1f / %.1f s", t - tStart, tEnd - tStart);
        }
        fprintf(stderr, "\rrender done: %d frames            \n", nFrames);
        return 0;
    }

    // ---- headless one-shot mp4 export (also used by the GUI Export button) ----
    if (!exportPath.empty()) {
        std::string audio = audioPath.empty() ? inPath : audioPath;
        fprintf(stderr, "exporting %s ...\n", exportPath.c_str());
        int r = exportMp4(R, audio, exportPath, forceScene);
        if (r == 1) { fprintf(stderr, "saved %s\n", exportPath.c_str()); return 0; }
        fprintf(stderr, "export failed (is ffmpeg on PATH?)\n"); return 1;
    }

#ifdef USE_SDL
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "SDL error: %s\n", SDL_GetError()); return 1;
    }
    int winW = 820, winH = 620;
    SDL_Window* win = SDL_CreateWindow("veffects", SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED, winW, winH, SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE);
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_Texture* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB24,
        SDL_TEXTUREACCESS_STREAMING, W, H);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;   // don't write imgui.ini next to the binary
    ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForSDLRenderer(win, ren);
    ImGui_ImplSDLRenderer2_Init(ren);

    // ---- audio device ----
    AudioCtx actx;
    SDL_AudioDeviceID adev = 0;
    auto openAudio = [&](VfxAudioPCM&& pcm) {
        if (adev) { SDL_CloseAudioDevice(adev); adev = 0; }
        actx.pos.store(0);
        actx.hasAudio.store(false);
        if (pcm.hz > 0 && !pcm.interleaved.empty()) {
            actx.pcm = std::move(pcm.interleaved);
            actx.hz = pcm.hz; actx.ch = pcm.ch;
            SDL_AudioSpec want{}, have{};
            want.freq = actx.hz; want.format = AUDIO_S16SYS;
            want.channels = (Uint8)actx.ch; want.samples = 1024;
            want.callback = audioCB; want.userdata = &actx;
            adev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
            if (adev) { actx.hasAudio.store(true); SDL_PauseAudioDevice(adev, 0); }
        }
    };
    if (R.hasData() && startPcm.hz > 0) openAudio(std::move(startPcm));

    // ---- background analysis job (Open) ----
    struct LoadJob {
        std::thread th; std::atomic<int> state{0};   // 0 idle,1 running,2 done,3 error
        std::string path, err, name; VfxData data; VfxAudioPCM pcm;
    } job;
    auto startLoad = [&](const std::string& path) {
        if (job.state.load() == 1) return;
        if (job.th.joinable()) job.th.join();
        job.path = path; job.err.clear();
        job.name = std::filesystem::path(path).filename().string();
        job.state.store(1);
        job.th = std::thread([&job]{
            VfxData d; VfxAudioPCM pcm; std::string err;
            bool ok = vfxAnalyzeMp3(job.path, d, &pcm, &err);
            if (ok) { job.data = std::move(d); job.pcm = std::move(pcm); job.state.store(2); }
            else    { job.err = err; job.state.store(3); }
        });
    };

    // clock: perf-counter fallback when there is no audio
    Uint64 pfreq = SDL_GetPerformanceFrequency();
    Uint64 t0 = SDL_GetPerformanceCounter();
    double tManual = 0; bool paused = false;
    std::vector<uint8_t> rgb;
    std::string trackName = inPath.empty() ? "" : std::filesystem::path(inPath).filename().string();
    std::string imageName = imagePath.empty() ? "" : std::filesystem::path(imagePath).filename().string();
    std::string status;

    // ---- offline mp4 export (renders the selected scene through ffmpeg) ----
    std::string audioSrc = audioPath.empty() ? inPath : audioPath;   // for muxing audio
    std::string lastExportName;
    std::atomic<bool> exporting{false}, abortExport{false};
    std::atomic<int>  exportProgress{0}, exportResult{0};   // result: 0 none,1 saved,2 noffmpeg,3 aborted
    std::thread exportTh;
    auto startExport = [&](std::string outPath){
        if (exporting.load() || !R.hasData()) return;
        if (exportTh.joinable()) exportTh.join();
        lastExportName = std::filesystem::path(outPath).filename().string();
        abortExport.store(false); exportProgress.store(0); exportResult.store(0);
        exporting.store(true);
        int sceneForExport = R.forceScene;
        std::string audio = audioSrc;
        exportTh = std::thread([&, outPath, audio, sceneForExport]{
            int r = exportMp4(R, audio, outPath, sceneForExport, &abortExport, &exportProgress);
            exportProgress.store(100);
            exportResult.store(r);
            exporting.store(false);
        });
    };

    // scene combo items
    auto sceneItems = [&]{
        std::vector<std::string> v; v.push_back("Auto (cycle all)");
        for (auto& h : R.plugins) v.push_back(h.name);
        return v;
    };
    std::vector<std::string> scenes = sceneItems();
    int sceneSel = (R.forceScene >= 0) ? R.forceScene + 1 : 0;

    // When an image is loaded, jump to an image-driven scene so it visibly does
    // something (unless the user is already on one).
    auto switchToImageScene = [&]{
        int pp = findScene(R.plugins, "Photo Particles");
        int iw = findScene(R.plugins, "Image World 3D");
        if (R.forceScene == pp || R.forceScene == iw) return;
        int target = (iw >= 0) ? iw : pp;
        if (target >= 0) { R.forceScene = target; sceneSel = target + 1;
                           status = "Image loaded -> " + R.plugins[target].name; }
    };

    bool run = true;
    while (run) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL2_ProcessEvent(&e);
            ImGuiIO& io = ImGui::GetIO();
            if (e.type == SDL_QUIT) run = false;
            if (e.type == SDL_DROPFILE) {                    // drag & drop mp3 or image
                char* f = e.drop.file;
                if (f) {
                    std::string fp = f;
                    if (isImagePath(fp)) {
                        std::vector<unsigned char> px; int w, h;
                        if (loadImageFile(fp, px, w, h)) {
                            R.setImage(std::move(px), w, h, 3);
                            imageName = std::filesystem::path(fp).filename().string();
                            status.clear();
                            switchToImageScene();
                        } else status = "Cannot load image";
                    } else { startLoad(fp); status = "Analyzing..."; }
                    SDL_free(f);
                }
            }
            if (e.type == SDL_KEYDOWN && !io.WantCaptureKeyboard) {
                SDL_Keycode k = e.key.keysym.sym;
                if (k == SDLK_ESCAPE) run = false;
                if (k == SDLK_SPACE) { paused = !paused; if (adev) SDL_PauseAudioDevice(adev, paused ? 1 : 0); }
                if (k == SDLK_m) { bool nm = !actx.muted.load(); actx.muted.store(nm); }
                if (k >= SDLK_1 && k <= SDLK_9) {
                    int idx = (int)(k - SDLK_1);
                    if (idx < R.totalScenes()) { R.forceScene = idx; sceneSel = idx + 1; }
                }
                if (k == SDLK_0) { R.forceScene = -1; sceneSel = 0; }
            }
        }

        // apply finished analysis
        if (job.state.load() == 2) {
            if (job.th.joinable()) job.th.join();
            R.setData(std::move(job.data));
            openAudio(std::move(job.pcm));
            trackName = job.name; audioSrc = job.path; status.clear(); paused = false;
            tManual = 0; t0 = SDL_GetPerformanceCounter();
            job.state.store(0);
        } else if (job.state.load() == 3) {
            if (job.th.joinable()) job.th.join();
            status = "Analyze failed: " + job.err;
            job.state.store(0);
        }

        // ---- master clock ----
        double t;
        if (actx.hasAudio.load()) {
            size_t p = actx.pos.load();
            t = (double)(p / std::max(1, actx.ch)) / std::max(1, actx.hz);
            if (R.hasData() && t >= R.data.header.duration - 0.02) {   // loop
                actx.pos.store(0); t = 0;
            }
        } else {
            Uint64 now = SDL_GetPerformanceCounter();
            if (!paused) tManual += (double)(now - t0) / pfreq;
            t0 = now;
            t = tManual;
            if (R.hasData() && t > R.data.header.duration) { tManual = 0; t = 0; }
        }

        // pick up a finished export result (set status on the main thread)
        if (!exporting.load() && exportResult.load() != 0) {
            int r = exportResult.exchange(0);
            if (r == 1) status = "Saved " + lastExportName;
            else if (r == 2) status = "Export failed: ffmpeg not found on PATH";
            else if (r == 3) status = "Export aborted";
        }

        // ---- render visualization to the streaming texture ----
        // (skipped while exporting: the export thread owns the renderer then)
        double dt = 1.0 / 60.0;
        if (!exporting.load()) {
            R.frame(t, dt);
            R.toRGB24(rgb);
            SDL_UpdateTexture(tex, NULL, rgb.data(), W * 3);
        }

        // ---- ImGui panel ----
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        {
            ImGui::SetNextWindowPos(ImVec2(12, 12), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(300, 0), ImGuiCond_FirstUseEver);
            ImGui::Begin("veffects", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::TextColored(ImVec4(0.3f, 0.85f, 0.95f, 1), "veffects");
            ImGui::SameLine(); ImGui::TextDisabled("music -> math visuals");

            ImGui::BeginDisabled(exporting.load());   // lock controls while exporting
            if (ImGui::Button("Open audio...")) {
                const char* filt[5] = { "*.mp3", "*.wav", "*.flac", "*.mid", "*.midi" };
                const char* f = tinyfd_openFileDialog("Open audio (mp3/wav/flac/midi)", "", 5, filt, "audio", 0);
                if (f) { startLoad(f); status = "Analyzing..."; }
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%s", trackName.empty() ? "(no track - drag audio here)" : trackName.c_str());

            if (ImGui::Button("Open image...")) {
                const char* filt[4] = { "*.png", "*.jpg", "*.jpeg", "*.bmp" };
                const char* f = tinyfd_openFileDialog("Open an image", "", 4, filt, "images", 0);
                if (f) {
                    std::vector<unsigned char> px; int w, h;
                    if (loadImageFile(f, px, w, h)) {
                        R.setImage(std::move(px), w, h, 3);
                        imageName = std::filesystem::path(f).filename().string();
                        switchToImageScene();
                    }
                }
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%s", imageName.empty() ? "(for image scenes)" : imageName.c_str());
            {   // choose how the loaded image becomes a world
                ImGui::TextDisabled("Show image as:"); ImGui::SameLine();
                int pp = findScene(R.plugins, "Photo Particles");
                int iw = findScene(R.plugins, "Image World 3D");
                if (ImGui::SmallButton("2D") && pp >= 0) { R.forceScene = pp; sceneSel = pp + 1; }
                ImGui::SameLine();
                if (ImGui::SmallButton("3D") && iw >= 0) { R.forceScene = iw; sceneSel = iw + 1; }
            }

            // scene dropdown ("* " marks favorites)
            if (!scenes.empty()) {
                std::vector<std::string> labels; labels.reserve(scenes.size());
                labels.push_back(scenes[0]);
                for (size_t i = 1; i < scenes.size(); i++)
                    labels.push_back((R.favSet.count(scenes[i]) ? "* " : "") + scenes[i]);
                std::vector<const char*> ci; for (auto& s : labels) ci.push_back(s.c_str());
                if (ImGui::Combo("Scene", &sceneSel, ci.data(), (int)ci.size()))
                    R.forceScene = (sceneSel == 0) ? -1 : sceneSel - 1;
            }
            ImGui::SameLine();
            if (ImGui::Button("Random")) {
                int total = R.totalScenes();
                if (total > 0) {
                    int idx = (int)(SDL_GetPerformanceCounter() % (Uint64)total);
                    R.forceScene = idx; sceneSel = idx + 1;
                }
            }
            if (sceneSel >= 1 && sceneSel - 1 < (int)R.plugins.size()) {   // favorite the current scene
                std::string nm = R.plugins[sceneSel - 1].name;
                bool fav = R.favSet.count(nm) > 0;
                if (ImGui::Checkbox("Favorite", &fav)) {
                    if (fav) R.favSet.insert(nm); else R.favSet.erase(nm);
                    saveFavs(R.favSet);
                }
            }
            if (sceneSel == 0) {                     // Auto: how to switch scenes
                const char* modes[] = { "Timed", "Reactive", "Shuffle" };
                int am = R.autoMode;
                ImGui::SetNextItemWidth(140);
                if (ImGui::Combo("Auto mode", &am, modes, 3)) R.autoMode = am;
                ImGui::SameLine(); ImGui::TextDisabled("(Reactive follows the music)");
                bool fo = R.favoritesOnly;
                if (ImGui::Checkbox("Favorites only", &fo)) R.favoritesOnly = fo;
            }

            // mute + transport
            bool muted = actx.muted.load();
            if (ImGui::Checkbox("Mute original audio", &muted)) actx.muted.store(muted);
            if (ImGui::Button(paused ? "Play" : "Pause")) {
                paused = !paused; if (adev) SDL_PauseAudioDevice(adev, paused ? 1 : 0);
            }
            ImGui::SameLine();
            double dur = R.hasData() ? R.data.header.duration : 0;
            ImGui::Text("%02d:%05.2f / %02d:%05.2f", (int)t/60, fmod(t,60.0), (int)dur/60, fmod(dur,60.0));
            if (R.hasData() && R.data.header.bpm > 0) { ImGui::SameLine(); ImGui::TextDisabled("~%.0f BPM", R.data.header.bpm); }

            // seek slider
            if (R.hasData()) {
                float tt = (float)t;
                ImGui::SetNextItemWidth(240);
                if (ImGui::SliderFloat("##seek", &tt, 0.f, (float)std::max(dur, 0.1), "%.1f s")) {
                    if (actx.hasAudio.load()) actx.pos.store((size_t)((double)tt * actx.hz * actx.ch));
                    else tManual = tt;
                }
            }
            ImGui::EndDisabled();

            // export
            if (R.hasData()) {
                if (exporting.load()) {
                    ImGui::Text("Exporting... %d%%", exportProgress.load());
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel")) abortExport.store(true);
                } else if (ImGui::Button("Export mp4...")) {
                    const char* f = tinyfd_saveFileDialog("Export mp4", "veffects.mp4", 0, nullptr, nullptr);
                    if (f) startExport(f);
                }
                ImGui::SameLine();
                ImGui::TextDisabled("(current scene, needs ffmpeg)");
            }

            if (!status.empty()) ImGui::TextColored(ImVec4(1,0.7f,0.2f,1), "%s", status.c_str());
            ImGui::TextDisabled("keys: 1-9 scene, 0 auto, Space pause, M mute, Esc quit");
            ImGui::End();
        }
        ImGui::Render();

        // ---- compose: letterboxed viz + ImGui overlay ----
        int ww, wh; SDL_GetRendererOutputSize(ren, &ww, &wh);
        SDL_SetRenderDrawColor(ren, 6, 7, 12, 255);
        SDL_RenderClear(ren);
        float scale = std::min((float)ww / W, (float)wh / H);
        SDL_Rect dst;
        dst.w = (int)(W * scale); dst.h = (int)(H * scale);
        dst.x = (ww - dst.w) / 2; dst.y = (wh - dst.h) / 2;
        SDL_RenderCopy(ren, tex, NULL, &dst);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), ren);
        SDL_RenderPresent(ren);
    }

    abortExport.store(true);
    if (exportTh.joinable()) exportTh.join();
    if (job.th.joinable()) job.th.join();
    if (adev) SDL_CloseAudioDevice(adev);
    for (auto& h : R.plugins) if (h.destroy && h.state) h.destroy(h.state);
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyTexture(tex); SDL_DestroyRenderer(ren); SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
#else
    fprintf(stderr, "Built without SDL/GUI. Use --render or --list-scenes.\n");
    return R.hasData() ? 0 : 1;
#endif
}
