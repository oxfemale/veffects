// volcano.cpp -- "Volcano" scene plugin.
// An erupting volcano at night: a dark cone silhouette, a fountain of glowing
// molten lava blasting up from the crater and arcing back down, glowing lava
// rivers streaming down the slopes, a billowing red-lit ash plume, showers of
// buoyant embers, a warm eruption glow lighting the sky and ash, and occasional
// lightning flickers in the cloud.
//
// All animation is derived analytically from the absolute time p->time, so any
// frame renders correctly without simulation history (offline render + seeking).
// The additive HDR buffer is tone-mapped by the player (bloom/shake/grain); all
// output is scaled by p->alpha.

#include "veffects_plugin.h"
#include <cmath>
#include <cstdint>
#include <vector>

static inline float clampf(float x, float a, float b){ return x<a?a:(x>b?b:x); }
static inline float mixf(float a, float b, float t){ return a+(b-a)*t; }
static inline uint32_t hashu(uint32_t x){
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x;
}
static inline float hashf(uint32_t a, uint32_t b){
    return (hashu(a*0x9e3779b1U ^ b*0x85ebca77U) & 0xffffff) / 16777215.f;
}
// additive HDR write
static inline void putAdd(float* fb, int W, int H, int x, int y,
                          float r, float g, float b, float k){
    if ((unsigned)x >= (unsigned)W || (unsigned)y >= (unsigned)H) return;
    size_t i = ((size_t)y*W + x)*3;
    fb[i] += r*k; fb[i+1] += g*k; fb[i+2] += b*k;
}
// OPAQUE composite: fb = fb*(1-a) + rgb*a. Used to stamp the dark cone so the
// silhouette reads even where eruption glow overlaps it.
static inline void putOver(float* fb, int W, int H, int x, int y,
                           float r, float g, float b, float a){
    if ((unsigned)x >= (unsigned)W || (unsigned)y >= (unsigned)H) return;
    size_t i=((size_t)y*W+x)*3; float ia=1.f-a;
    fb[i]=fb[i]*ia + r*a; fb[i+1]=fb[i+1]*ia + g*a; fb[i+2]=fb[i+2]*ia + b*a;
}
static inline void lineAdd(float* fb, int W, int H, float x0,float y0,float x1,float y1,
                           float r,float g,float b,float k){
    float dx=x1-x0, dy=y1-y0; float len=sqrtf(dx*dx+dy*dy);
    int n=(int)len+1; if(n<1)n=1;
    for(int i=0;i<=n;i++){ float t=(float)i/n; putAdd(fb,W,H,(int)(x0+dx*t),(int)(y0+dy*t),r,g,b,k); }
}
// map a 0..1 "heat" to a molten color (deep red -> orange -> yellow-white)
static inline void heatColor(float h, float& r, float& g, float& b){
    h = clampf(h,0.f,1.f);
    r = mixf(0.90f, 1.00f, clampf(h*1.4f,0.f,1.f));
    g = mixf(0.06f, 0.95f, h*h);
    b = mixf(0.01f, 0.70f, clampf((h-0.55f)/0.45f,0.f,1.f)*clampf((h-0.55f)/0.45f,0.f,1.f));
}

struct Blob { float phase, vx, vy, hue; };
struct State {
    int W, H;
    std::vector<Blob> lava;   // fountain droplets
    std::vector<Blob> ember;  // buoyant sparks
};

VFX_EXPORT const VfxPluginInfo* vfx_plugin_info(void){
    static VfxPluginInfo INFO = {
        VFX_PLUGIN_ABI, "Volcano", "veffects",
        "A nighttime volcano erupting a molten lava fountain, lava rivers, a red-lit ash plume, embers and lightning, all driven by the audio."
    };
    return &INFO;
}

VFX_EXPORT void* vfx_plugin_create(int W, int H){
    State* s = new State();
    s->W = W; s->H = H;
    const int NL = 300;
    s->lava.resize(NL);
    for(int i=0;i<NL;i++){
        Blob& b = s->lava[i];
        b.phase = hashf(i,11);
        b.vx    = (hashf(i,12)-0.5f)*2.f;      // lateral spread factor [-1..1]
        b.vy    = 0.55f + 0.45f*hashf(i,13);   // launch-speed factor
        b.hue   = hashf(i,14);
    }
    const int NE = 240;
    s->ember.resize(NE);
    for(int i=0;i<NE;i++){
        Blob& e = s->ember[i];
        e.phase = hashf(i+777,21);
        e.vx    = (hashf(i+777,22)-0.5f)*2.f;
        e.vy    = 0.5f + 0.7f*hashf(i+777,23);
        e.hue   = hashf(i+777,24);
    }
    return s; // never NULL
}
VFX_EXPORT void vfx_plugin_destroy(void* st){ delete (State*)st; }

VFX_EXPORT void vfx_plugin_render(void* st, const VfxCanvas* cv, const VfxParams* p){
    State* s = (State*)st;
    float* fb = cv->fb; int W = cv->width, H = cv->height;
    float A = p->alpha; if(A<=0.f) return;
    double t = p->time;
    float bass=p->bass, mid=p->mid, tre=p->treble, rms=p->rms, beat=p->beat, onset=p->onset;

    // ---- geometry of the cone --------------------------------------------
    const float cx = W*0.5f;
    const float baseY   = H - 6.f;
    const float summitY = H*0.40f;
    const float craterHalf = W*0.058f;
    const float craterDepth= 14.f;
    const float leftBaseX  = W*0.06f;
    const float rightBaseX = W*0.94f;
    const float rimX = craterHalf;             // half-width of crater mouth
    const float craterFloorY = summitY + craterDepth;
    auto topY = [&](float x)->float{
        float dx = x - cx, adx = fabsf(dx);
        if(adx < craterHalf){                  // crater dip
            float u = dx/craterHalf;
            return summitY + craterDepth*(1.f - u*u);
        }
        // slopes: rim -> base, gently concave, with a little rocky roughness
        float t01, edgeY;
        if(dx < 0){ t01 = (x - (cx-craterHalf)) / (leftBaseX - (cx-craterHalf)); }
        else      { t01 = (x - (cx+craterHalf)) / (rightBaseX - (cx+craterHalf)); }
        t01 = clampf(t01,0.f,1.f);
        edgeY = mixf(summitY, baseY, powf(t01,0.86f));
        float rough = (hashf((uint32_t)(x*0.5f),7)-0.5f)*6.f*(0.3f+0.7f*t01);
        edgeY += rough;
        if(x < leftBaseX || x > rightBaseX) edgeY = baseY; // flat ground beyond base
        return edgeY;
    };

    // ---- audio-driven intensities ----------------------------------------
    float heat   = 0.28f + 0.7f*rms + 0.4f*bass;           // overall eruption glow
    float erupt  = 1.f + 1.3f*beat + 1.1f*onset;           // fountain height / power
    float billow = 0.5f + 1.1f*mid;                        // smoke billow
    float sparkle= 0.4f + 1.2f*tre;                        // ember / lightning sparkle

    // ---- 1) sky: dark night + radial eruption glow (excludes the cone) ---
    const float GR = 120.f, GR2 = GR*GR;
    float gTop = summitY - 20.f;                            // glow centered above crater
    for(int x=0;x<W;x++){
        int ty = (int)topY((float)x);
        if(ty > H) ty = H;
        float dxg = (float)x - cx;
        for(int y=0;y<ty;y++){
            float v = (float)y/H;                           // 0 top .. 1 down
            // near-black night base, faintly cool up high, faintly warm low down
            float sr = 0.006f + 0.014f*v;
            float sg = 0.006f + 0.009f*v;
            float sb = 0.016f + 0.009f*v;
            // radial eruption glow with a steep tail so night stays dark
            float dyg = (float)y - gTop;
            float d2 = dxg*dxg + dyg*dyg;
            float q  = d2/GR2;
            float gl = heat / (1.f + q + 0.55f*q*q);
            gl *= (dyg>0 ? 1.f : 1.f+0.35f*clampf(-dyg/GR,0.f,1.5f)); // reach up a bit
            sr += gl*1.00f; sg += gl*0.32f; sb += gl*0.08f;
            putAdd(fb,W,H,x,y, sr,sg,sb, A);
        }
    }

    // ---- 2) dark cone silhouette (opaque) with a faint ridge rim-light ----
    for(int x=(int)leftBaseX; x<=(int)rightBaseX; x++){
        int ty = (int)topY((float)x);
        // near-black volcanic rock, marginally lit toward the summit
        float lit = clampf((summitY+120.f - ty)/120.f, 0.f, 1.f);
        float rr = 0.012f + 0.020f*lit*heat;
        float gg = 0.006f + 0.006f*lit*heat;
        float bb = 0.010f;
        for(int y=ty; y<H; y++) putOver(fb,W,H,x,y, rr,gg,bb, 1.0f);
        // rim-light at the silhouette edge
        putAdd(fb,W,H,x,ty, 0.9f,0.28f,0.06f, 0.25f*heat*A);
    }

    // hot crater-mouth glow pulsing with bass/beat
    cv->add_glow(cv, cx, craterFloorY-2.f, 1.0f,0.45f,0.12f,
                 (0.8f + 1.6f*bass + 1.4f*beat)*A, 46.f);
    cv->add_glow(cv, cx, craterFloorY,     1.0f,0.75f,0.35f,
                 (0.5f + 1.0f*beat)*A, 20.f);

    // ---- 3) lava fountain (parabolic, analytic) + band-driven jets --------
    const float LIFE = 1.55f;
    const float G    = 560.f;                               // px/s^2
    int NB = p->bandCount;
    for(size_t i=0;i<s->lava.size();i++){
        Blob& b = s->lava[i];
        // multiple jets: distribute droplets across a few band-driven nozzles
        int jet = (NB>0) ? (int)(i % 5) : 0;
        float jetX = cx + ((jet-2)*0.36f)*craterHalf;
        float jetGain = 1.f;
        if(NB>0){ int bi = (jet*NB)/5; if(bi>=NB) bi=NB-1; jetGain = 0.55f + 1.4f*p->bands[bi]; }

        float age = fmodf((float)t + b.phase*LIFE, LIFE);
        float vy0 = (150.f + 235.f*b.vy) * erupt * (0.7f+0.6f*jetGain); // upward speed
        float up  = vy0*age - 0.5f*G*age*age;               // parabola (up positive)
        if(up < -30.f) continue;                            // fell well below crater
        float x = jetX + b.vx*(28.f+55.f*b.vy)*age;         // lateral drift
        float y = craterFloorY - up;
        float vy = vy0 - G*age;                             // current vertical vel
        float hot = clampf(0.35f + 0.55f*(vy/vy0) + 0.25f*b.hue, 0.f, 1.f);
        float life01 = age/LIFE;
        float fade = (1.f - life01*life01);                 // dim as it cools/falls
        float k = (0.9f + 1.8f*heat) * fade * jetGain;
        float rr,gg,bb; heatColor(hot, rr,gg,bb);
        float rad = 1.6f + 2.6f*hot + 1.2f*(1.f-life01);
        cv->add_glow(cv, x, y, rr,gg,bb, k*A*0.9f, rad);
    }

    // ---- 4) lava rivers streaming down the slopes ------------------------
    for(int side=0; side<2; side++){
        float sgn = side? 1.f : -1.f;
        for(int ch=0; ch<2; ch++){                          // 2 channels per side
            float ax = cx + sgn*craterHalf*0.6f;
            float ay = craterFloorY + 6.f;
            float bx = mixf(cx, (side?rightBaseX:leftBaseX), 0.72f) + sgn*(ch*26.f);
            float by = baseY - 10.f;
            int NR = 80;
            for(int i=0;i<NR;i++){
                float base_u = (float)i/NR;
                float u = fmodf(base_u + (float)t*(0.13f+0.03f*ch) + ch*0.5f, 1.f);
                float mx = sinf(u*9.4f + ch*2.f + side*1.3f) * 10.f*(1.f-u); // meander
                float x = mixf(ax,bx,u) + mx;
                float y = mixf(ay,by,u);
                float cool = 1.f - u;                        // hotter near the crater
                float rr,gg,bb; heatColor(0.12f + 0.7f*cool, rr,gg,bb);
                float k = (0.30f + 1.1f*heat) * (0.30f+0.7f*cool);
                cv->add_glow(cv, x, y, rr,gg,bb, k*A, 1.8f + 1.8f*cool);
            }
        }
    }

    // ---- 5) billowing ash / smoke plume, lit red on its underside --------
    {
        int NA = 66;
        for(int i=0;i<NA;i++){
            float ph = hashf(i+40,31);
            float ALIFE = 5.5f;
            float age = fmodf((float)t*(0.85f+0.3f*hashf(i,32)) + ph*ALIFE, ALIFE);
            float a01 = age/ALIFE;
            float rise = 42.f*age*(0.9f+0.3f*billow);        // rise speed
            float y = craterFloorY - rise;
            float spread = (28.f + 150.f*a01) * (0.5f+0.5f*billow);
            float sway = sinf(age*1.3f + i*1.7f) * 22.f*a01;
            float x = cx + (hashf(i,33)-0.5f)*2.f*spread*0.6f + sway;
            float rad = 22.f + 62.f*a01;
            // dark warm-grey, brightest (red-lit) low near the crater, dim grey up high
            float underlit = clampf(1.2f - 2.1f*a01, 0.f, 1.f);
            float dens = (0.05f + 0.09f*billow) * (1.f - 0.45f*a01);
            float rr = dens*(0.75f + 1.9f*underlit);
            float gg = dens*(0.62f + 0.55f*underlit);
            float bb = dens*(0.62f + 0.08f*underlit);
            cv->add_glow(cv, x, y, rr,gg,bb, A, rad);
        }
    }

    // ---- 6) buoyant embers drifting up and fading ------------------------
    const float ELIFE = 2.6f;
    for(size_t i=0;i<s->ember.size();i++){
        Blob& e = s->ember[i];
        float age = fmodf((float)t*(0.8f+0.5f*e.vy) + e.phase*ELIFE, ELIFE);
        float a01 = age/ELIFE;
        float rise = (55.f + 40.f*e.vy)*age;                 // rise, roughly linear
        float y = craterFloorY - rise;
        float x = cx + e.vx*(20.f + 70.f*a01) + sinf(age*3.1f + i)*8.f;
        float flick = 0.55f + 0.45f*sinf((float)t*22.f + i*2.3f + e.hue*6.28f);
        float k = (0.35f + 1.1f*sparkle) * (1.f - a01) * flick;
        float rr,gg,bb; heatColor(0.45f + 0.4f*e.hue, rr,gg,bb);
        cv->add_glow(cv, x, y, rr,gg,bb, k*A*0.8f, 1.2f + 1.0f*flick);
    }

    // ---- 7) occasional lightning flicker inside the ash cloud ------------
    {
        float rate = 2.6f;
        int seg = (int)floorf((float)t*rate);
        float frac = (float)t*rate - seg;
        float h = hashf((uint32_t)seg, 91);
        float thresh = 0.12f + 0.55f*tre;
        if(h < thresh){
            float env = expf(-frac*6.5f);                    // quick decay flash
            float k = env * (0.4f + 1.1f*sparkle) * A;
            if(k > 0.02f){
                float bx = cx + (hashf((uint32_t)seg,92)-0.5f)*180.f;
                float by = summitY - 30.f - hashf((uint32_t)seg,93)*90.f;
                float x = bx, y = by;
                int segs = 7;
                for(int j=0;j<segs;j++){
                    float nx = x + (hashf((uint32_t)seg, 100+j)-0.5f)*40.f;
                    float ny = y + 16.f + hashf((uint32_t)seg, 200+j)*14.f;
                    lineAdd(fb,W,H, x,y,nx,ny, 0.8f,0.85f,1.0f, k);
                    lineAdd(fb,W,H, x,y,nx,ny, 0.5f,0.6f,1.0f, k*0.5f); // halo
                    x=nx; y=ny;
                }
                // brief overall sky bloom from the flash
                cv->add_glow(cv, bx, by+20.f, 0.6f,0.65f,0.95f, k*0.6f, 120.f);
            }
        }
    }
}
