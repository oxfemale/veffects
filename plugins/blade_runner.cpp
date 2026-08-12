// blade_runner.cpp -- "Blade Runner" scene plugin.
// A rainy neon dystopian megacity at night in the Blade Runner 2049 palette:
// deep teal/blue smog, hot amber and magenta neon billboards floating in the
// haze, silhouetted black towers with lit windows, slow volumetric searchlight
// beams sweeping the sky, flying-car light streaks, and heavy diagonal rain.
//
// All animation is derived from the absolute time p->time (analytic, no
// cross-frame state), so offline render and seeking both reproduce every frame.
// Every contribution is scaled by p->alpha for scene crossfade. The player owns
// tone-mapping, bloom, chromatic aberration and grain, so this only writes into
// the additive HDR frame buffer.

#include "veffects_plugin.h"
#include <cmath>
#include <cstdint>
#include <vector>

static inline float clampf(float x, float a, float b){ return x<a?a:(x>b?b:x); }
static inline float smooth01(float x){ x=clampf(x,0.f,1.f); return x*x*(3.f-2.f*x); }
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
// filled rectangle border (thin) -- for billboard frames
static inline void rectFrame(float* fb, int W, int H, int x0,int y0,int x1,int y1,
                             float r,float g,float b,float k){
    for(int x=x0;x<=x1;x++){ putAdd(fb,W,H,x,y0,r,g,b,k); putAdd(fb,W,H,x,y1,r,g,b,k); }
    for(int y=y0;y<=y1;y++){ putAdd(fb,W,H,x0,y,r,g,b,k); putAdd(fb,W,H,x1,y,r,g,b,k); }
}

struct Building {
    int   x0, x1;      // horizontal footprint
    int   top;         // silhouette top (smaller = taller)
    uint32_t seed;
    int   antenna;     // 0/1 has an antenna spire
};
struct Billboard {
    float bx, by;      // center (by is a base position; drifts with time)
    float bw, bh;      // half extents
    float drift;       // vertical drift amplitude
    float phase;       // animation phase
    int   band;        // spectrum band that drives this panel
    int   magenta;     // 0 amber-ish, 1 magenta/pink accent
    uint32_t seed;
};
struct Beam {
    float ox, oy;      // origin
    float base;        // base angle
    float span;        // sweep half-span
    float rate;        // sweep rate
    int   magenta;
};
struct State {
    int W, H, horizon;
    std::vector<Building>  bld;
    std::vector<int>       topArr;   // per-column silhouette top
    std::vector<Billboard> boards;
    std::vector<Beam>      beams;
};

// draw a soft filled glowing panel (billboard body) with additive falloff to edges
static void panelFill(float* fb,int W,int H,int cx,int cy,int hw,int hh,
                      float r,float g,float b,float k){
    for(int y=-hh;y<=hh;y++){
        float fy=1.f-fabsf((float)y/hh); // vertical falloff
        int py=cy+y;
        for(int x=-hw;x<=hw;x++){
            float fx=1.f-fabsf((float)x/hw);
            float w=fx*fy; w*=w;
            putAdd(fb,W,H,cx+x,py,r,g,b,k*w);
        }
    }
}

// procedural "kanji/glyph" strokes inside a panel
static void glyphStrokes(float* fb,int W,int H,int cx,int cy,int hw,int hh,
                         uint32_t seed,float r,float g,float b,float k){
    int x0=cx-hw+3, x1=cx+hw-3, y0=cy-hh+3, y1=cy+hh-3;
    if(x1<=x0||y1<=y0) return;
    int nglyph = 1 + (int)(hashf(seed,1)*3.0f);
    int gw = (x1-x0)/(nglyph);
    if(gw<4) return;
    for(int gi=0; gi<nglyph; gi++){
        int gx0=x0+gi*gw+1, gx1=gx0+gw-3;
        uint32_t gs=hashu(seed ^ (uint32_t)(gi*2246822519U));
        int nh = 2 + (int)(hashf(gs,2)*3.f);
        for(int s=0;s<nh;s++){
            int ry=y0+(int)(hashf(gs,10+s)*(y1-y0));
            int xa=gx0+(int)(hashf(gs,20+s)*(gx1-gx0)*0.4f);
            int xb=gx1-(int)(hashf(gs,30+s)*(gx1-gx0)*0.4f);
            for(int x=xa;x<=xb;x++){ putAdd(fb,W,H,x,ry,r,g,b,k); putAdd(fb,W,H,x,ry+1,r,g,b,k*0.6f); }
        }
        int nv = 1 + (int)(hashf(gs,3)*2.f);
        for(int s=0;s<nv;s++){
            int rx=gx0+(int)(hashf(gs,40+s)*(gx1-gx0));
            int ya=y0+(int)(hashf(gs,50+s)*(y1-y0)*0.35f);
            int yb=y1-(int)(hashf(gs,60+s)*(y1-y0)*0.35f);
            for(int y=ya;y<=yb;y++){ putAdd(fb,W,H,rx,y,r,g,b,k); putAdd(fb,W,H,rx+1,y,r,g,b,k*0.6f); }
        }
    }
}

static VfxPluginInfo INFO = {
    VFX_PLUGIN_ABI, "Blade Runner", "veffects",
    "A rainy neon megacity at night: teal smog, amber and magenta billboards, silhouetted towers, sweeping searchlights and flying-car streaks."
};
VFX_EXPORT const VfxPluginInfo* vfx_plugin_info(void){ return &INFO; }

VFX_EXPORT void* vfx_plugin_create(int W, int H){
    State* s = new State();
    s->W=W; s->H=H; s->horizon=(int)(H*0.66f);
    s->topArr.assign(W, s->horizon+8);

    // --- skyline: overlapping tower blocks tiling the width ---
    int x=-20;
    uint32_t bseed=0;
    while(x < W+20){
        Building b;
        int bw = 22 + (int)(hashf(bseed,1)*70);
        b.x0=x; b.x1=x+bw;
        float th=hashf(bseed,2);
        // a few dominant towers, mostly mid-height blocks
        float tallness = th*th;
        int minTop = (int)(H*0.16f);
        int maxTop = s->horizon - 6;
        b.top = maxTop - (int)((maxTop-minTop)*tallness);
        b.seed = hashu(bseed*2654435761U+7u);
        b.antenna = (tallness>0.72f) ? 1 : 0;
        s->bld.push_back(b);
        for(int xi=b.x0; xi<=b.x1 && xi<W; xi++){
            if(xi<0) continue;
            if(b.top < s->topArr[xi]) s->topArr[xi]=b.top;
        }
        x += (int)(bw*0.72f); // overlap so silhouette stays dense
        bseed++;
    }

    // --- floating neon billboards ---
    const int NB=13;
    for(int i=0;i<NB;i++){
        Billboard b;
        b.bw = 14 + hashf(i,11)*34;
        b.bh = 10 + hashf(i,12)*26;
        b.bx = 24 + hashf(i,13)*(W-48);
        // keep them in the upper/mid haze, above most of the skyline
        b.by = H*0.16f + hashf(i,14)*(s->horizon - H*0.16f - 20);
        b.drift = 3.f + hashf(i,15)*8.f;
        b.phase = hashf(i,16)*6.283f;
        b.band  = i; // mapped to spectrum band at render (mod bandCount)
        b.magenta = (hashf(i,17) > 0.78f) ? 1 : 0; // magenta is an accent, amber dominates
        b.seed = hashu((uint32_t)i*40503u+3u);
        s->boards.push_back(b);
    }

    // --- searchlights anchored on tall towers ---
    const int NBEAM=3;
    for(int i=0;i<NBEAM;i++){
        Beam bm;
        bm.ox = W*(0.2f + 0.3f*i) + (hashf(i,21)-0.5f)*40.f;
        bm.oy = s->horizon - 20 - hashf(i,22)*120.f;
        bm.base = -1.5708f + (hashf(i,23)-0.5f)*0.8f; // point generally upward
        bm.span = 0.5f + hashf(i,24)*0.5f;
        bm.rate = 0.18f + hashf(i,25)*0.22f;
        bm.magenta = (i==1)?1:0;
        s->beams.push_back(bm);
    }
    return s;
}
VFX_EXPORT void vfx_plugin_destroy(void* st){ delete (State*)st; }

VFX_EXPORT void vfx_plugin_render(void* st, const VfxCanvas* cv, const VfxParams* p){
    State* s=(State*)st;
    float* fb=cv->fb; int W=cv->width, H=cv->height;
    float A=p->alpha;
    float bass=p->bass, mid=p->mid, tre=p->treble, rms=p->rms, beat=p->beat, onset=p->onset;
    double t=p->time;
    int horizon=s->horizon;
    int NBands=p->bandCount;

    // centroid shifts the amber<->magenta balance (0 -> amber, 1 -> magenta)
    float cen = clampf(p->centroid, 0.f, 1.f);
    float magentaMix = clampf(0.25f + cen*0.6f, 0.f, 1.f);

    // ---------------------------------------------------------------
    // 1) SKY / SMOG gradient (only above the skyline; towers stay dark)
    //    deep teal-blue up top, warming to an amber city glow at horizon.
    // ---------------------------------------------------------------
    float cityGlow = 0.55f + 1.1f*bass + 0.4f*rms;
    for(int y=0;y<horizon+8;y+=2){
        float v = (float)y/horizon;              // 0 top .. 1 horizon
        float hz = smooth01((v-0.45f)/0.55f);    // horizon warmth ramp
        // teal-blue base
        float r = 0.010f + 0.020f*v;
        float g = 0.030f + 0.055f*v;
        float b = 0.055f + 0.075f*v;
        // amber horizon glow blended in near the bottom of the sky
        float amberK = hz*hz*0.9f*cityGlow;
        r += amberK*0.20f; g += amberK*0.10f; b += amberK*0.02f;
        float base = 0.10f;                      // overall dimness
        float rr=r*base, gg=g*base, bb=b*base;
        for(int x=0;x<W;x+=2){
            if(y < s->topArr[x]){
                // subtle horizontal haze banding
                float band = 0.85f + 0.15f*sinf(x*0.02f + (float)t*0.2f);
                putAdd(fb,W,H,x,  y,  rr,gg,bb, band*A);
                putAdd(fb,W,H,x+1,y,  rr,gg,bb, band*A);
                putAdd(fb,W,H,x,  y+1,rr,gg,bb, band*A*0.9f);
                putAdd(fb,W,H,x+1,y+1,rr,gg,bb, band*A*0.9f);
            }
        }
    }
    // low warm haze glow hugging the horizon (sits behind the towers)
    for(int i=0;i<13;i++){
        float gx = (i+0.5f)/13.f*W;
        float pulse = 0.7f+0.3f*sinf((float)t*0.5f+i);
        cv->add_glow(cv, gx, horizon-1, 1.0f,0.48f,0.12f,
                     (0.07f+0.10f*cityGlow)*pulse*A, 70.f);
    }

    // ---------------------------------------------------------------
    // 2) SEARCHLIGHT beams -- volumetric, sweeping, flare on beat
    // ---------------------------------------------------------------
    float beamFlare = 1.f + 2.2f*beat + 1.2f*onset;
    for(size_t bi=0; bi<s->beams.size(); bi++){
        Beam& bm=s->beams[bi];
        float rate = bm.rate*(1.f + 1.6f*beat);          // sweep faster on beat
        float ang = bm.base + bm.span*sinf((float)t*rate + bi*2.1f);
        float ca=cosf(ang), sa=sinf(ang);
        float L = 430.f;
        float br,bg,bb2;
        if(bm.magenta){ br=1.0f; bg=0.28f; bb2=0.75f; }
        else          { br=1.0f; bg=0.62f; bb2=0.22f; }
        int steps=54;
        for(int i=0;i<steps;i++){
            float u=(float)i/steps;
            float px=bm.ox+ca*L*u, py=bm.oy+sa*L*u;
            float fall=(1.f-u);                            // dimmer far from source
            float rad=4.f + u*26.f;                        // cone widens outward
            float k=(0.05f+0.11f*fall)*beamFlare*(0.7f+0.5f*rms);
            cv->add_glow(cv, px,py, br,bg,bb2, k*A, rad);
        }
        // hot source
        cv->add_glow(cv, bm.ox,bm.oy, br,bg,bb2, (0.6f+1.4f*beat)*A, 10.f);
    }

    // ---------------------------------------------------------------
    // 3) FLYING-CAR light streaks crossing the sky
    // ---------------------------------------------------------------
    const int NCAR=6;
    for(int i=0;i<NCAR;i++){
        float speed = (i%2? -1.f:1.f)*(55.f + hashf(i,31)*70.f);
        float yy = H*0.14f + hashf(i,32)*(horizon-H*0.14f-30);
        float slope = (hashf(i,33)-0.5f)*0.5f;             // slight diagonal
        float span = W+120.f;
        float ph = hashf(i,34);
        float xx = fmodf((float)(ph*span + t*speed), span);
        if(xx<0) xx+=span; xx-=60.f;
        float len = 26.f + hashf(i,35)*30.f;
        float dir = (speed<0)?-1.f:1.f;
        float hx0=xx, hy0=yy;
        float hx1=xx+dir*len, hy1=yy+slope*len;
        // red/amber tail
        lineAdd(fb,W,H, hx0,hy0, hx1,hy1, 1.0f,0.25f,0.10f, 0.35f*A);
        // bright white-hot head
        cv->add_glow(cv, hx1,hy1, 1.0f,0.9f,0.7f, 0.5f*A, 3.5f);
        // faint white lead
        cv->add_glow(cv, hx0,hy0, 1.0f,0.5f,0.3f, 0.25f*A, 2.5f);
    }

    // ---------------------------------------------------------------
    // 4) NEON BILLBOARDS floating in the smog
    // ---------------------------------------------------------------
    for(size_t i=0;i<s->boards.size();i++){
        Billboard& b=s->boards[i];
        float bandE = (NBands>0) ? p->bands[b.band % NBands] : mid;
        float drift = b.drift*sinf((float)t*0.35f + b.phase);
        float sway  = 4.f*sinf((float)t*0.22f + b.phase*1.7f);
        float cx=b.bx+sway, cy=b.by+drift;
        int hw=(int)b.bw, hh=(int)b.bh;

        // brightness pulses with mid + beat + this panel's band
        float flicker = 0.9f + 0.1f*sinf((float)t*11.f + b.phase*5.f); // subtle neon buzz
        float pulse = (0.5f + 0.7f*mid + 0.5f*beat + 0.8f*bandE) * flicker;
        pulse = clampf(pulse,0.15f,2.6f);

        // color: amber base or magenta accent, biased by spectral centroid
        float r,g,bl;
        int mag = b.magenta;
        float gain;
        if(mag){ r=1.0f; g=0.16f; bl=0.58f; gain=0.92f; }   // magenta/pink accent
        else   { r=1.0f; g=0.50f; bl=0.10f; gain=1.25f; }   // hot amber -- the dominant hue
        // nudge amber panels a touch toward pink as centroid rises (subtle)
        if(!mag){ g=0.50f-0.08f*cen; bl=0.10f+0.14f*cen; }
        float pk=pulse*gain;

        // soft body glow
        panelFill(fb,W,H,(int)cx,(int)cy,hw,hh, r,g,bl, 0.17f*pk*A);
        // bright frame
        rectFrame(fb,W,H,(int)cx-hw,(int)cy-hh,(int)cx+hw,(int)cy+hh, r,g,bl, 0.55f*pk*A);
        // glyphs
        glyphStrokes(fb,W,H,(int)cx,(int)cy,hw,hh, b.seed,
                     clampf(r+0.1f,0,1), clampf(g+0.25f,0,1), clampf(bl+0.2f,0,1),
                     0.6f*pk*A);
        // bloom halo (reflected in the smog) -- tight so panels stay distinct
        cv->add_glow(cv, cx,cy, r,g,bl, (0.08f+0.10f*pulse)*gain*A, (hw+hh)*0.42f+6.f);
        // faint vertical reflection smear below (wet air)
        lineAdd(fb,W,H, cx,cy+hh, cx,cy+hh+22.f, r,g,bl, 0.05f*pulse*A);
    }

    // ---------------------------------------------------------------
    // 5) TOWER silhouettes: dark edge rim + lit-window equalizer
    // ---------------------------------------------------------------
    for(size_t bi=0; bi<s->bld.size(); bi++){
        Building& b=s->bld[bi];
        int x0=b.x0<0?0:b.x0, x1=b.x1>=W?W-1:b.x1;
        if(x0>=x1) continue;
        // cool rim light along the tower top edge -- defines the silhouette
        for(int x=x0;x<=x1;x++){
            putAdd(fb,W,H,x,b.top,   0.12f,0.40f,0.60f, (0.28f+0.30f*bass)*A);
            putAdd(fb,W,H,x,b.top+1, 0.08f,0.28f,0.45f, (0.14f+0.16f*bass)*A);
        }
        // antenna spire with a blinking beacon
        if(b.antenna){
            int mx=(x0+x1)/2;
            lineAdd(fb,W,H, mx,b.top, mx,b.top-18.f, 0.15f,0.25f,0.35f, 0.4f*A);
            float blink=0.5f+0.5f*sinf((float)t*3.0f+bi);
            cv->add_glow(cv, mx,b.top-18.f, 1.0f,0.15f,0.12f, (0.2f+0.6f*blink)*A, 4.f);
        }
        // lit windows: a grid; columns lit per spectrum band -> "skyline EQ".
        // Kept sparse so the towers still read as dark silhouettes.
        int gw=7, gh=9;                 // window cell size
        int cols=(x1-x0)/gw;
        int rows=(H-b.top-4)/gh;
        if(cols<1||rows<1) continue;
        for(int cxi=0; cxi<cols; cxi++){
            int band=(NBands>0)? ((int)(( (float)(bi*7+cxi) )) % NBands) : 0;
            float bandE=(NBands>0)? p->bands[band] : mid;
            // how many rows of this column are lit rises with the band energy
            float litFrac = clampf(0.10f + 0.80f*bandE*bandE, 0.f, 1.f);
            int litRows=(int)(litFrac*rows);
            for(int ryi=0; ryi<rows; ryi++){
                uint32_t hs=hashu(b.seed ^ (uint32_t)(cxi*131u) ^ (uint32_t)(ryi*977u));
                float onBias = (ryi < litRows) ? 0.55f : 0.06f;
                // slow twinkle so windows flip on/off over time
                float tw = hashf(hs, (uint32_t)(t*0.5) + 1u);
                if(tw > onBias) continue;
                int wx=x0+cxi*gw+1;
                int wy=b.top+2+ryi*gh + (int)(hashf(hs,3)*2);
                if(wy>=H-1) continue;
                // window color: mostly warm amber, occasional cool teal
                float warm=hashf(hs,4);
                float r,g,bl;
                if(warm>0.72f){ r=0.35f; g=0.7f; bl=1.0f; }   // teal
                else          { r=1.0f;  g=0.6f; bl=0.18f; }  // amber
                float wk=(0.28f + 0.75f*bandE + 0.35f*beat)*A;
                // 2x2 window
                putAdd(fb,W,H,wx,  wy,  r,g,bl, wk);
                putAdd(fb,W,H,wx+1,wy,  r,g,bl, wk*0.9f);
                putAdd(fb,W,H,wx,  wy+1,r,g,bl, wk*0.9f);
                putAdd(fb,W,H,wx+1,wy+1,r,g,bl, wk*0.8f);
            }
        }
    }

    // ---------------------------------------------------------------
    // 6) RAIN -- heavy diagonal streaks over everything; treble driven
    // ---------------------------------------------------------------
    float rainAmt = 0.45f + 0.9f*tre + 0.3f*rms;
    int NRAIN = 260 + (int)(220.f*clampf(tre*1.4f,0.f,1.f));
    float slopeX = 0.28f;                 // diagonal lean
    for(int i=0;i<NRAIN;i++){
        float col = hashf(i,51);
        float sp  = 260.f + hashf(i,52)*260.f;      // fall speed
        float ph  = hashf(i,53);
        float travel = H + 60.f;
        float yy = fmodf((float)(ph*travel + t*sp), travel) - 30.f;
        float xx = col*(W+60.f) - 30.f + yy*slopeX;  // shear with height
        xx = fmodf(xx, (float)(W+60.f)); if(xx<0) xx+=W+60.f; xx-=30.f;
        float len = 9.f + hashf(i,54)*12.f;
        float k = (0.06f + 0.16f*hashf(i,55)) * rainAmt * A;
        // cool near-white streak
        lineAdd(fb,W,H, xx,yy, xx-slopeX*len, yy-len, 0.55f,0.72f,0.85f, k);
    }
    // a few brighter foreground rain streaks catching the neon
    int NFG=40;
    for(int i=0;i<NFG;i++){
        float col=hashf(i,61);
        float sp=360.f+hashf(i,62)*220.f;
        float ph=hashf(i,63);
        float travel=H+80.f;
        float yy=fmodf((float)(ph*travel + t*sp), travel)-40.f;
        float xx=col*(W+80.f)-40.f + yy*slopeX;
        xx=fmodf(xx,(float)(W+80.f)); if(xx<0)xx+=W+80.f; xx-=40.f;
        float len=16.f+hashf(i,64)*18.f;
        float k=(0.14f+0.14f*hashf(i,65))*rainAmt*A;
        lineAdd(fb,W,H, xx,yy, xx-slopeX*len, yy-len, 0.7f,0.82f,0.95f, k);
    }

    // subtle ground reflection glow at the very bottom (wet street)
    for(int x=0;x<W;x+=3){
        float g=0.4f+0.6f*sinf(x*0.03f+(float)t*0.4f);
        putAdd(fb,W,H,x,H-1, 1.0f,0.5f,0.2f, (0.05f+0.10f*cityGlow)*g*A);
        putAdd(fb,W,H,x,H-2, 1.0f,0.4f,0.3f, (0.03f+0.06f*cityGlow)*g*A);
    }
}
