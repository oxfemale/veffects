// matrix.cpp -- "Matrix" scene plugin.
// The iconic full-screen cascade of glowing green code: dense falling columns of
// glyphs with bright white leading characters and long fading trails, parallax
// depth from multiple layers at different scales/speeds, a faint darker-green
// "code wall" backdrop, and occasional bright surges sweeping down the screen.
//
// All animation is derived from the absolute time p->time, so any frame renders
// correctly without simulation history (offline render and seeking both work).

#include "veffects_plugin.h"
#include <cmath>
#include <cstdint>
#include <vector>

static inline float clampf(float x, float a, float b){ return x<a?a:(x>b?b:x); }
static inline uint32_t hashu(uint32_t x){
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x;
}
static inline float hashf(uint32_t a, uint32_t b){
    return (hashu(a*0x9e3779b1U ^ b*0x85ebca77U) & 0xffffff) / 16777215.f;
}
static inline void putAdd(float* fb, int W, int H, int x, int y,
                          float r, float g, float b, float k){
    if ((unsigned)x >= (unsigned)W || (unsigned)y >= (unsigned)H) return;
    size_t i = ((size_t)y*W + x)*3;
    fb[i] += r*k; fb[i+1] += g*k; fb[i+2] += b*k;
}

// Compact procedural "code" glyph: half-width katakana / digit feel built from a
// few thick strokes inside a cw x ch cell. Cheap: a handful of short runs.
static void drawGlyph(float* fb, int W, int H, int px, int py, int cw, int ch,
                      uint32_t seed, float r, float g, float b, float k){
    if (k < 0.004f) return;
    int x0 = px+1, y0 = py+1, iw = cw-2, ih = ch-2;
    if (iw < 2 || ih < 2) return;
    // horizontal strokes
    int nh = 1 + (int)(hashf(seed,1)*2.99f);
    for(int s=0;s<nh;s++){
        int ry = y0 + (int)(hashf(seed, 10+s)*(ih-1));
        int xa = x0 + (int)(hashf(seed, 20+s)*iw*0.4f);
        int xb = x0 + iw - (int)(hashf(seed, 30+s)*iw*0.4f);
        for(int x=xa;x<=xb;x++) putAdd(fb,W,H,x,ry,r,g,b,k);
    }
    // vertical strokes
    int nv = 1 + (int)(hashf(seed,2)*1.99f);
    for(int s=0;s<nv;s++){
        int rx = x0 + (int)(hashf(seed, 40+s)*(iw-1));
        int ya = y0 + (int)(hashf(seed, 50+s)*ih*0.35f);
        int yb = y0 + ih - (int)(hashf(seed, 60+s)*ih*0.35f);
        for(int y=ya;y<=yb;y++) putAdd(fb,W,H,rx,y,r,g,b,k);
    }
    // occasional dot / short diagonal fleck for texture
    if(hashf(seed,3) > 0.55f){
        int dx = x0 + (int)(hashf(seed,5)*(iw-1));
        int dy = y0 + (int)(hashf(seed,6)*(ih-1));
        putAdd(fb,W,H,dx,dy,r,g,b,k);
        putAdd(fb,W,H,dx+1,dy,r,g,b,k*0.6f);
    }
}

struct Col { float phase, speed, flick; int len; uint32_t seed; };
struct Layer {
    int cw, ch, cols;
    float speedBase;   // px/sec
    float bright;      // depth brightness multiplier
    int   trail;       // max trail length in cells
    std::vector<Col> col;
};
struct State {
    int W, H;
    std::vector<Layer> layers;
};

static VfxPluginInfo INFO = {
    VFX_PLUGIN_ABI, "Matrix", "veffects",
    "The iconic full-screen cascade of glowing green Matrix code: dense falling glyph columns with bright white leaders, long fading trails, parallax depth and sweeping surges."
};
VFX_EXPORT const VfxPluginInfo* vfx_plugin_info(void){ return &INFO; }

static void buildLayer(Layer& L, int W, int H, int cw, int ch, float speedBase,
                       float bright, int trail, uint32_t salt){
    L.cw = cw; L.ch = ch; L.speedBase = speedBase; L.bright = bright; L.trail = trail;
    L.cols = W / cw;
    L.col.resize(L.cols);
    for(int c=0;c<L.cols;c++){
        Col& r = L.col[c];
        uint32_t sd = hashu((uint32_t)c*2654435761U ^ salt);
        r.seed  = sd;
        r.phase = hashf(sd,1);
        r.speed = speedBase * (0.7f + 0.6f*hashf(sd,2));
        r.len   = (int)(trail*(0.55f + 0.45f*hashf(sd,3)));
        r.flick = hashf(sd,4)*100.f;
    }
}

VFX_EXPORT void* vfx_plugin_create(int W, int H){
    State* s = new State();
    s->W = W; s->H = H;
    s->layers.resize(3);
    // far (small, dim, slow) -> near (large, bright, fast) : parallax depth
    buildLayer(s->layers[0], W, H,  8, 11,  55.f, 0.42f, 26, 0x1111u); // far code wall depth
    buildLayer(s->layers[1], W, H, 11, 15,  95.f, 0.80f, 22, 0x2222u); // mid
    buildLayer(s->layers[2], W, H, 15, 20, 150.f, 1.25f, 18, 0x3333u); // near
    return s;
}
VFX_EXPORT void vfx_plugin_destroy(void* st){ delete (State*)st; }

VFX_EXPORT void vfx_plugin_render(void* st, const VfxCanvas* cv, const VfxParams* p){
    State* s = (State*)st;
    float* fb = cv->fb; int W = cv->width, H = cv->height;
    float A = p->alpha;
    float bass=p->bass, tre=p->treble, rms=p->rms, beat=p->beat, onset=p->onset;
    double t = p->time;

    float speedMul = 1.f + 0.55f*bass;                 // bass drives fall speed
    float globBright = 0.72f + 0.85f*rms;              // rms drives overall brightness

    // --- faint dark-green vertical gradient background ---
    for(int y=0;y<H;y+=2){
        float gy = (float)y/H;
        float m = 0.006f + 0.016f*gy*gy;
        for(int x=0;x<W;x+=2) putAdd(fb,W,H,x,y, 0.0f,0.45f,0.14f, m*A);
    }

    // --- sweeping surge: a soft bright band gliding down, occasionally strong ---
    // travels analytically; its intensity swells slowly and pops on strong beats.
    float surgeSpeed = 130.f + 220.f*bass;
    float surgeSpan  = H + 220.f;
    float surgeY = fmodf((float)t*surgeSpeed, surgeSpan) - 110.f;
    float surgeGate = 0.5f + 0.5f*sinf((float)t*0.37f);          // slow on/off swell
    float surgeStr  = clampf(surgeGate*0.7f + beat*0.6f, 0.f, 1.2f);
    float surgeSig  = 46.f;                                       // band half-width

    // second, slower ripple for extra depth
    float surge2Y   = fmodf((float)t*70.f + surgeSpan*0.5f, surgeSpan) - 110.f;

    int NB = p->bandCount;

    // --- draw layers far -> near ---
    for(int li=0; li<(int)s->layers.size(); ++li){
        Layer& L = s->layers[li];
        int cw=L.cw, ch=L.ch;
        float layerBright = L.bright * globBright;

        for(int c=0;c<L.cols;c++){
            Col& r = L.col[c];
            int px = c * cw;

            // horizontal brightness gradient driven by spectrum bands (subtle)
            float grad = 1.f;
            if(NB>0){
                float gx = (float)px/(W>1?W-1:1);
                int bi = (int)(gx*(NB-1)); if(bi<0)bi=0; if(bi>=NB)bi=NB-1;
                grad = 0.72f + 0.62f*p->bands[bi];
            }

            // is this a "hot" column right now? new bright columns flare on beats.
            uint32_t hotSeed = hashu(r.seed ^ (uint32_t)((int)(t*1.7)) ^ (uint32_t)(li*7919));
            float hotRnd = (hotSeed & 0xffff)/65535.f;
            float hot = 0.f;
            if(hotRnd < (0.10f + 0.35f*beat + 0.25f*onset)) hot = 0.6f + 0.7f*beat;

            float travel = H + L.trail*ch;
            // two staggered streams per column for full-screen density
            for(int stream=0; stream<2; ++stream){
                float ph = r.phase + stream*0.5f;
                float headY = fmodf(ph*travel + (float)t*r.speed*speedMul, travel) - L.trail*ch;
                int headCell = (int)floorf(headY / ch);

                for(int q=0;q<r.len;q++){
                    int cellY = headCell - q;
                    int py = cellY * ch;
                    if(py < -ch || py >= H) continue;

                    // long fading trail
                    float fq = (float)q/r.len;
                    float fade = (1.f - fq);
                    fade *= fade;                       // long, smooth tail

                    // per-cell glyph, changes over time (flicker w/ treble)
                    float flickRate = 5.f + 9.f*tre;
                    uint32_t gs = hashu(r.seed ^ (uint32_t)(cellY*2246822519U)
                                        ^ (uint32_t)((t*flickRate + r.flick) + stream*31));

                    // surge boost by vertical position
                    float d1 = (py - surgeY)/surgeSig;
                    float d2 = (py - surge2Y)/(surgeSig*1.6f);
                    float surge = surgeStr*expf(-0.5f*d1*d1) + 0.35f*expf(-0.5f*d2*d2);

                    float rr,gg,bb,k;
                    if(q==0){
                        // bright white leading character + glow; flashes on onset
                        float lead = 1.6f + 1.1f*tre + 1.6f*onset + hot;
                        rr=0.75f; gg=1.0f; bb=0.80f;
                        k = lead * layerBright * grad * (1.f + 0.5f*surge);
                        cv->add_glow(cv, px+cw*0.5f, py+ch*0.5f,
                                     0.45f,1.0f,0.55f, (0.35f+0.5f*L.bright)*A, cw*0.6f);
                        drawGlyph(fb,W,H, px,py, cw,ch, gs, rr,gg,bb, k*A);
                    } else {
                        // green trail, occasional sparkle flicker w/ treble
                        float spark = (hashf(gs, 7) < 0.06f*tre) ? 1.8f : 1.f;
                        rr=0.10f; gg=1.0f; bb=0.28f;
                        k = (0.30f + 1.15f*fade + hot*0.5f) * layerBright * grad
                            * (0.9f + 0.9f*surge) * spark;
                        drawGlyph(fb,W,H, px,py, cw,ch, gs, rr,gg,bb, k*A);
                    }
                }
            }
        }
    }

    // --- faint static "code wall" backdrop behind everything (far grid) ---
    // dim green glyphs that flicker slowly; fills negative space so the screen
    // reads as a solid wall of code, never sparse.
    {
        int cw=8, ch=11;
        int gcols=W/cw, grows=H/ch;
        int tq = (int)(t*3.0);          // slow flicker quantum
        float wallK = (0.05f + 0.05f*rms) * (0.7f + 0.6f*tre);
        for(int gy=0; gy<grows; ++gy){
            for(int gx=0; gx<gcols; ++gx){
                uint32_t h = hashu((uint32_t)gx*734093U ^ (uint32_t)gy*29U ^ (uint32_t)(tq*97));
                if((h & 0xff) < 150){    // ~59% coverage -> dense wall
                    uint32_t gs = hashu(h ^ 0xBEEF);
                    drawGlyph(fb,W,H, gx*cw, gy*ch, cw, ch, gs, 0.0f,0.9f,0.22f, wallK*A);
                }
            }
        }
    }
}
