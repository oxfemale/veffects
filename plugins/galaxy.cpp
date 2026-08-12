// galaxy.cpp -- "Galaxy" scene plugin.
// A tilted spiral galaxy: thousands of stars swept into glowing spiral arms
// around a bright warm galactic core, slowly rotating, threaded with magenta/teal
// nebula dust lanes and set against a scattered background starfield.
//
// All animation is derived from the absolute time p->time (analytic), so any
// frame renders correctly without simulation history (offline render + seek work).

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
static inline void putAdd(float* fb, int W, int H, int x, int y,
                          float r, float g, float b, float k){
    if ((unsigned)x >= (unsigned)W || (unsigned)y >= (unsigned)H) return;
    size_t i = ((size_t)y*W + x)*3;
    fb[i] += r*k; fb[i+1] += g*k; fb[i+2] += b*k;
}

// One disk star, precomputed in create(). Position is analytic from (r,theta0)+t.
struct Star {
    float r;        // normalized galactocentric radius [0..1]
    float theta0;   // base angular position within its arm
    float warm;     // 1 = warm bulge colour .. 0 = blue-white arm colour
    float bright;   // base brightness
    float size;     // glow radius hint
    float twSpd;    // twinkle speed
    float twPh;     // twinkle phase
    uint32_t seed;
};

// Nebula puff (dust lane / gas cloud) traced along the arms.
struct Neb {
    float r, theta0, size;
    float teal;     // 0 = magenta .. 1 = teal
    float k;
};

// Background field star (fixed screen position).
struct BgStar { float x, y, k, spd, ph; float warm; };

struct State {
    int W, H;
    std::vector<Star>   stars;
    std::vector<Neb>    nebs;
    std::vector<BgStar> bg;
};

static const int   NARMS   = 2;        // grand-design two-armed spiral
static const float TWIST   = 4.6f;     // radians of winding across the disk
static const float ARM_W   = 0.52f;    // angular half-spread of an arm

VFX_EXPORT const VfxPluginInfo* vfx_plugin_info(void);

static VfxPluginInfo INFO = {
    VFX_PLUGIN_ABI, "Galaxy", "veffects",
    "A tilted, slowly rotating spiral galaxy of thousands of stars in glowing arms around a warm pulsing core, with magenta/teal nebula and a background starfield."
};
VFX_EXPORT const VfxPluginInfo* vfx_plugin_info(void){ return &INFO; }

VFX_EXPORT void* vfx_plugin_create(int W, int H){
    State* s = new State();
    s->W = W; s->H = H;

    // ---- disk stars: sweep them into spiral arms ---------------------------
    const int N = 14000;
    s->stars.reserve(N);
    for(int i=0;i<N;i++){
        uint32_t sd = hashu((uint32_t)i*2654435761U + 101U);
        float u  = hashf(sd,1);
        // radius: concentrated toward the centre, tail reaching the rim
        float r  = powf(u, 0.62f);
        float th, warm, bright;
        float armPick = hashf(sd,2);
        if(armPick < 0.72f){
            // ARM star: cling to one of the spiral arms
            int a = (int)(hashf(sd,3)*NARMS) % NARMS;
            float jit = (hashf(sd,4)-0.5f) + (hashf(sd,5)-0.5f); // ~triangular
            th = a*(6.2831853f/NARMS) + jit*ARM_W;
            bright = 0.55f + 0.55f*hashf(sd,6);
        } else {
            // inter-arm / field star: dimmer, spread anywhere
            th = hashf(sd,7)*6.2831853f;
            bright = 0.20f + 0.30f*hashf(sd,8);
        }
        // colour: warm gold/red bulge in the centre -> blue-white in the arms
        warm = clampf(1.20f - r*2.3f, 0.f, 1.f);
        warm = warm*warm;                                   // tighten the bulge
        // arms host bright young blue stars; give a few extra sparkle
        if(hashf(sd,9) > 0.93f) bright *= 1.9f;
        float size = 0.9f + hashf(sd,10)*1.1f + (1.f-r)*0.6f;

        Star st;
        st.r=r; st.theta0=th; st.warm=warm; st.bright=bright; st.size=size;
        st.twSpd = 1.2f + hashf(sd,11)*5.0f;
        st.twPh  = hashf(sd,12)*6.2831853f;
        st.seed  = sd;
        s->stars.push_back(st);
    }

    // ---- nebula puffs traced along the arms (dust lanes + gas) --------------
    const int NN = 30;
    s->nebs.reserve(NN);
    for(int i=0;i<NN;i++){
        uint32_t sd = hashu((uint32_t)i*40503U + 7U);
        int a = i % NARMS;
        float r = 0.24f + hashf(sd,1)*0.66f;
        float jit = (hashf(sd,2)-0.5f)*ARM_W*1.4f;
        Neb nb;
        nb.r = r;
        nb.theta0 = a*(6.2831853f/NARMS) + jit;
        nb.size = 18.f + hashf(sd,3)*26.f;
        nb.teal = hashf(sd,4);
        nb.k = 0.05f + 0.06f*hashf(sd,5);
        s->nebs.push_back(nb);
    }

    // ---- background starfield (fixed screen positions) ---------------------
    const int NBG = 520;
    s->bg.reserve(NBG);
    for(int i=0;i<NBG;i++){
        uint32_t sd = hashu((uint32_t)i*911U + 333U);
        BgStar b;
        b.x = hashf(sd,1)*W;
        b.y = hashf(sd,2)*H;
        b.k = 0.05f + 0.35f*powf(hashf(sd,3),3.f);
        b.spd = 1.0f + hashf(sd,4)*5.f;
        b.ph = hashf(sd,5)*6.2831853f;
        b.warm = hashf(sd,6);
        s->bg.push_back(b);
    }

    return s; // never NULL
}
VFX_EXPORT void vfx_plugin_destroy(void* st){ delete (State*)st; }

VFX_EXPORT void vfx_plugin_render(void* st, const VfxCanvas* cv, const VfxParams* p){
    State* s = (State*)st;
    float* fb = cv->fb; int W = cv->width, H = cv->height;
    float A = p->alpha;
    float bass=p->bass, mid=p->mid, tre=p->treble, rms=p->rms;
    float beat=p->beat, onset=p->onset, cen=clampf(p->centroid,0.f,1.f);
    double t = p->time;
    int NB = p->bandCount;

    float cx = W*0.5f, cy = H*0.5f;
    float Rmax = 0.42f*H*1.28f;            // disk radius in px (~205)

    // ---- viewing geometry: inclined disk (tilt) + position angle -----------
    const float cosI = 0.50f, sinI = 0.866f;   // ~60deg inclination
    const float PA   = -0.42f;                  // major-axis position angle
    float cPA = cosf(PA), sPA = sinf(PA);

    // ---- galaxy rotation: slow, rides rms subtly (analytic in t) -----------
    float omega = 0.070f + 0.060f*rms;
    float rot   = (float)t*omega;

    // ---- 0) deep-space background: black -> indigo vignette ----------------
    for(int y=0;y<H;y+=2){
        float ny = (y-cy)/(0.5f*H);
        for(int x=0;x<W;x+=2){
            float nx = (x-cx)/(0.5f*W);
            float d2 = nx*nx + ny*ny;
            float m = 0.030f*(1.f - clampf(d2*0.7f,0.f,0.95f)); // brighter toward centre
            putAdd(fb,W,H,x,y, 0.020f,0.028f,0.075f, m*A);
        }
    }

    // ---- 1) background starfield ------------------------------------------
    for(const BgStar& b : s->bg){
        float tw = 0.55f + 0.45f*sinf((float)t*b.spd + b.ph);
        float k = b.k*(0.7f + 0.8f*tw)*(0.7f + 0.6f*tre);
        float r = mixf(0.75f,1.0f,b.warm), g = mixf(0.85f,0.95f,b.warm), bb = 1.0f;
        putAdd(fb,W,H,(int)b.x,(int)b.y, r,g,bb, k*A);
    }

    // helper: project a disk point (r,theta) -> screen (sx,sy), depth sign ---
    auto project = [&](float r, float theta, float& sx, float& sy, float& depth){
        float R = r*Rmax;
        float dx = R*cosf(theta), dy = R*sinf(theta);
        depth = dy*sinI;                      // >0 = near side
        float ex = dx, ey = dy*cosI;          // squash into the tilt
        sx = cx + ex*cPA - ey*sPA;
        sy = cy + ex*sPA + ey*cPA;
    };

    // ---- 2) nebula dust lanes / gas clouds (behind the stars) --------------
    for(const Neb& nb : s->nebs){
        float theta = nb.theta0 + nb.r*TWIST + rot;
        float sx,sy,depth; project(nb.r, theta, sx,sy,depth);
        // centroid shifts the nebula hue between magenta and teal
        float teal = clampf(nb.teal + (cen-0.5f)*0.7f, 0.f, 1.f);
        float r = mixf(1.00f, 0.10f, teal);
        float g = mixf(0.18f, 0.85f, teal);
        float b = mixf(0.80f, 0.95f, teal);
        float pulse = 0.7f + 0.9f*mid + 0.5f*rms;
        cv->add_glow(cv, sx, sy, r,g,b, nb.k*pulse*A, nb.size);
    }

    // ---- 3) the disk stars in their spiral arms ----------------------------
    float armGain = 0.85f + 0.7f*rms;
    for(const Star& st : s->stars){
        float theta = st.theta0 + st.r*TWIST + rot;   // spiral winding + rotation
        float sx,sy,depth; project(st.r, theta, sx,sy,depth);
        if((unsigned)(int)sx >= (unsigned)W || (unsigned)(int)sy >= (unsigned)H) continue;

        // spectrum drives brightness of successive arm radii
        float bandE = 0.f;
        if(NB>0){ int bi = (int)(st.r*(NB-1)); bandE = p->bands[bi]; }
        float radPulse = 0.60f + 1.15f*bandE;

        float tw   = 0.72f + 0.28f*sinf((float)t*st.twSpd + st.twPh);
        float front= 0.72f + 0.28f*clampf(depth/Rmax + 0.5f, 0.f, 1.f);
        float k = st.bright*radPulse*tw*front*armGain;

        // colour: warm gold/red bulge -> blue-white arms
        float r = mixf(0.62f, 1.00f, st.warm);
        float g = mixf(0.74f, 0.74f, st.warm);
        float b = mixf(1.00f, 0.42f, st.warm);

        putAdd(fb,W,H,(int)sx,(int)sy, r,g,b, k*0.9f*A);
        if(k > 0.85f)                                  // brightest stars bloom
            cv->add_glow(cv, sx,sy, r,g,b, (k-0.6f)*0.28f*A, st.size);
    }

    // ---- 4) supernova sparkles: flash on onset / treble --------------------
    float sparkle = clampf(onset*1.2f + tre*0.5f, 0.f, 1.f);
    if(sparkle > 0.30f){
        int nfl = 1 + (int)(sparkle*4.99f);
        int bucket = (int)(t*7.0);
        for(int q=0;q<nfl;q++){
            uint32_t hs = hashu((uint32_t)bucket*2654435761U + q*40503U);
            int idx = (int)(hashf(hs,1)*(s->stars.size()-1));
            const Star& st = s->stars[idx];
            float theta = st.theta0 + st.r*TWIST + rot;
            float sx,sy,depth; project(st.r, theta, sx,sy,depth);
            float kk = (0.5f + 1.4f*sparkle)*A;
            cv->add_glow(cv, sx,sy, 1.0f,0.95f,0.85f, kk, 3.5f);
            // little cross of rays
            for(int a=0;a<4;a++){
                float an = a*1.5708f;
                float dx=cosf(an), dy=sinf(an);
                for(int r=1;r<=4;r++)
                    putAdd(fb,W,H,(int)(sx+dx*r),(int)(sy+dy*r),
                           1.0f,0.95f,0.9f, kk*(1.f-r*0.2f)*0.5f);
            }
        }
    }

    // ---- 5) bright warm galactic CORE: pulses with bass + beat -------------
    float coreBoost = 0.55f + 1.7f*bass + 1.3f*beat;
    float coreR = 26.f + 10.f*coreBoost;
    // layered warm glows (round bulge, not squashed)
    cv->add_glow(cv, cx,cy, 1.0f,0.62f,0.30f, 0.22f*coreBoost*A, coreR*2.4f);
    cv->add_glow(cv, cx,cy, 1.0f,0.78f,0.42f, 0.35f*coreBoost*A, coreR*1.3f);
    cv->add_glow(cv, cx,cy, 1.0f,0.92f,0.70f, 0.55f*coreBoost*A, coreR*0.6f);
    cv->add_glow(cv, cx,cy, 1.0f,1.00f,0.92f, 0.75f*coreBoost*A, coreR*0.28f);

    // warm inner-bulge haze so the core reads as a dense stellar swarm
    {
        int NBULGE = 900;
        for(int i=0;i<NBULGE;i++){
            uint32_t sd = hashu((uint32_t)i*2246822519U + 55U);
            float rr = powf(hashf(sd,1),1.8f)*0.16f;      // tight to centre
            float th = hashf(sd,2)*6.2831853f + rot*0.6f; // bulge rotates gently
            float sx,sy,depth; project(rr, th, sx,sy,depth);
            float tw = 0.7f+0.3f*sinf((float)t*(2.f+hashf(sd,3)*4.f)+hashf(sd,4)*6.28f);
            float k = (0.35f + 0.5f*(1.f-rr/0.16f))*tw*(0.6f+0.8f*bass);
            putAdd(fb,W,H,(int)sx,(int)sy, 1.0f,0.80f,0.48f, k*0.5f*A);
        }
    }
}
