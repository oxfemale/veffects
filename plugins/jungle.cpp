// jungle.cpp -- "Jungle" scene plugin.
// A lush rainforest: deep layered green foliage in several parallax layers, a
// tall trunk lattice and hanging vines, diagonal god-rays streaming through the
// canopy, drifting glowing fireflies / spore motes, a shimmering waterfall on one
// side, colorful parrots flitting across, and a big monstera leaf in the front.
//
// All animation is derived analytically from the absolute time p->time, so any
// frame renders correctly without simulation history (offline render + seeking).
// The player tone-maps the additive HDR buffer (bloom/shake/grain), so we lay a
// full lush base every frame. All output is scaled by p->alpha.

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
// OPAQUE composite (fb = fb*(1-a) + rgb*a) -- for leaves/birds so their colors read
static inline void putOver(float* fb, int W, int H, int x, int y,
                           float r, float g, float b, float a){
    if ((unsigned)x >= (unsigned)W || (unsigned)y >= (unsigned)H) return;
    size_t i=((size_t)y*W+x)*3; float ia=1.f-a;
    fb[i]=fb[i]*ia + r*a; fb[i+1]=fb[i+1]*ia + g*a; fb[i+2]=fb[i+2]*ia + b*a;
}
static void ellipseOver(float* fb, int W, int H, float cx, float cy, float rx, float ry,
                        float r, float g, float b, float a){
    int y0=(int)floorf(cy-ry), y1=(int)ceilf(cy+ry);
    for(int y=y0;y<=y1;y++){
        float dy=(y-cy)/ry; if(dy<-1.f||dy>1.f) continue;
        float ext=rx*sqrtf(1.f-dy*dy);
        int xa=(int)floorf(cx-ext), xb=(int)ceilf(cx+ext);
        for(int x=xa;x<=xb;x++) putOver(fb,W,H,x,y,r,g,b,a);
    }
}
// rotated filled ellipse (leaf blade) -- opaque
static void leafBlade(float* fb, int W, int H, float cx, float cy,
                      float len, float wid, float ang,
                      float r, float g, float b, float a){
    float ca=cosf(ang), sa=sinf(ang);
    int rad=(int)ceilf(len)+1;
    for(int oy=-rad;oy<=rad;oy++) for(int ox=-rad;ox<=rad;ox++){
        // rotate offset into leaf-local space
        float lx= ox*ca + oy*sa;   // along blade
        float ly=-ox*sa + oy*ca;   // across blade
        float u=lx/len, v=ly/wid;
        if(u*u+v*v<=1.f){
            // slight darkening toward the tips for shape
            float sh=1.f-0.25f*(u*u);
            putOver(fb,W,H,(int)(cx+ox),(int)(cy+oy), r*sh,g*sh,b*sh, a);
        }
    }
}
// additive soft line (vines / rays / water)
static void lineAdd(float* fb, int W, int H, float x0,float y0,float x1,float y1,
                    float r,float g,float b,float k,float wid){
    float dx=x1-x0, dy=y1-y0; float len=sqrtf(dx*dx+dy*dy); int n=(int)len+1;
    int hw=(int)wid;
    for(int i=0;i<=n;i++){ float t=(float)i/n; float px=x0+dx*t, py=y0+dy*t;
        for(int oy=-hw;oy<=hw;oy++) for(int ox=-hw;ox<=hw;ox++){
            float d=sqrtf((float)(ox*ox+oy*oy)); float f=clampf(1.f-d/(wid+0.5f),0.f,1.f);
            putAdd(fb,W,H,(int)px+ox,(int)py+oy, r,g,b, k*f);
        }
    }
}
// filled triangle (opaque) via bounding box + edge sign test -- for bird wings
static void triOver(float* fb, int W, int H,
                    float ax,float ay,float bx,float by,float cx,float cy,
                    float r,float g,float b,float a){
    int x0=(int)floorf(fminf(ax,fminf(bx,cx))), x1=(int)ceilf(fmaxf(ax,fmaxf(bx,cx)));
    int y0=(int)floorf(fminf(ay,fminf(by,cy))), y1=(int)ceilf(fmaxf(ay,fmaxf(by,cy)));
    float area=(bx-ax)*(cy-ay)-(by-ay)*(cx-ax);
    if(fabsf(area)<1e-4f) return; float inv=1.f/area;
    for(int y=y0;y<=y1;y++) for(int x=x0;x<=x1;x++){
        float px=x+0.5f, py=y+0.5f;
        float w0=((bx-ax)*(py-ay)-(by-ay)*(px-ax))*inv;
        float w1=((cx-bx)*(py-by)-(cy-by)*(px-bx))*inv;
        float w2=1.f-w0-w1;
        if(w0>=0.f&&w1>=0.f&&w2>=0.f) putOver(fb,W,H,x,y,r,g,b,a);
    }
}
// opaque thick line (trunks / vines silhouettes)
static void lineOver(float* fb, int W, int H, float x0,float y0,float x1,float y1,
                     float r,float g,float b,float a,float wid){
    float dx=x1-x0, dy=y1-y0; float len=sqrtf(dx*dx+dy*dy); int n=(int)len+1;
    int hw=(int)wid;
    for(int i=0;i<=n;i++){ float t=(float)i/n; float px=x0+dx*t, py=y0+dy*t;
        for(int oy=-hw;oy<=hw;oy++) for(int ox=-hw;ox<=hw;ox++)
            if(ox*ox+oy*oy<=hw*hw) putOver(fb,W,H,(int)px+ox,(int)py+oy, r,g,b, a);
    }
}

struct Leaf { float x, y, size, ang, aphase; int layer; };
struct Fly  { float x, y, phx, phy, sp, sz; };
struct Bird { float phase, y, sp; int dir; float hue; };
struct State {
    int W, H;
    std::vector<Leaf> leaves;   // background foliage clumps
    std::vector<Fly>  flies;    // fireflies
    std::vector<Bird> birds;
    int nVines = 9;
    int nTrunks = 5;
};

static VfxPluginInfo INFO = {
    VFX_PLUGIN_ABI, "Jungle", "veffects",
    "A lush rainforest of layered green foliage, hanging vines, diagonal god-rays, glowing fireflies, a shimmering waterfall and darting tropical parrots."
};
VFX_EXPORT const VfxPluginInfo* vfx_plugin_info(void){ return &INFO; }

VFX_EXPORT void* vfx_plugin_create(int W, int H){
    State* s=new State(); s->W=W; s->H=H;
    // Parallax foliage clumps: layer 0 = far/light, 3 = near/dark. Big near the
    // top (canopy) and edges.
    int NL=150;
    for(int i=0;i<NL;i++){
        Leaf L;
        L.layer  = (int)(hashf(i,1)*4.f); if(L.layer>3) L.layer=3;
        L.x      = hashf(i,2)*W;
        // near layers cling to top canopy and bottom; far layers fill mid
        float yb = hashf(i,3);
        L.y      = (L.layer>=2)? (yb*0.42f*H) : (0.05f*H + yb*0.9f*H);
        L.size   = (18.f + hashf(i,4)*40.f) * (0.6f + 0.5f*L.layer);
        L.ang    = hashf(i,5)*6.2832f;
        L.aphase = hashf(i,6)*6.2832f;
        s->leaves.push_back(L);
    }
    // Fireflies
    int NF=90;
    for(int i=0;i<NF;i++){
        Fly f;
        f.x=hashf(i,10)*W; f.y=hashf(i,11)*H;
        f.phx=hashf(i,12)*6.2832f; f.phy=hashf(i,13)*6.2832f;
        f.sp=0.4f+hashf(i,14)*1.1f;
        f.sz=1.0f+hashf(i,15)*1.6f;
        s->flies.push_back(f);
    }
    // Parrots
    int NB=6;
    for(int i=0;i<NB;i++){
        Bird b;
        b.phase=hashf(i,20);
        b.y=0.14f*H + hashf(i,21)*0.6f*H;
        b.sp=70.f+hashf(i,22)*80.f;
        b.dir=(hashf(i,23)<0.5f)? 1:-1;
        b.hue=hashf(i,24);
        s->birds.push_back(b);
    }
    return s;
}
VFX_EXPORT void vfx_plugin_destroy(void* st){ delete (State*)st; }

VFX_EXPORT void vfx_plugin_render(void* st, const VfxCanvas* cv, const VfxParams* p){
    State* s=(State*)st;
    float* fb=cv->fb; int W=cv->width, H=cv->height;
    float A=p->alpha;
    float bass=p->bass, mid=p->mid, tre=p->treble, rms=p->rms, beat=p->beat, onset=p->onset;
    float centroid=clampf(p->centroid,0.f,1.f);
    double t=p->time;
    int NBND=p->bandCount;

    // centroid warms(low)<->cools(high) the golden light
    float warm = mixf(1.10f, 0.80f, centroid);   // R gain of light
    float cool = mixf(0.80f, 1.10f, centroid);   // B gain of light

    // ================= BASE: deep green vertical gradient =================
    // Brighter, warmer canopy up top; dark forest floor at the bottom.
    float swell = 1.f + 0.10f*bass + 0.04f*sinf((float)t*0.4f);
    for(int y=0;y<H;y++){
        float v=(float)y/H;                       // 0 canopy .. 1 floor
        // top: warm-tinged bright green; bottom: deep shadow green
        float br=mixf(0.06f,0.010f,v);
        float bg=mixf(0.22f,0.045f,v);
        float bb=mixf(0.07f,0.020f,v);
        // soft dappled light near the top
        float dap=clampf(1.f-v*1.4f,0.f,1.f); dap*=dap;
        for(int x=0;x<W;x++){
            float d = 0.5f+0.5f*sinf(x*0.02f + (float)t*0.3f)*sinf(y*0.03f - (float)t*0.2f);
            float add=0.05f*dap*d;
            putAdd(fb,W,H,x,y, (br+add*warm), (bg+add*1.1f), (bb+add*0.4f), 0.95f*A*swell);
        }
    }

    // ================= GOD-RAYS: diagonal shafts from top-right =================
    // Brighten with bass. Drawn additively as broad soft diagonal bands.
    float rayBright = (0.70f + 1.0f*bass) * A;
    float rayHue_r=1.0f*warm, rayHue_g=0.90f, rayHue_b=0.50f*cool;
    for(int y=0;y<H;y++){
        float v=(float)y/H;
        float atten=clampf(1.f-v*0.75f,0.08f,1.f);   // rays fade toward floor
        for(int x=0;x<W;x++){
            // diagonal coordinate (light comes from upper-right, angled down-left)
            float sc = (float)x + y*0.9f;
            float ray=0.f;
            for(int k=0;k<6;k++){
                float center = 60.f + k*165.f + 26.f*sinf((float)t*0.25f + k*1.3f);
                float dd=(sc-center)/42.f;
                float bm = (NBND>0)? (0.55f+0.9f*p->bands[(k*2)%NBND]) : 1.f;
                ray += expf(-dd*dd)*bm;
            }
            float k=ray*atten*rayBright*0.26f;
            if(k>0.001f)
                putAdd(fb,W,H,x,y,   rayHue_r,rayHue_g,rayHue_b, k);
        }
    }

    // ================= FAR/MID FOLIAGE: parallax leaf clumps =================
    // Sway with mid. Far layers lighter, near layers darker green.
    float sway = (2.f + 10.f*mid);
    for(size_t i=0;i<s->leaves.size();++i){
        Leaf& L=s->leaves[i];
        int ly=L.layer;
        // parallax drift per layer (analytic, wraps)
        float drift = (float)t*(2.f + ly*3.f);
        float x = fmodf(L.x + drift, (float)W); if(x<0) x+=W;
        float swy = sinf((float)t*0.7f + L.aphase)*sway*(0.4f+0.3f*ly);
        float ang = L.ang + 0.12f*sinf((float)t*0.5f + L.aphase);
        // brightness by layer; p->bands drives per-layer pop
        float bmod = (NBND>0)? (0.75f+0.6f*p->bands[(ly*5)%NBND]) : 1.f;
        float base = mixf(0.34f, 0.12f, ly/3.f) * bmod;   // far bright -> near dark
        float r=base*0.30f, g=base, b=base*0.28f;
        float aa = mixf(0.45f, 0.92f, ly/3.f);
        // a leaf clump = a few blades fanned out
        int nb=3;
        for(int q=0;q<nb;q++){
            float a2=ang + (q-1)*0.6f;
            leafBlade(fb,W,H, x, L.y+swy, L.size, L.size*0.42f, a2, r,g,b, aa*A);
        }
        // subtle vein highlight on nearer leaves
        if(ly>=2){
            float ca=cosf(ang), sa=sinf(ang);
            lineAdd(fb,W,H, x-ca*L.size,L.y+swy-sa*L.size, x+ca*L.size,L.y+swy+sa*L.size,
                    0.08f,0.18f,0.05f, 0.3f*A, 0.6f);
        }
    }

    // ================= TALL TREE TRUNKS =================
    for(int c=0;c<s->nTrunks;c++){
        float bx=(c+0.5f)/s->nTrunks*W + (hashf(c,60)-0.5f)*50.f;
        float wdt=6.f+hashf(c,61)*7.f;
        float tr=0.10f, tg=0.07f, tb=0.05f;   // dark brown-green bark
        lineOver(fb,W,H, bx, (float)H+4, bx+(hashf(c,62)-0.5f)*24.f, -10.f, tr,tg,tb, 0.9f, wdt);
    }

    // ================= HANGING VINES (sway with mid) =================
    for(int kk=0;kk<s->nVines;kk++){
        float rootX=(kk+0.5f)/s->nVines*W + (hashf(kk,30)-0.5f)*40.f;
        float hgt=90.f + hashf(kk,31)*200.f;
        int seg=12;
        float vsw=6.f + 22.f*mid;
        float ph=hashf(kk,32)*6.28f;
        float px=rootX, py=-4.f;
        float g0=0.20f+0.18f*hashf(kk,33);
        for(int i=1;i<=seg;i++){
            float f=(float)i/seg;
            float ny=hgt*f;
            float nx=rootX + sinf((float)t*0.8f + ph + f*2.2f)*vsw*f*f;
            lineAdd(fb,W,H, px,py, nx,ny, 0.05f,g0,0.10f, 0.75f*A, 1.8f-0.8f*f);
            // occasional leaf on the vine
            if((i%4)==0)
                leafBlade(fb,W,H, nx, ny, 9.f, 4.f, ph+f, 0.09f,0.30f,0.09f, 0.7f*A);
            px=nx; py=ny;
        }
    }

    // ================= WATERFALL on the left edge =================
    // Falling shimmering water; shimmer brightens with rms.
    {
        float wx0=W*0.04f, wx1=W*0.16f;   // waterfall band
        float shim=0.5f+1.3f*rms;
        int strands=14;
        for(int q=0;q<strands;q++){
            float fx=mixf(wx0,wx1,(q+0.5f)/strands) + 2.f*sinf((float)t*0.6f+q);
            // vertical falling streaks (phase scroll downward analytically)
            for(int seg=0;seg<26;seg++){
                float sp=220.f;   // fall speed px/s
                float yy=fmodf((float)t*sp + hashf(q,70+seg)*H + seg*20.f, (float)H);
                float glow=0.30f + 0.5f*fabsf(sinf(yy*0.08f + (float)t*3.0f + q));
                float kk=(0.10f + 0.22f*glow)*shim*A;
                putAdd(fb,W,H,(int)fx,(int)yy, 0.55f,0.80f,0.95f, kk);
                putAdd(fb,W,H,(int)fx+1,(int)yy, 0.45f,0.72f,0.90f, kk*0.6f);
            }
        }
        // base column glow + splash/mist at the bottom
        cv->add_glow(cv, (wx0+wx1)*0.5f, H*0.5f, 0.4f,0.65f,0.9f, (0.10f+0.18f*rms)*A, 60.f);
        for(int m=0;m<40;m++){
            float mx=mixf(wx0-8,wx1+8,hashf(m,80)) + 6.f*sinf((float)t*1.5f+m);
            float my=H - fabsf(sinf((float)t*2.0f + m*0.7f))*24.f - hashf(m,81)*10.f;
            cv->add_glow(cv, mx,my, 0.6f,0.8f,1.0f, 0.12f*shim*A, 4.f+hashf(m,82)*4.f);
        }
    }

    // ================= FIREFLIES / spore motes =================
    // Pulse + swarm with treble + beat.
    int flyCount=(int)s->flies.size();
    // bands can raise the active count a touch
    float pulseAll = 0.5f + 0.8f*tre + 0.9f*beat;
    for(int i=0;i<flyCount;i++){
        Fly& f=s->flies[i];
        // swarm: gentle lissajous drift + a beat-driven convergence toward center
        float baseX=f.x + sinf((float)t*f.sp + f.phx)*40.f + (float)t*4.f;
        float baseY=f.y + cosf((float)t*f.sp*0.8f + f.phy)*32.f;
        // swarm pull toward a slowly moving attractor when beat hits
        float ax=W*(0.5f+0.25f*sinf((float)t*0.3f));
        float ay=H*(0.45f+0.20f*cosf((float)t*0.23f));
        float pull=0.25f*beat;
        float x=fmodf(mixf(baseX,ax,pull), (float)W); if(x<0)x+=W;
        float y=mixf(baseY,ay,pull);
        y=fmodf(y,(float)H); if(y<0)y+=H;
        float tw=0.5f+0.5f*sinf((float)t*3.5f + i*1.3f);
        float k=(0.25f + 0.9f*tw*pulseAll)*A;
        // warm golden-green glow
        cv->add_glow(cv, x,y, 0.9f*warm,1.0f,0.35f*cool, k*0.5f, f.sz*3.0f);
        putAdd(fb,W,H,(int)x,(int)y, 1.0f,1.0f,0.6f, k);
    }

    // ================= PARROTS / birds darting across (on onset) =================
    float dash = 1.f + 2.5f*onset;   // birds surge on onset
    for(size_t i=0;i<s->birds.size();++i){
        Bird& b=s->birds[i];
        float margin=40.f, span=W+2.f*margin;
        float travel=fmodf(b.phase*span + (float)t*b.sp*dash, span);
        float x=(b.dir>0)? (travel-margin) : (W+margin-travel);
        float y=b.y + 26.f*sinf((float)t*0.9f + b.phase*6.28f);
        int dir=b.dir;
        // tropical color from hue
        float r,g,bl;
        if(b.hue<0.34f){ r=1.0f; g=0.25f; bl=0.15f; }       // red macaw
        else if(b.hue<0.67f){ r=1.0f; g=0.85f; bl=0.15f; }  // yellow
        else { r=0.15f; g=0.45f; bl=1.0f; }                 // blue
        float a=clampf(0.95f*A,0.f,1.f);
        float sz=5.f+2.f*hashf((uint32_t)i,90);
        // body
        ellipseOver(fb,W,H, x,y, sz, sz*0.55f, r,g,bl, a);
        // long tail
        float bkX=x - dir*sz*1.1f;
        lineOver(fb,W,H, bkX,y, bkX-dir*sz*1.6f, y+sz*0.3f, r*0.9f,g*0.9f,bl*0.9f, a, 1.2f);
        // flapping wing (analytic flap)
        float flap=sinf((float)t*16.f + b.phase*6.28f)*sz*0.9f;
        triOver(fb,W,H, x,y, x-dir*sz*0.3f, y-flap, x+dir*sz*0.3f, y-flap*0.6f, r,g,bl,a);
        // head + eye
        float hx=x+dir*sz*0.9f;
        ellipseOver(fb,W,H, hx,y-sz*0.15f, sz*0.5f,sz*0.5f, r,g,bl,a);
        putOver(fb,W,H,(int)(hx+dir*sz*0.25f),(int)(y-sz*0.25f), 0.02f,0.02f,0.02f,a);
        cv->add_glow(cv, x,y, r,g,bl, 0.12f*A, sz*1.2f);
    }

    // ================= FOREGROUND MONSTERA LEAF (bottom-right) =================
    // A big near-black-green silhouette leaf with characteristic split fronds,
    // gently swaying with mid.
    {
        float cx=W*0.82f, cy=H*0.86f;
        float lsw=0.06f*sinf((float)t*0.6f) + 0.05f*mid;
        float baseAng=-2.3f + lsw;     // pointing up-left
        float len=150.f, wid=95.f;
        float lr=0.05f, lg=0.16f, lb=0.05f;
        // draw as a big blade, then carve lobes by overdrawing fewer -- approximate
        // with several overlapping blades radiating from the stem for a lobed shape.
        for(int q=0;q<7;q++){
            float f=(q-3)/3.f;
            float a2=baseAng + f*0.55f;
            float ll=len*(1.f-0.18f*fabsf(f));
            float ca=cosf(baseAng), sa=sinf(baseAng);
            float ox=cx + ca*ll*0.55f, oy=cy + sa*ll*0.55f;
            leafBlade(fb,W,H, mixf(cx,ox,0.0f), mixf(cy,oy,0.0f), ll, wid*0.28f, a2, lr,lg,lb, 0.96f*A);
        }
        // central blade for body
        leafBlade(fb,W,H, cx,cy, len, wid, baseAng, lr,lg,lb, 0.9f*A);
        // stem
        float ca=cosf(baseAng), sa=sinf(baseAng);
        lineOver(fb,W,H, cx,cy, cx-ca*len*0.9f, cy-sa*len*0.9f, 0.06f,0.12f,0.05f, 0.9f, 3.f);
        // a faint rim light where god-rays graze it
        lineAdd(fb,W,H, cx-ca*len*0.8f,cy-sa*len*0.8f, cx+ca*len*0.2f,cy+sa*len*0.2f,
                0.10f*warm,0.16f,0.05f, 0.35f*A*(0.5f+bass), 1.2f);
    }
}
