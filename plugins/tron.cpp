// tron.cpp -- "Tron: The Grid" scene plugin.
// A vast neon perspective grid receding to a glowing horizon and skyline, with
// cyan and orange light cycles racing across it trailing solid neon ribbon walls
// that turn at right angles, plus circuit lines and spinning identity-disc arcs.
//
// All animation is derived analytically from the absolute time p->time, so every
// frame renders correctly without simulation history (offline render + seek work).
// Audio only modulates brightness/color, never the cycles' positions.

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
static inline void lineAdd(float* fb, int W, int H, float x0,float y0,float x1,float y1,
                           float r,float g,float b,float k){
    float dx=x1-x0, dy=y1-y0; float len=sqrtf(dx*dx+dy*dy);
    int n=(int)len+1; if(n<1)n=1;
    for(int i=0;i<=n;i++){ float t=(float)i/n; putAdd(fb,W,H,(int)(x0+dx*t),(int)(y0+dy*t),r,g,b,k); }
}
// vertical span (a slice of a neon wall), clamped to a max height for safety
static inline void vspan(float* fb, int W, int H, int x, float yTop, float yBot,
                         float r,float g,float b,float k){
    int y0=(int)yTop, y1=(int)yBot;
    if(y1<y0){ int t=y0; y0=y1; y1=t; }
    if(y1-y0 > 160) y0 = y1-160;   // clamp very-near walls
    for(int y=y0;y<=y1;y++) putAdd(fb,W,H,x,y,r,g,b,k);
}

// A light cycle rides a precomputed axis-aligned path in ground space (lateral x,
// depth d). Turns are all 90 degrees; the whole path loops via fmod on arc length.
struct Cycle {
    std::vector<float> px, pd;   // polyline vertices (ground plane)
    std::vector<float> cum;      // cumulative arc length at each vertex
    float total = 0.f;
    float speed = 0.f;           // ground units / second (from time only)
    float phase = 0.f;           // start offset along the path
    int   team  = 0;             // 0 = cyan, 1 = orange
};

struct State {
    int W, H;
    std::vector<Cycle> cycles;
};

// Build one right-angle path bounded inside a ground box.
static void buildPath(Cycle& c, uint32_t seed){
    const float XMIN=-4.2f, XMAX=4.2f, DMIN=0.75f, DMAX=9.5f;
    float x = XMIN + hashf(seed,1)*(XMAX-XMIN);
    float d = DMIN + hashf(seed,2)*(DMAX-DMIN);
    int dir = (int)(hashf(seed,3)*4.f) & 3;   // 0:+x 1:-x 2:+d 3:-d
    c.px.push_back(x); c.pd.push_back(d);
    c.cum.push_back(0.f);
    const int SEGS = 26;
    for(int s=0;s<SEGS;s++){
        float len = 1.2f + hashf(seed, 100+s)*2.6f;
        float nx=x, nd=d;
        // try current direction, else pick one that stays in the box
        for(int tries=0; tries<4; tries++){
            float tx=x, td=d;
            switch(dir){
                case 0: tx += len; break;
                case 1: tx -= len; break;
                case 2: td += len; break;
                case 3: td -= len; break;
            }
            if(tx>=XMIN && tx<=XMAX && td>=DMIN && td<=DMAX){ nx=tx; nd=td; break; }
            // turn 90 degrees toward a valid heading
            dir = (dir<2) ? (2 + ((s+tries)&1)) : (0 + ((s+tries)&1));
        }
        x=nx; d=nd;
        float dxl = x-c.px.back(), ddl = d-c.pd.back();
        c.total += sqrtf(dxl*dxl+ddl*ddl);
        c.px.push_back(x); c.pd.push_back(d); c.cum.push_back(c.total);
        // 90-degree turn for the next segment
        int nextDir;
        if(dir<2) nextDir = 2 + (int)(hashf(seed,200+s)*2.f);   // was lateral -> go depth
        else      nextDir = 0 + (int)(hashf(seed,200+s)*2.f);   // was depth  -> go lateral
        dir = nextDir & 3;
    }
    if(c.total < 1.f) c.total = 1.f;
}

// Ground position at arc length s (wrapped) along the cycle's path.
static void samplePath(const Cycle& c, float s, float& x, float& d){
    s = fmodf(s, c.total); if(s<0) s += c.total;
    // linear scan (paths are short); find segment containing s
    size_t i=0;
    for(; i+1<c.cum.size(); ++i) if(c.cum[i+1] >= s) break;
    if(i+1>=c.px.size()){ x=c.px.back(); d=c.pd.back(); return; }
    float seg = c.cum[i+1]-c.cum[i]; if(seg<1e-4f) seg=1e-4f;
    float u = (s-c.cum[i])/seg;
    x = c.px[i] + (c.px[i+1]-c.px[i])*u;
    d = c.pd[i] + (c.pd[i+1]-c.pd[i])*u;
}

static VfxPluginInfo INFO = {
    VFX_PLUGIN_ABI, "Tron", "veffects",
    "A neon perspective grid receding to a glowing horizon while cyan and orange light cycles race across it trailing solid ribbon walls."
};
VFX_EXPORT const VfxPluginInfo* vfx_plugin_info(void){ return &INFO; }

VFX_EXPORT void* vfx_plugin_create(int W, int H){
    State* s = new State();
    s->W=W; s->H=H;
    const int NC = 5;
    s->cycles.resize(NC);
    for(int i=0;i<NC;i++){
        Cycle& c = s->cycles[i];
        uint32_t seed = hashu(0x51A0u + i*2654435761U);
        buildPath(c, seed);
        c.speed = 2.1f + hashf(seed,7)*1.8f;
        c.phase = hashf(seed,8)*c.total;
        c.team  = i & 1;
    }
    return s;
}
VFX_EXPORT void vfx_plugin_destroy(void* st){ delete (State*)st; }

VFX_EXPORT void vfx_plugin_render(void* st, const VfxCanvas* cv, const VfxParams* p){
    State* s = (State*)st;
    float* fb = cv->fb; int W = cv->width, H = cv->height;
    float A = p->alpha;
    float bass=p->bass, mid=p->mid, tre=p->treble, rms=p->rms;
    float beat=p->beat, onset=p->onset, cen=clampf(p->centroid,0.f,1.f);
    double t = p->time;

    const float HY = H*0.42f;            // horizon line
    const float PY = 235.f;              // vertical perspective scale (eye height * focal)
    const float PX = 300.f;              // lateral perspective scale
    // ground(x,d) -> screen; floor points sit below the horizon
    auto proj = [&](float x, float d, float& sx, float& sy){
        float inv = 1.f/d; sx = W*0.5f + x*PX*inv; sy = HY + PY*inv;
    };

    // global cyan<->orange bias from spectral centroid (brighter timbre -> more orange)
    float orangeBias = clampf(0.35f + cen*0.5f, 0.f, 1.f);
    const float CY_R=0.05f, CY_G=0.85f, CY_B=1.0f;   // cyan
    const float OR_R=1.0f,  OR_G=0.42f, OR_B=0.06f;  // orange

    // 1) near-black sky with a cool horizon glow band (kept blue, not green)
    for(int y=0;y<(int)HY;y+=1){
        float gy = (float)y/HY;                     // 0 top .. 1 horizon
        float glow = powf(gy, 3.5f) * (0.05f + 0.26f*bass + 0.08f*rms);
        // bias glow color toward deep blue with an orange warm-up on bright timbre
        float r = 0.04f + 0.55f*orangeBias;
        float g = 0.16f + 0.20f*orangeBias;
        float b = 0.55f - 0.35f*orangeBias;
        for(int x=0;x<W;x+=2) putAdd(fb,W,H,x,y, r, g, b, glow*A);
    }
    // faint dark floor wash
    for(int y=(int)HY;y<H;y+=2){
        float m = 0.010f + 0.020f*((float)(y-HY)/(H-HY));
        for(int x=0;x<W;x+=3) putAdd(fb,W,H,x,y, 0.02f,0.10f,0.16f, m*A);
    }

    // 2) distant neon skyline sitting on the horizon
    {
        int nB = 26;
        for(int i=0;i<nB;i++){
            uint32_t hs = hashu(i*40507u);
            float bx = (float)i/nB * W + hashf(hs,1)*6.f;
            float bw = 6.f + hashf(hs,2)*16.f;
            float bh = 6.f + hashf(hs,3)*30.f * (0.6f+0.8f*bass);
            int team = (hashf(hs,4) < orangeBias) ? 1 : 0;
            float r = team?OR_R:CY_R, g = team?OR_G:CY_G, b = team?OR_B:CY_B;
            float k = (0.10f + 0.20f*rms)*A;
            for(int x=(int)bx;x<(int)(bx+bw) && x<W;x++){
                putAdd(fb,W,H,x,(int)HY, r,g,b, k*1.3f);         // bright rooftop line
                for(int y=(int)(HY-bh); y<(int)HY; y++)
                    putAdd(fb,W,H,x,y, r*0.5f,g*0.6f,b*0.7f, k*0.35f);
            }
        }
        // horizon bloom line
        for(int x=0;x<W;x++){
            float r = CY_R*(1.f-orangeBias)+OR_R*orangeBias;
            float g = CY_G*(1.f-orangeBias)+OR_G*orangeBias;
            float b = CY_B*(1.f-orangeBias)+OR_B*orangeBias;
            putAdd(fb,W,H,x,(int)HY,   r,g,b, (0.20f+0.5f*bass)*A);
            putAdd(fb,W,H,x,(int)HY+1, r*0.6f,g,b, (0.12f+0.3f*bass)*A);
        }
    }

    // 3) perspective grid floor
    const float GS = 0.62f;              // world grid spacing
    float gridPulse = 0.55f + 0.9f*bass; // bass drives grid brightness
    // 3a) depth (horizontal) lines scrolling toward the viewer
    {
        float scroll = fmodf((float)t*1.3f, GS);
        int NL = 30;
        for(int k=0;k<NL;k++){
            float d = 0.55f + k*GS + scroll;
            if(d < 0.55f) continue;
            float sx0,sy0,sx1,sy1;
            proj(-6.f,d, sx0,sy0); proj(6.f,d, sx1,sy1);
            float fade = clampf(1.0f/(0.4f+d*0.55f), 0.f, 1.f);
            // let a spectrum band ripple across successive depth lines
            float band = 0.f;
            if(p->bandCount>0) band = p->bands[k % p->bandCount];
            float k2 = (0.10f + 0.34f*fade)*gridPulse*(0.7f+0.9f*band)*A;
            lineAdd(fb,W,H, sx0,sy0, sx1,sy1, CY_R*0.6f, CY_G, CY_B, k2);
        }
    }
    // 3b) lateral (converging) lines fanning from the vanishing point
    {
        for(int i=-7;i<=7;i++){
            float x = i*GS;
            float nx,ny,fx,fy;
            proj(x, 0.55f, nx,ny);   // near
            proj(x, 11.f,  fx,fy);   // far (toward horizon)
            float k2 = (0.12f + 0.20f*gridPulse)*A;
            lineAdd(fb,W,H, nx,ny, fx,fy, CY_R*0.6f, CY_G, CY_B, k2);
        }
    }

    // 4) light cycles + solid neon ribbon walls
    float boost = clampf(beat*1.1f + onset*0.6f, 0.f, 1.4f);   // beat brightens walls/heads
    for(size_t ci=0; ci<s->cycles.size(); ++ci){
        Cycle& c = s->cycles[ci];
        float head = c.phase + (float)t*c.speed;               // analytic position
        float r = c.team?OR_R:CY_R, g = c.team?OR_G:CY_G, b = c.team?OR_B:CY_B;
        float wallH = 0.55f;                                   // world wall height
        float trailLen = 6.0f + 1.5f*boost;                    // ground units of trail
        int   NS = 150;                                        // trail samples
        float prevSx=0, prevSyT=0, prevSyF=0; bool have=false;
        for(int i=NS;i>=0;i--){
            float frac = (float)i/NS;                          // 1 = tail .. 0 = head
            float s_arc = head - trailLen*frac;
            float gx, gd; samplePath(c, s_arc, gx, gd);
            if(gd < 0.55f) { have=false; continue; }
            float sx, syF; proj(gx, gd, sx, syF);
            float wallPx = wallH*PY/gd;
            float syT = syF - wallPx;
            float taper = 0.35f + 0.65f*(1.f-frac);            // brighter near head
            float depthFade = clampf(1.0f/(0.5f+gd*0.5f),0.f,1.f);
            float k = (0.30f + 0.55f*taper)*(0.85f+0.6f*boost)*depthFade*A;
            // draw the wall as a vertical slice, plus fill gaps between samples in x
            int xi = (int)sx;
            vspan(fb,W,H, xi, syT, syF, r,g,b, k);
            // fill horizontal gap to the previous sample so the wall stays solid,
            // interpolating both the floor and top edges of the ribbon
            if(have){
                int x0=(int)prevSx, x1=xi;
                float yTa=syT, yBa=syF, yTb=prevSyT, yBb=prevSyF;
                if(x1<x0){ int tt=x0;x0=x1;x1=tt; float s;
                           s=yTa;yTa=yTb;yTb=s; s=yBa;yBa=yBb;yBb=s; }
                if(x1-x0>1 && x1-x0<W){
                    for(int xx=x0+1;xx<x1;xx++){
                        float u=(float)(xx-x0)/(x1-x0);
                        vspan(fb,W,H, xx, yTa+(yTb-yTa)*u, yBa+(yBb-yBa)*u, r,g,b, k*0.9f);
                    }
                }
            }
            // bright top edge of the wall
            putAdd(fb,W,H, xi, (int)syT, r+0.3f, g+0.3f, b+0.3f, k*1.4f);
            prevSx=sx; prevSyT=syT; prevSyF=syF; have=true;
        }
        // the cycle itself: a bright head glow at the front of the trail
        float hx, hd; samplePath(c, head, hx, hd);
        if(hd >= 0.55f){
            float sx, sy; proj(hx, hd, sx, sy);
            float wallPx = wallH*PY/hd;
            float k = (1.2f + 2.6f*boost)*A * clampf(1.f/(0.4f+hd*0.4f),0.2f,1.5f);
            cv->add_glow(cv, sx, sy - wallPx*0.5f, r,g,b, k, 4.5f + 4.f*boost);
            cv->add_glow(cv, sx, sy, r+0.2f,g+0.2f,b+0.2f, k*0.6f, 3.f);
        }
    }

    // 5) circuit lines: short bright segments creeping along the grid, lit by bands
    {
        int NLc = 14;
        for(int i=0;i<NLc;i++){
            uint32_t hs = hashu(i*2246822519U + 7u);
            float lane = (hashf(hs,1)*14.f - 7.f)*GS;          // a lateral track
            float span = 2.5f + hashf(hs,2)*4.f;
            float sp   = 1.5f + hashf(hs,3)*2.5f;
            float d0   = 0.7f + fmodf((float)t*sp + hashf(hs,4)*10.f, 9.f);
            float band = (p->bandCount>0)? p->bands[i % p->bandCount] : mid;
            float k = (0.15f + 0.9f*band)*A;
            if(k < 0.03f) continue;
            int team = (hashf(hs,5) < orangeBias)?1:0;
            float r = team?OR_R:CY_R, g = team?OR_G:CY_G, b = team?OR_B:CY_B;
            float ax,ay,bx,by;
            proj(lane, d0, ax,ay); proj(lane, d0+span, bx,by);
            lineAdd(fb,W,H, ax,ay, bx,by, r,g,b, k);
        }
    }

    // 6) identity-disc arcs: spinning neon rings floating above the grid
    {
        int ND = 3;
        for(int i=0;i<ND;i++){
            float cx = W*(0.22f + 0.28f*i);
            float cy = HY*0.55f + 20.f*sinf((float)t*0.5f + i*2.1f);
            float R  = 26.f + 8.f*sinf((float)t*0.7f + i);
            int team = i&1;
            float r = team?OR_R:CY_R, g = team?OR_G:CY_G, b = team?OR_B:CY_B;
            float spin = (float)t*(1.4f+0.3f*i) + i*2.0f;
            float kbase = (0.20f + 0.9f*mid)*A;
            for(int a=0;a<3;a++){                              // three arc segments
                float a0 = spin + a*2.094f;
                float a1 = a0 + 1.15f;
                int steps=14;
                for(int j=0;j<steps;j++){
                    float u0=a0+(a1-a0)*j/steps, u1=a0+(a1-a0)*(j+1)/steps;
                    lineAdd(fb,W,H, cx+cosf(u0)*R, cy+sinf(u0)*R*0.9f,
                                    cx+cosf(u1)*R, cy+sinf(u1)*R*0.9f, r,g,b, kbase);
                }
            }
            cv->add_glow(cv, cx, cy, r,g,b, kbase*0.5f, 4.f);
        }
    }

    // 7) treble sparkles skittering over the grid floor
    if(tre > 0.02f){
        int NSp = 90;
        for(int i=0;i<NSp;i++){
            uint32_t hs = hashu((uint32_t)i*911u ^ (uint32_t)(t*3.0));
            float tw = hashf(hs, (uint32_t)(t*6.0)+1u);
            if(tw > 0.6f){
                float gx = (hashf(hs,2)*14.f-7.f)*GS;
                float gd = 0.7f + hashf(hs,3)*9.f;
                float sx, sy; proj(gx, gd, sx, sy);
                float k = tre*(0.4f+0.6f*tw)*A;
                putAdd(fb,W,H,(int)sx,(int)sy, 1.f,1.f,1.f, k);
                putAdd(fb,W,H,(int)sx+1,(int)sy, 0.8f,0.9f,1.f, k*0.5f);
            }
        }
    }
}
