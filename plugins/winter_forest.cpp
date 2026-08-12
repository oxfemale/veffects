// winter_forest.cpp -- "Winter Forest" scene plugin.
// A serene snowy winter forest at night: a cold indigo gradient sky with a big
// glowing moon, shimmering aurora ribbons (green/teal/violet) drifting across the
// upper sky, a silhouetted row of snow-laden fir trees along the bottom, gentle
// falling snow filling the frame, a sparkling snowy foreground and distant stars.
//
// All animation is derived analytically from the absolute time p->time, so any
// frame renders correctly without simulation history (offline render + seeking).
// The player tone-maps the additive HDR buffer (bloom/shake/grain), so we lay a
// full sky base every frame. All output is scaled by p->alpha.

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
// OPAQUE composite (fb = fb*(1-a) + rgb*a), coverage scaled by scene alpha so at
// A=0 the buffer is untouched. Used for solid silhouettes so they read as dark
// shapes instead of blooming.
static inline void putOver(float* fb, int W, int H, int x, int y,
                           float r, float g, float b, float a){
    if ((unsigned)x >= (unsigned)W || (unsigned)y >= (unsigned)H) return;
    size_t i=((size_t)y*W+x)*3; float ia=1.f-a;
    fb[i]=fb[i]*ia + r*a; fb[i+1]=fb[i+1]*ia + g*a; fb[i+2]=fb[i+2]*ia + b*a;
}

struct Flake { float x0, y0; float speed; float drift; float size; float sway; };
struct Star  { float x, y; float base; float tw; };
struct Tree  { float x; float half; float h; float bx; }; // base-x, half-width, height, baseY

struct State {
    int W, H;
    int horizon;
    std::vector<Flake> flakes;
    std::vector<Star>  stars;
    std::vector<Tree>  trees;
};

static VfxPluginInfo INFO = {
    VFX_PLUGIN_ABI, "Winter Forest", "veffects",
    "A serene snowy winter forest at night with a glowing moon, shimmering aurora ribbons, snow-laden firs and gently falling snow, all reacting to the music."
};
VFX_EXPORT const VfxPluginInfo* vfx_plugin_info(void){ return &INFO; }

VFX_EXPORT void* vfx_plugin_create(int W, int H){
    State* s = new State();
    s->W=W; s->H=H;
    s->horizon = (int)(0.72f*H);   // where the snowy ground/tree line sits

    // gently falling snow: a few hundred flakes with varied speed/size/drift
    int NF = 340;
    s->flakes.reserve(NF);
    for(int i=0;i<NF;i++){
        Flake f;
        f.x0    = hashf(i,1)*W;
        f.y0    = hashf(i,2)*H;                 // vertical phase offset
        f.speed = 18.f + hashf(i,3)*46.f;       // px/sec fall
        f.drift = (hashf(i,4)-0.5f)*30.f;        // horizontal drift
        f.size  = 0.8f + hashf(i,5)*1.9f;        // radius-ish
        f.sway  = hashf(i,6)*6.2831f;
        s->flakes.push_back(f);
    }

    // a scatter of distant stars in the upper sky
    int NS = 70;
    s->stars.reserve(NS);
    for(int i=0;i<NS;i++){
        Star st;
        st.x    = hashf(i,11)*W;
        st.y    = hashf(i,12)*0.42f*H;          // upper sky only
        st.base = 0.15f + hashf(i,13)*0.5f;
        st.tw   = hashf(i,14)*6.2831f;
        s->stars.push_back(st);
    }

    // a silhouetted row of firs along the bottom, varied heights, overlapping
    int NT = 13;
    s->trees.reserve(NT);
    for(int i=0;i<NT;i++){
        Tree tr;
        float u = (i + 0.5f)/NT;
        tr.x    = u*W + (hashf(i,21)-0.5f)*(0.7f*W/NT);
        tr.half = 26.f + hashf(i,22)*20.f;
        tr.h    = 95.f + hashf(i,23)*85.f;
        tr.bx   = s->horizon + 0.14f*H + hashf(i,24)*0.06f*H; // base sits on snow
        s->trees.push_back(tr);
    }
    return s;
}
VFX_EXPORT void vfx_plugin_destroy(void* st){ delete (State*)st; }

// A layered aurora ribbon: a wavy horizontal band whose vertical position and
// brightness undulate analytically; color drifts green<->teal<->violet by hue.
static void drawAurora(const VfxCanvas* cv, float* fb, int W, int H,
                       double t, float baseY, float amp, float thick, float hue,
                       float energy, float phase, float A){
    if(energy <= 0.001f) return;
    for(int x=0;x<W;x++){
        float fx = (float)x;
        // wavy centerline (a couple of drifting sines)
        float wave = amp*sinf(fx*0.0075f + (float)t*0.35f + phase)
                   + amp*0.5f*sinf(fx*0.019f - (float)t*0.22f + phase*1.7f);
        float cy = baseY + wave;
        // along-ribbon brightness curtains that slowly drift sideways
        float curtain = 0.55f + 0.45f*sinf(fx*0.03f + (float)t*0.6f + phase*2.3f);
        curtain *= 0.6f + 0.4f*sinf(fx*0.011f - (float)t*0.27f);
        float colBright = energy * curtain;
        if(colBright < 0.01f) continue;
        float r,g,b; cv->hsv(hue, 0.82f, 1.0f, &r,&g,&b);
        int y0=(int)(cy-thick), y1=(int)(cy+thick);
        for(int y=y0;y<=y1;y++){
            float dv=(y-cy)/thick; if(dv<-1.f||dv>1.f) continue;
            // soft vertical falloff, fading upward like real curtains
            float fall = (1.f-dv*dv);
            float vfade = clampf(1.f - (dv*0.5f+0.5f)*0.65f, 0.f, 1.f);
            float k = colBright*fall*vfade*0.30f;
            putAdd(fb,W,H,x,y, r,g,b, k*A);
        }
    }
}

// A single snow-laden fir: dark triangular tiers with white snow caps on top.
static void drawFir(float* fb, int W, int H, const Tree& tr, float A, float snowLift){
    float half = tr.half, h = tr.h, bx = tr.x, by = tr.bx;
    // trunk
    float ty0 = by, ty1 = by - h*0.12f;
    for(float y=ty1;y<=ty0;y+=1.f)
        for(int ox=-2;ox<=2;ox++)
            putOver(fb,W,H,(int)(bx+ox),(int)y, 0.06f,0.07f,0.11f, 0.92f*A);

    int tiers = 3;
    float top = by - h;
    for(int ti=0; ti<tiers; ti++){
        float tu0 = (float)ti/tiers, tu1=(float)(ti+1)/tiers;
        float yTop = by - h*(1.f - tu0);
        float yBot = by - h*(1.f - tu1) + h*0.06f; // slight overlap downward
        float wTop = half*tu0*0.35f;
        float wBot = half*(0.35f + 0.65f*tu1);
        int iy0=(int)yTop, iy1=(int)yBot;
        for(int y=iy0;y<=iy1;y++){
            float f=(float)(y-iy0)/(float)(iy1-iy0+1);
            float w = wTop + (wBot-wTop)*f;
            int xa=(int)(bx-w), xb=(int)(bx+w);
            for(int x=xa;x<=xb;x++)
                putOver(fb,W,H,x,y, 0.05f,0.07f,0.12f, 0.94f*A);
        }
        // snow cap: a bright white sliver on top of each tier
        float capY = yTop + 1.f;
        float capBot = yTop + h*0.10f;
        for(float y=capY;y<=capBot;y++){
            float f=clampf((y-yTop)/(capBot-yTop+1.f),0.f,1.f);
            float w = (wTop + (wBot-wTop)*f)*0.9f;
            int xa=(int)(bx-w), xb=(int)(bx+w);
            float a = (0.85f - 0.55f*f)*snowLift;
            for(int x=xa;x<=xb;x++)
                putOver(fb,W,H,x,y, 0.90f,0.94f,1.0f, clampf(a,0.f,1.f)*A);
        }
    }
    (void)top;
}

VFX_EXPORT void vfx_plugin_render(void* st, const VfxCanvas* cv, const VfxParams* p){
    State* s=(State*)st;
    float* fb=cv->fb; int W=cv->width, H=cv->height;
    float A=p->alpha;
    float bass=p->bass, mid=p->mid, tre=p->treble, rms=p->rms, beat=p->beat, onset=p->onset;
    float centroid=p->centroid;
    double t=p->time;
    int hz=s->horizon;

    // ---- SKY: cold deep-blue/indigo gradient, darker up top -> lighter horizon
    for(int y=0;y<hz;y++){
        float v=(float)y/hz;                 // 0 top .. 1 horizon
        float r=0.020f + 0.075f*v;
        float g=0.045f + 0.110f*v;
        float b=0.120f + 0.230f*v;
        for(int x=0;x<W;x++) putAdd(fb,W,H,x,y, r,g,b, 0.9f*A);
    }

    // ---- STARS: distant twinkling points in the upper sky ----
    for(const Star& stx : s->stars){
        float tw = 0.5f + 0.5f*sinf((float)t*(1.2f) + stx.tw);
        float k = stx.base*(0.4f+0.6f*tw)*(0.7f+0.5f*tre);
        putAdd(fb,W,H,(int)stx.x,(int)stx.y, 0.85f,0.90f,1.0f, k*A);
        if(k>0.4f) cv->add_glow(cv, stx.x, stx.y, 0.8f,0.88f,1.0f, 0.10f*k*A, 2.2f);
    }

    // ---- MOON: big cold glowing disk, glow pulses gently with rms ----
    float mx=0.24f*W, my=0.20f*H;
    float moonP = 1.f + 0.35f*rms + 0.10f*beat;
    cv->add_glow(cv, mx,my, 0.75f,0.83f,1.0f, 0.30f*moonP*A, 150.f*moonP);
    cv->add_glow(cv, mx,my, 0.85f,0.90f,1.0f, 0.55f*moonP*A, 70.f);
    cv->add_glow(cv, mx,my, 0.95f,0.97f,1.0f, 0.90f*moonP*A, 34.f);
    // moon disk (opaque so it reads as a solid body)
    float mr=28.f;
    int my0=(int)(my-mr), my1=(int)(my+mr);
    for(int y=my0;y<=my1;y++){
        float dy=(y-my)/mr; if(dy<-1.f||dy>1.f) continue;
        float ext=mr*sqrtf(1.f-dy*dy);
        for(int x=(int)(mx-ext);x<=(int)(mx+ext);x++){
            float dx=(x-mx)/mr;
            // faint crater shading
            float sh = 0.94f - 0.08f*hashf((uint32_t)x*3u,(uint32_t)y*7u);
            putOver(fb,W,H,x,y, 0.93f*sh,0.95f*sh,1.0f*sh, clampf(0.97f*A,0.f,1.f));
            (void)dx;
        }
    }

    // ---- AURORA: layered wavy ribbons drifting across the upper sky ----
    // slow undulation driven by mid+bass; hue shifts green<->violet with centroid.
    float auroraEnergy = 0.85f + 1.7f*mid + 1.0f*bass;
    auroraEnergy *= (1.f + 0.5f*beat + 0.6f*onset);        // gust brightening
    // centroid (brightness of timbre) pushes green(0.33) toward violet(0.78)
    float hueBase = 0.33f + clampf(centroid,0.f,1.f)*0.42f;
    float bandC = p->bandCount;
    // three ribbons at different heights; per-ribbon intensity from spectrum bands
    for(int rib=0; rib<3; rib++){
        float baseY = (0.18f + rib*0.11f)*H + 8.f*sinf((float)t*0.18f + rib*1.3f);
        float amp   = 16.f + 10.f*rib + 22.f*mid;
        float thick = 20.f + 8.f*rib + 14.f*bass;
        float hue   = hueBase + (rib-1)*0.10f;             // teal/green/violet spread
        float bandK = 1.f;
        if(bandC>0){
            int bi = (int)((rib+0.5f)/3.f * (bandC-1));
            bandK = 0.5f + 1.3f*p->bands[bi];
        }
        float energy = auroraEnergy*bandK*(0.9f - 0.15f*rib);
        drawAurora(cv, fb, W, H, t, baseY, amp, thick, hue, energy, rib*2.1f, A);
    }

    // ---- SNOWY GROUND: cold pale-blue snowfield below the tree line ----
    for(int y=hz;y<H;y++){
        float u=(float)(y-hz)/(float)(H-hz);   // 0 tree line .. 1 bottom
        float r=0.30f + 0.34f*u;
        float g=0.36f + 0.36f*u;
        float b=0.52f + 0.34f*u;
        for(int x=0;x<W;x++) putAdd(fb,W,H,x,y, r,g,b, 0.9f*A);
    }
    // faint foreground sparkle on the snow (twinkles with treble)
    for(int i=0;i<70;i++){
        float gx=hashf(i,81)*W;
        float gy=hz + 0.10f*H + hashf(i,82)*0.90f*(H-hz);
        float tw=0.5f+0.5f*sinf((float)t*4.5f + i*1.7f);
        float k=(0.06f+0.32f*tre)*tw;
        if(k>0.05f) cv->add_glow(cv, gx,gy, 0.85f,0.92f,1.0f, k*A, 2.4f);
    }

    // ---- FIR TREES: snow-laden silhouettes along the tree line ----
    float snowLift = 0.85f + 0.20f*rms;
    for(const Tree& tr : s->trees)
        drawFir(fb, W, H, tr, A, snowLift);

    // ---- FALLING SNOW: gentle drifting flakes filling the frame ----
    // density/speed rise with treble; a gust sways flakes sideways on beat/onset.
    float speedMul = 1.f + 1.3f*tre;
    float gust = (beat*0.6f + onset*1.2f);                 // extra sideways push
    float gustDir = sinf((float)t*0.5f);
    for(const Flake& f : s->flakes){
        float fall = f.speed*speedMul;
        float y = fmodf(f.y0 + (float)t*fall, (float)(H+20)) - 10.f;
        // horizontal: base drift + swaying + gust
        float swayX = 10.f*sinf((float)t*0.9f + f.sway) + f.drift*0.4f*(float)t*0.0f;
        float x = f.x0 + f.drift*0.0f + swayX + gust*28.f*gustDir + 12.f*sinf(y*0.03f + f.sway);
        x = fmodf(x, (float)(W+20)); if(x<0) x+=W+20; x-=10.f;
        float k = (0.55f + 0.45f*tre) * (0.6f+0.5f*f.size);
        // near/large flakes as tiny soft glows, small ones as single bright px
        if(f.size>1.8f){
            cv->add_glow(cv, x,y, 0.92f,0.95f,1.0f, 0.22f*k*A, f.size*1.6f);
        } else {
            putAdd(fb,W,H,(int)x,(int)y, 0.92f,0.95f,1.0f, k*A);
            putAdd(fb,W,H,(int)x+1,(int)y, 0.85f,0.90f,1.0f, 0.5f*k*A);
        }
    }
}
