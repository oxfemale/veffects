// heaven.cpp -- "Heaven" scene plugin.
// A serene celestial paradise: a bright golden glow crowning the sky, volumetric
// god-rays streaming down through soft billowing clouds, a luminous pearly-gate
// arch and halo, rising glowing orbs (souls of light), drifting feathers and a
// shimmer of treble-driven sparkles. Everything bright, warm and dreamy.
//
// All animation is analytic from p->time, so any single frame renders correctly
// without simulation history (offline render and seeking both work). Every
// contribution is multiplied by p->alpha for scene crossfade.

#include "veffects_plugin.h"
#include <cmath>
#include <cstdint>
#include <vector>

static inline float clampf(float x, float a, float b){ return x<a?a:(x>b?b:x); }
static inline float smoothstep(float a, float b, float x){
    float t = clampf((x-a)/(b-a), 0.f, 1.f); return t*t*(3.f-2.f*t);
}
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
static inline void lineAdd(float* fb, int W, int H, float x0,float y0,float x1,float y1,
                           float r,float g,float b,float k){
    float dx=x1-x0, dy=y1-y0; float len=sqrtf(dx*dx+dy*dy);
    int n=(int)len+1; if(n<1)n=1;
    for(int i=0;i<=n;i++){ float t=(float)i/n; putAdd(fb,W,H,(int)(x0+dx*t),(int)(y0+dy*t),r,g,b,k); }
}

// Small persistent struct so vfx_plugin_create never returns NULL.
struct State { int W, H; };

static VfxPluginInfo INFO = {
    VFX_PLUGIN_ABI, "Heaven", "veffects",
    "A serene celestial paradise of golden god-rays, billowing clouds, a pearly-gate halo and rising glowing souls."
};
VFX_EXPORT const VfxPluginInfo* vfx_plugin_info(void){ return &INFO; }

VFX_EXPORT void* vfx_plugin_create(int W, int H){
    State* s = new State();
    s->W = W; s->H = H;
    return s;
}
VFX_EXPORT void vfx_plugin_destroy(void* st){ delete (State*)st; }

VFX_EXPORT void vfx_plugin_render(void* st, const VfxCanvas* cv, const VfxParams* p){
    State* s = (State*)st; (void)s;
    float* fb = cv->fb; int W = cv->width, H = cv->height;
    float A = p->alpha;
    float bass=p->bass, mid=p->mid, tre=p->treble, rms=p->rms, beat=p->beat;
    float onset=p->onset, centroid=clampf(p->centroid,0.f,1.f);
    double t = p->time;
    int NB = p->bandCount;

    // Light "temperature": low centroid -> warm gold, high -> cooler pearl.
    float warm = 1.f - 0.45f*centroid;               // 0.55..1
    // Warm gold light color for the rays/glow.
    float lr = 1.00f, lg = 0.86f + 0.10f*(1.f-warm), lb = 0.55f + 0.35f*(1.f-warm);

    // God-ray source: a radiant sun-glow drifting gently above the top edge.
    float sx = W*0.5f + 40.f*sinf((float)t*0.12f);
    float sy = -46.f + 10.f*sinf((float)t*0.19f);

    // Overall light swell (bass + loudness).
    float swell = 0.55f + 1.05f*bass + 0.75f*rms;
    // Cloud undulation phase driven by mid.
    float cph = (float)t*0.15f;
    float cspd = 0.10f + 0.25f*mid;

    // ---- Full-frame base pass: sky gradient + volumetric god-rays + clouds ----
    for(int y=0; y<H; y++){
        float ny = (float)y/H;                 // 0 top .. 1 bottom
        // Sky vertical gradient: warm golden cream near the glow -> pale sky-blue
        // below, with a gentle rose band through the middle.
        float topf = 1.f - ny;                 // brightness toward the heavens
        float rose = 0.10f*expf(-(ny-0.42f)*(ny-0.42f)*11.f); // soft pink midband
        float sky_r = 0.50f + 0.62f*topf + rose;
        float sky_g = 0.54f + 0.44f*topf + rose*0.5f;
        float sky_b = 0.78f + 0.10f*topf;      // blue strongest low, warm up high
        // gentle warm lift overall
        float skyBright = 0.58f + 0.20f*rms;
        size_t row = (size_t)y*W*3;
        for(int x=0; x<W; x++){
            float dx = x - sx, dy = y - sy;
            float dist = sqrtf(dx*dx + dy*dy) + 1e-3f;
            // angle of this pixel around the source (rays fan outward/down)
            float ang = atan2f(dx, dy);        // ~0 straight down
            // crisp radiating shafts
            float sh = 0.5f + 0.5f*sinf(ang*11.f + 0.6f*sinf((float)t*0.3f));
            sh = powf(sh, 2.2f);
            // a second finer set for richness
            float sh2 = 0.5f + 0.5f*sinf(ang*23.f - (float)t*0.2f);
            sh = sh*0.75f + powf(sh2,3.f)*0.25f;
            // individual shafts lit by spectrum bands
            if(NB>0){
                float u = (ang/6.2831853f)+0.5f; u -= floorf(u);
                int bi = (int)(u*NB); if(bi>=NB) bi=NB-1;
                sh *= 0.75f + 1.10f*p->bands[bi];
            }
            // rays are strongest near the source, softly fading downward
            float fall = expf(-dist*0.0055f);
            float ray = sh * fall * swell;

            // ---- soft billowing clouds (layered analytic noise) ----
            float fx = (float)x, fy = (float)y;
            float cd = 0.5f*(0.5f+0.5f*sinf(fx*0.011f + sinf(fy*0.028f + cph)*1.6f + cph))
                     + 0.3f*(0.5f+0.5f*sinf(fx*0.021f - cph*0.8f + fy*0.019f))
                     + 0.2f*(0.5f+0.5f*sinf(fx*0.047f + cspd*(float)t + sinf(fx*0.009f + fy*0.02f)*2.0f));
            // billow the density into puffy shapes; clouds sit in a soft band
            float band = smoothstep(0.18f, 0.55f, ny) * (1.f - 0.55f*smoothstep(0.80f,1.f,ny));
            float cloud = smoothstep(0.52f, 0.92f, cd) * band;
            // clouds catch the golden light from above (brighter tops), with a
            // warm creamy ambient so shadowed billows stay soft, not gray.
            float lit = 0.7f + 0.85f*(1.f - ny);

            float r = sky_r*skyBright + ray*lr*0.95f + cloud*lit*1.02f;
            float g = sky_g*skyBright + ray*lg*0.95f + cloud*lit*0.94f;
            float b = sky_b*skyBright + ray*lb*0.95f + cloud*lit*0.82f;

            size_t i = row + (size_t)x*3;
            fb[i]   += r*A;
            fb[i+1] += g*A;
            fb[i+2] += b*A;
        }
    }

    // ---- Radiant crowning glow (the light of heaven) ----
    float glowK = (2.6f + 3.2f*bass + 2.0f*rms);
    cv->add_glow(cv, sx, sy+20.f, lr, lg, lb, glowK*A, 190.f);
    cv->add_glow(cv, sx, sy+30.f, 1.0f, 0.97f, 0.9f, (1.8f+2.0f*rms)*A, 90.f);

    // ---- Pearly-gate arch + halo ring beneath the glow ----
    float acx = sx, acy = sy + 150.f;
    float aR  = 118.f + 8.f*sinf((float)t*0.4f) + 22.f*rms;
    int aseg = 60;
    for(int i=0;i<=aseg;i++){
        // arch spans an upper semicircle (a soft gateway)
        float u = (float)i/aseg;
        float an = 3.1415926f*(0.12f + 0.76f*u);   // left..right over the top
        float ca=cosf(an), sena=sinf(an);
        float px = acx + ca*aR, py = acy - sena*aR;
        float k = (0.22f + 0.25f*sinf(u*3.14159f)) * (0.9f + 0.6f*beat);
        putAdd(fb,W,H,(int)px,(int)py, 1.0f,0.95f,0.82f, k*A);
        putAdd(fb,W,H,(int)px,(int)py-1, 1.0f,0.92f,0.78f, k*0.6f*A);
        // soft glow gems along the gate
        if(i%12==0) cv->add_glow(cv, px, py, 1.0f,0.9f,0.7f, (0.7f+0.8f*beat)*A, 9.f);
    }
    // thin halo ring around the source
    float hR = 62.f + 6.f*sinf((float)t*0.7f);
    int hseg = 80;
    for(int i=0;i<hseg;i++){
        float an=(float)i/hseg*6.2831853f;
        float px=sx+cosf(an)*hR, py=(sy+34.f)+sinf(an)*hR*0.6f;
        putAdd(fb,W,H,(int)px,(int)py, 1.0f,0.9f,0.66f, (0.35f+0.5f*rms)*A);
    }

    // ---- Rising glowing orbs / souls of light ----
    int NORB = 16;
    float soulLift = 0.7f + 1.6f*beat + 2.2f*onset;   // souls brighten on beats
    for(int i=0;i<NORB;i++){
        float ph   = hashf(i,1);
        float spd  = 10.f + hashf(i,2)*20.f;          // px/s upward
        float period = H + 130.f;
        float baseX = W*(0.06f + 0.88f*hashf(i,3));
        float px = baseX + 30.f*sinf((float)t*(0.25f+0.5f*hashf(i,4)) + ph*6.28f);
        float py = (H+50.f) - fmodf((float)t*spd + ph*period, period);
        float lifeUp = 1.f - clampf((py)/(float)H, 0.f, 1.f); // brighter as it ascends
        float twk = 0.6f + 0.4f*sinf((float)t*(1.5f+2.f*hashf(i,5)) + ph*9.f);
        float k = (0.5f + 1.3f*lifeUp) * twk * (0.7f + 0.5f*soulLift);
        // warm pearly-pink core
        float orr=1.0f, org=0.82f+0.12f*hashf(i,6), orb=0.72f+0.18f*hashf(i,7);
        float rad = 6.f + 8.f*hashf(i,8) + 4.f*beat;
        cv->add_glow(cv, px, py, orr, org, orb, k*A, rad);
        cv->add_glow(cv, px, py, 1.0f, 0.98f, 0.95f, k*0.5f*A, rad*0.4f);
    }

    // ---- Drifting doves / feathers of light ----
    int NF = 7;
    for(int i=0;i<NF;i++){
        float ph = hashf(i,20);
        float driftT = fmodf((float)t*(0.03f+0.02f*hashf(i,21)) + ph, 1.f);
        float fx = W*(driftT*1.2f - 0.1f);            // drift left->right
        float fy = H*(0.18f + 0.6f*hashf(i,22)) + 22.f*sinf((float)t*(0.5f+hashf(i,23)) + ph*6.28f);
        float fl = 5.f + 4.f*sinf((float)t*2.2f + ph*6.28f); // gentle wing flap
        // simple dove: two soft wing strokes forming a shallow "v"
        float k = (0.5f + 0.4f*mid)*A;
        lineAdd(fb,W,H, fx-9.f, fy, fx, fy-fl, 1.0f,0.98f,0.9f, k);
        lineAdd(fb,W,H, fx+9.f, fy, fx, fy-fl, 1.0f,0.98f,0.9f, k);
        cv->add_glow(cv, fx, fy-fl*0.4f, 1.0f,0.97f,0.9f, 0.5f*A, 4.f);
    }

    // ---- Shimmering treble sparkles (twinkling motes of light) ----
    int NSPK = 240;
    float spkGain = 0.25f + 2.2f*tre + 0.6f*rms;
    for(int i=0;i<NSPK;i++){
        float bx = hashf(i,30)*W;
        float by = hashf(i,31)*H*0.92f;
        float tw = 0.5f + 0.5f*sinf((float)t*(3.f+6.f*hashf(i,32)) + hashf(i,33)*6.2831853f);
        tw = tw*tw*tw;
        float k = tw*spkGain*(0.4f+0.8f*hashf(i,34));
        if(k < 0.02f) continue;
        int ix=(int)bx, iy=(int)by;
        putAdd(fb,W,H, ix,   iy,   1.0f,0.98f,0.92f, k*A);
        putAdd(fb,W,H, ix-1, iy,   1.0f,0.95f,0.85f, k*0.5f*A);
        putAdd(fb,W,H, ix+1, iy,   1.0f,0.95f,0.85f, k*0.5f*A);
        putAdd(fb,W,H, ix,   iy-1, 1.0f,0.95f,0.85f, k*0.5f*A);
        putAdd(fb,W,H, ix,   iy+1, 1.0f,0.95f,0.85f, k*0.5f*A);
    }
}
