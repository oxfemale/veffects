// neuromancer.cpp -- "Neuromancer" (William Gibson) cyberspace scene plugin.
// "Lines of light ranged in the nonspace of the mind, clusters and constellations
// of data. Like city lights, receding." A flythrough of an infinite perspective
// data-grid, rotating neon wireframe data-constructs, a distant data-city of
// constellations on the horizon, and beat-driven ICE beams.
//
// All animation is derived from the absolute time p->time, so any frame renders
// correctly without simulation history (offline render and seeking both work).

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
static inline void lineAdd(float* fb, int W, int H, float x0,float y0,float x1,float y1,
                           float r,float g,float b,float k){
    float dx=x1-x0, dy=y1-y0; float len=sqrtf(dx*dx+dy*dy);
    if(len>2000.f) return;                       // guard against near-plane blowups
    int n=(int)len+1; if(n<1)n=1;
    for(int i=0;i<=n;i++){ float t=(float)i/n; putAdd(fb,W,H,(int)(x0+dx*t),(int)(y0+dy*t),r,g,b,k); }
}

struct Vec3 { float x,y,z; };

// A floating wireframe data-construct template (unit-ish geometry).
struct Shape { std::vector<Vec3> v; std::vector<std::pair<int,int>> e; };

struct State {
    int W, H;
    std::vector<Shape> shapes;   // cube, octahedron, tetrahedron, prism
};

static Shape makeCube(){
    Shape s;
    for(int i=0;i<8;i++)
        s.v.push_back({ (i&1)?0.6f:-0.6f, (i&2)?0.6f:-0.6f, (i&4)?0.6f:-0.6f });
    int E[12][2]={{0,1},{1,3},{3,2},{2,0},{4,5},{5,7},{7,6},{6,4},{0,4},{1,5},{2,6},{3,7}};
    for(auto&p:E) s.e.push_back({p[0],p[1]});
    return s;
}
static Shape makeOcta(){
    Shape s;
    s.v={{0.8f,0,0},{-0.8f,0,0},{0,0.8f,0},{0,-0.8f,0},{0,0,0.8f},{0,0,-0.8f}};
    int E[12][2]={{0,2},{0,3},{0,4},{0,5},{1,2},{1,3},{1,4},{1,5},{2,4},{4,3},{3,5},{5,2}};
    for(auto&p:E) s.e.push_back({p[0],p[1]});
    return s;
}
static Shape makeTetra(){
    Shape s;
    s.v={{0,0.75f,0},{-0.7f,-0.5f,-0.4f},{0.7f,-0.5f,-0.4f},{0,-0.5f,0.8f}};
    int E[6][2]={{0,1},{0,2},{0,3},{1,2},{2,3},{3,1}};
    for(auto&p:E) s.e.push_back({p[0],p[1]});
    return s;
}
static Shape makePrism(){ // vertical data-monolith (corporate database tower)
    Shape s;
    for(int i=0;i<4;i++){
        float a=(i+0.5f)*1.5708f;
        s.v.push_back({cosf(a)*0.45f, 1.1f, sinf(a)*0.45f});
        s.v.push_back({cosf(a)*0.45f,-1.1f, sinf(a)*0.45f});
    }
    for(int i=0;i<4;i++){
        int a0=i*2, a1=((i+1)%4)*2;
        s.e.push_back({a0,a1}); s.e.push_back({a0+1,a1+1}); s.e.push_back({a0,a0+1});
    }
    return s;
}

static VfxPluginInfo INFO = {
    VFX_PLUGIN_ABI, "Neuromancer", "veffects",
    "A flythrough of Gibson's cyberspace: an infinite neon perspective grid, rotating wireframe data-constructs, and a distant constellation data-city."
};
VFX_EXPORT const VfxPluginInfo* vfx_plugin_info(void){ return &INFO; }

VFX_EXPORT void* vfx_plugin_create(int W, int H){
    State* s = new State();
    s->W=W; s->H=H;
    s->shapes = { makeCube(), makeOcta(), makeTetra(), makePrism() };
    return s;
}
VFX_EXPORT void vfx_plugin_destroy(void* st){ delete (State*)st; }

VFX_EXPORT void vfx_plugin_render(void* st, const VfxCanvas* cv, const VfxParams* p){
    State* s = (State*)st;
    float* fb = cv->fb; int W = cv->width, H = cv->height;
    float A = p->alpha;
    float bass=p->bass, mid=p->mid, tre=p->treble, rms=p->rms;
    float beat=p->beat, onset=p->onset, cen=clampf(p->centroid,0.f,1.f);
    double t=p->time;

    float cx = W*0.5f;
    float hy = H*0.46f;                                   // horizon / vanishing point y
    float focal = 300.f;

    // ---- palette: cyan<->magenta blend driven by spectral centroid ----------
    float mag = clampf(cen*1.15f, 0.f, 1.f);
    float gR = mixf(0.10f, 0.95f, mag);                   // grid colour
    float gG = mixf(0.85f, 0.20f, mag);
    float gB = 1.00f;

    // ---- 0) near-black void with faint blue-violet vertical gradient --------
    for(int y=0;y<H;y+=2){
        float gy=(float)y/H;
        float up = clampf((hy-y)/hy,0.f,1.f);             // darker sky above horizon
        float m = 0.006f + 0.014f*(1.f-up)*(1.f-up)*(0.6f+0.6f*rms);
        for(int x=0;x<W;x+=2) putAdd(fb,W,H,x,y, 0.06f,0.05f,0.16f, m*A);
    }

    // ---- 1) horizon glow (data-city light-band), pulses with bass ----------
    {
        float hg = (0.6f + 1.8f*bass + 0.5f*rms);
        // broad soft electric-blue band behind the horizon
        cv->add_glow(cv, cx, hy, 0.10f,0.45f,1.0f, 0.14f*hg*A, 150.f);
        // colourful city-light clusters strung along the horizon
        for(int i=0;i<9;i++){
            float xx = cx + (hashf(99,i)-0.5f)*W*1.05f;
            float cc = hashf(99,i+40);
            float r=mixf(0.15f,1.0f,cc), g=mixf(0.9f,0.35f,cc), b=1.0f;
            cv->add_glow(cv, xx, hy-1.f, r,g,b, 0.09f*hg*A, 34.f);
        }
    }

    // ---- 2) receding perspective GRID (floor + ceiling flythrough) ----------
    float scroll = fmodf((float)t*(1.15f + 1.9f*bass), 1.f);
    int NLINES = 26;
    float floorH = H - hy, ceilH = hy;
    for(int i=1;i<=NLINES;i++){
        float zz = (float)i - scroll;                     // depth index (moves toward cam)
        if(zz < 0.35f) continue;
        float invz = 1.f/zz;
        float fade = clampf(invz*1.05f, 0.f, 1.f);
        fade *= fade;
        float k = (0.045f + 0.11f*fade)*(0.85f+0.6f*rms);
        float yF = hy + floorH*invz;                      // floor line
        float yC = hy - ceilH*invz;                       // ceiling line
        lineAdd(fb,W,H, 0,yF, W,yF, gR,gG,gB, k*A);
        lineAdd(fb,W,H, 0,yC, W,yC, gR*0.6f,gG*0.9f,gB, k*0.75f*A);
    }
    // converging vertical lines (radiate from vanishing point) -> floor & ceiling
    int NV = 15;
    for(int j=-NV;j<=NV;j++){
        float bx = cx + j*(W*0.5f/NV)*2.2f;               // spread at the near plane
        float k = 0.05f + 0.05f*(1.f - fabsf((float)j)/NV);
        k *= (0.8f+0.5f*rms);
        lineAdd(fb,W,H, cx,hy, bx,(float)H, gR,gG,gB, k*A);   // floor spoke
        float tx = cx + (bx-cx)*0.9f;
        lineAdd(fb,W,H, cx,hy, tx,0.f,       gR*0.6f,gG*0.9f,gB, k*0.6f*A); // ceiling spoke
    }

    // ---- 3) distant CONSTELLATIONS / data-city (twinkles with treble) -------
    int NSTAR=90;
    for(int i=0;i<NSTAR;i++){
        float sx = hashf(i,7)*W;
        float sy = hy - hashf(i,8)*hy*0.55f - 2.f;        // clustered just above horizon
        float tw = 0.5f + 0.5f*sinf((float)t*(2.f+hashf(i,9)*6.f) + hashf(i,10)*6.28f);
        float k = (0.05f + 0.22f*tw*(0.4f+1.2f*tre))*A;
        float cc = hashf(i,11);
        float r = mixf(0.3f,1.0f,cc), g = mixf(1.0f,0.4f,cc), b=1.0f;
        putAdd(fb,W,H,(int)sx,(int)sy, r,g,b, k*2.0f);
        if(tw>0.85f) cv->add_glow(cv, sx,sy, r,g,b, k*0.6f, 2.2f);
    }

    // ---- 4) rotating wireframe DATA-CONSTRUCTS flying past ------------------
    int NC = 12;
    float Zfar=9.0f, Znear=0.9f;
    for(int c=0;c<NC;c++){
        float phase = hashf(c,21);
        float spd = 0.045f + hashf(c,22)*0.05f;
        float u = fmodf(phase + (float)t*spd, 1.f);       // 0=far .. 1=near
        float Z = mixf(Zfar, Znear, u);
        // appear/disappear envelope
        float av = clampf(u*6.f,0.f,1.f) * clampf((0.92f-u)*8.f,0.f,1.f);
        if(av<=0.f) continue;
        // fixed lateral world position (spreads outward as it nears)
        float WX = (hashf(c,23)-0.5f)*5.0f;
        float WY = (hashf(c,24)-0.5f)*3.2f;
        float size = 0.5f + hashf(c,25)*0.6f;

        const Shape& sh = s->shapes[(int)(hashf(c,26)*3.999f)];
        // rotation: steady tumble + beat spin
        float yaw = (float)t*(0.4f+hashf(c,27)*0.6f) + hashf(c,28)*6.28f + beat*1.5f;
        float pit = (float)t*(0.3f+hashf(c,29)*0.4f) + 0.4f*mid;
        float cyw=cosf(yaw),syw=sinf(yaw),cpt=cosf(pit),spt=sinf(pit);

        // per-construct colour: cyan / green / magenta / violet accent
        float hsel = hashf(c,30);
        float r,g,b;
        if(hsel<0.45f){ r=gR; g=gG; b=gB; }               // main cyan/magenta
        else if(hsel<0.75f){ r=0.15f; g=1.0f; b=0.45f; }  // neon green
        else if(hsel<0.9f){ r=1.0f; g=0.2f; b=0.85f; }    // magenta
        else { r=0.6f; g=0.3f; b=1.0f; }                  // violet

        float bright = av * (0.55f + 0.9f*mid + 1.3f*onset*hashf(c,31))*(0.8f+0.5f*rms);
        float depthFade = clampf(1.3f - Z*0.10f, 0.25f, 1.3f);
        float k = (0.10f + 0.16f*depthFade)*bright;

        auto proj=[&](const Vec3& vin, float& ox, float& oy, bool& ok){
            // scale + rotate
            float vx=vin.x*size, vy=vin.y*size, vz=vin.z*size;
            float x1=vx*cyw+vz*syw, z1=-vx*syw+vz*cyw;
            float y1=vy*cpt-z1*spt, z2=vy*spt+z1*cpt;
            float wx=WX+x1, wy=WY+y1, wz=Z+z2;
            if(wz<0.4f){ ok=false; return; }
            ox=cx+focal*wx/wz; oy=hy-focal*wy/wz; ok=true;
        };
        for(auto& ed : sh.e){
            float ax,ay,bx,by; bool oka,okb;
            proj(sh.v[ed.first],ax,ay,oka);
            proj(sh.v[ed.second],bx,by,okb);
            if(oka&&okb) lineAdd(fb,W,H, ax,ay,bx,by, r,g,b, k*A);
        }
        // core glow at the construct centre
        float ccx,ccy; bool okc; proj({0,0,0},ccx,ccy,okc);
        if(okc) cv->add_glow(cv, ccx,ccy, r,g,b, (0.10f+0.5f*onset)*av*A, 3.5f+6.f*(1.f-Z/Zfar));
    }

    // ---- 5) band-driven data-spike SKYLINE along the horizon ---------------
    int NB=p->bandCount;
    if(NB>0){
        int step = (NB>48)?(NB/48):1;
        int cols = NB/step;
        float bw = (float)W/cols;
        for(int i=0;i<cols;i++){
            float e = p->bands[i*step];
            float bh = e*e*(70.f+40.f*bass);
            float bx = i*bw + bw*0.5f;
            float r=mixf(0.15f,gR,0.5f), g=mixf(1.0f,gG,0.4f), bb=1.0f;
            lineAdd(fb,W,H, bx,hy, bx,hy-bh, r,g,bb, (0.10f+0.35f*e)*A);
            if(e>0.4f) cv->add_glow(cv, bx,hy-bh, r,g,bb, 0.12f*e*A, 3.f);
        }
    }

    // ---- 6) occasional bright ICE beams / walls on strong beats ------------
    float ice = clampf(beat*1.1f + onset*0.7f, 0.f, 1.f);
    if(ice>0.25f){
        int nbeams = 1 + (int)(ice*2.99f);
        int bucket = (int)(t*2.0);                        // deterministic per time-slice
        for(int q=0;q<nbeams;q++){
            uint32_t hs = hashu((uint32_t)bucket*2654435761u + q*40503u);
            float bx = (hashf(hs,1))*W;
            float k = (0.18f + 0.5f*ice)*A;
            // vertical ICE wall of light, brightest at the horizon
            for(int y=0;y<H;y+=1){
                float d = fabsf((float)y-hy)/H;
                float f = (1.f-d)*(1.f-d);
                putAdd(fb,W,H,(int)bx,   y, 0.5f,0.9f,1.0f, k*f*0.6f);
                putAdd(fb,W,H,(int)bx+1, y, 0.4f,0.85f,1.0f, k*f*0.4f);
            }
            cv->add_glow(cv, bx,hy, 0.6f,0.95f,1.0f, k*0.8f, 30.f);
        }
    }
}
