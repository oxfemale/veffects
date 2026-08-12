// naruto.cpp -- "Naruto" scene plugin.
// A stylized Naruto-anime world: a spinning blue Rasengan chakra sphere at the
// center of a Hidden Leaf (Uzumaki) spiral, drifting leaves, spinning shuriken
// and kunai silhouettes, chakra wind-lines and a warm village-sky glow.
//
// All animation is derived from the absolute time p->time so every frame renders
// correctly without simulation history (offline render and seeking both work).

#include "veffects_plugin.h"
#include <cmath>
#include <cstdint>
#include <cstddef>

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
                           float r,float g,float b,float k,float w){
    float dx=x1-x0, dy=y1-y0; float len=sqrtf(dx*dx+dy*dy);
    int n=(int)len+1; if(n<1)n=1;
    int wr = (int)(w*0.5f);
    for(int i=0;i<=n;i++){
        float t=(float)i/n;
        float px = x0+dx*t, py = y0+dy*t;
        if(wr<=0){ putAdd(fb,W,H,(int)px,(int)py,r,g,b,k); }
        else {
            // thin perpendicular smear for a bit of width
            float nx = -dy/len, ny = dx/len;
            for(int o=-wr;o<=wr;o++){
                float f = 1.f - 0.6f*(fabsf((float)o)/(wr+1));
                putAdd(fb,W,H,(int)(px+nx*o),(int)(py+ny*o),r,g,b,k*f);
            }
        }
    }
}

struct State { int W, H; };

static VfxPluginInfo INFO = {
    VFX_PLUGIN_ABI, "Naruto", "veffects",
    "A spinning blue Rasengan chakra sphere inside a Hidden Leaf spiral, with drifting leaves, spinning shuriken and warm village-sky glow reacting to the music."
};
VFX_EXPORT const VfxPluginInfo* vfx_plugin_info(void){ return &INFO; }

VFX_EXPORT void* vfx_plugin_create(int W, int H){ State* s=new State(); s->W=W; s->H=H; return s; }
VFX_EXPORT void vfx_plugin_destroy(void* st){ delete (State*)st; }

// Draw a small four-point shuriken silhouette (spinning) centered at (cx,cy).
static void drawShuriken(float* fb, int W, int H, float cx, float cy, float rad,
                         float ang, float r,float g,float b, float k){
    for(int blade=0; blade<4; blade++){
        float a = ang + blade*1.5708f;
        float tipx = cx + cosf(a)*rad, tipy = cy + sinf(a)*rad;
        float la = a + 0.45f, ra = a - 0.45f;
        float ir = rad*0.32f;
        float lx = cx + cosf(la)*ir, ly = cy + sinf(la)*ir;
        float rx = cx + cosf(ra)*ir, ry = cy + sinf(ra)*ir;
        lineAdd(fb,W,H, cx,cy, tipx,tipy, r,g,b, k*1.1f, 2.f);
        lineAdd(fb,W,H, lx,ly, tipx,tipy, r,g,b, k*0.8f, 1.f);
        lineAdd(fb,W,H, rx,ry, tipx,tipy, r,g,b, k*0.8f, 1.f);
    }
}

// Draw a spinning kunai silhouette: a blade line with a ring pommel.
static void drawKunai(float* fb, int W, int H, float cx, float cy, float len,
                      float ang, float r,float g,float b, float k){
    float dx=cosf(ang), dy=sinf(ang);
    float tipx=cx+dx*len*0.6f, tipy=cy+dy*len*0.6f;
    float bx =cx-dx*len*0.4f, by =cy-dy*len*0.4f;
    lineAdd(fb,W,H, bx,by, tipx,tipy, r,g,b, k, 2.f);
    // small cross guard
    float px=-dy, py=dx; float gw=len*0.10f;
    float gx0=cx-dx*len*0.22f, gy0=cy-dy*len*0.22f;
    lineAdd(fb,W,H, gx0-px*gw,gy0-py*gw, gx0+px*gw,gy0+py*gw, r,g,b, k, 1.f);
    // ring pommel
    for(int i=0;i<8;i++){
        float a0=i/8.f*6.2832f, a1=(i+1)/8.f*6.2832f;
        float rr=len*0.10f;
        lineAdd(fb,W,H, bx+cosf(a0)*rr,by+sinf(a0)*rr, bx+cosf(a1)*rr,by+sinf(a1)*rr, r,g,b,k*0.8f,1.f);
    }
}

// A stylized leaf (Konoha) drawn as two arcs meeting at tip and stem.
static void drawLeaf(float* fb, int W, int H, float cx, float cy, float size,
                     float ang, float r,float g,float b, float k){
    float ca=cosf(ang), sa=sinf(ang);
    float tipx=cx+ca*size, tipy=cy+sa*size;
    float stemx=cx-ca*size, stemy=cy-sa*size;
    float px=-sa, py=ca;
    // two curved sides via midpoints bulged out
    float bulge=size*0.5f;
    float m1x=cx+px*bulge, m1y=cy+py*bulge;
    float m2x=cx-px*bulge, m2y=cy-py*bulge;
    lineAdd(fb,W,H, stemx,stemy, m1x,m1y, r,g,b,k,1.f);
    lineAdd(fb,W,H, m1x,m1y, tipx,tipy, r,g,b,k,1.f);
    lineAdd(fb,W,H, stemx,stemy, m2x,m2y, r,g,b,k,1.f);
    lineAdd(fb,W,H, m2x,m2y, tipx,tipy, r,g,b,k,1.f);
    lineAdd(fb,W,H, stemx,stemy, tipx,tipy, r,g,b,k*0.6f,1.f); // central vein
}

VFX_EXPORT void vfx_plugin_render(void* st, const VfxCanvas* cv, const VfxParams* p){
    State* s=(State*)st;
    float* fb=cv->fb; int W=cv->width, H=cv->height;
    float A=p->alpha;
    float bass=p->bass, mid=p->mid, tre=p->treble, rms=p->rms;
    float beat=p->beat, onset=p->onset, cent=clampf(p->centroid,0.f,1.f);
    double t=p->time;
    float cx=W*0.5f, cy=H*0.48f;

    // centroid shifts the overall hue orange(0)<->blue(1)
    float blue = clampf(0.35f + 0.5f*cent, 0.f, 1.f);
    float orange = 1.f - blue;

    // pulse envelope on onset for the Rasengan
    float pulse = clampf(beat*0.8f + onset*1.4f, 0.f, 1.f);

    // ---- 1) warm village-sky glow background (sparse gradient) ----
    for(int y=0;y<H;y+=2){
        float gy=(float)y/H;                       // 0 top .. 1 bottom
        float horizon = 1.f - gy;                  // brighter near lower sky
        float warm = 0.006f + 0.020f*horizon*horizon*(0.6f+0.6f*rms);
        // sky: orange low, deep blue high
        float r = (0.9f*orange + 0.15f)*warm;
        float g = (0.45f*orange + 0.10f)*warm;
        float b = (0.20f + 0.9f*blue*gy)*warm;
        for(int x=0;x<W;x+=2) putAdd(fb,W,H,x,y, r,g,b, A);
    }
    // soft sun/village glow low center
    cv->add_glow(cv, cx, H*0.86f, 1.0f*orange+0.2f, 0.55f*orange+0.12f, 0.12f,
                 (0.7f+0.8f*rms)*A, 200.f);

    // ---- 2) Hidden Leaf / Uzumaki spiral emblem (background, slow) ----
    {
        float rot = (float)t*0.25f;
        float Rmax = fminf(W,H)*0.46f;
        int turns=3; int steps=turns*70;
        float ecol_r=0.2f, ecol_g=0.45f+0.4f*orange, ecol_b=0.9f;
        for(int i=0;i<steps;i++){
            float u=(float)i/steps;
            float ang = rot + u*turns*6.2832f;
            float rad = Rmax*(0.15f + 0.85f*u);
            float x=cx+cosf(ang)*rad, y=cy+sinf(ang)*rad;
            float ang2 = rot + (u+1.f/steps)*turns*6.2832f;
            float rad2 = Rmax*(0.15f + 0.85f*(u+1.f/steps));
            float x2=cx+cosf(ang2)*rad2, y2=cy+sinf(ang2)*rad2;
            float k=(0.05f+0.10f*(1.f-u))*(0.7f+0.5f*mid)*A;
            lineAdd(fb,W,H, x,y, x2,y2, ecol_r*0.6f, ecol_g, ecol_b, k, 2.f);
        }
    }

    // ---- 3) radiating chakra spokes driven by bands ----
    int NB=p->bandCount;
    if(NB>0){
        int spokes = NB<32? NB : 32;
        for(int i=0;i<spokes;i++){
            float e=p->bands[(size_t)i*NB/spokes];
            float a = (float)t*0.5f + i*6.2832f/spokes;
            float r0 = 70.f + 30.f*bass;
            float r1 = r0 + e*e*160.f*(0.6f+bass);
            float br=(0.15f+0.6f*e);
            lineAdd(fb,W,H, cx+cosf(a)*r0, cy+sinf(a)*r0,
                            cx+cosf(a)*r1, cy+sinf(a)*r1,
                            0.2f+0.7f*orange, 0.55f, 0.9f*blue+0.3f, br*A, 2.f);
        }
    }

    // ---- 4) Rasengan: rotating spiral energy strands + bright core ----
    {
        float grow = 1.f + 0.55f*bass + 0.35f*pulse;
        float R = (44.f + 22.f*rms) * grow;
        float spin = (float)t*(2.2f + 4.5f*bass + 3.0f*beat);
        int strands=6;
        for(int sN=0; sN<strands; sN++){
            float base = spin + sN*6.2832f/strands;
            int seg=48;
            float prevx=0,prevy=0; bool have=false;
            for(int i=0;i<=seg;i++){
                float u=(float)i/seg;                 // 0 core .. 1 rim
                float ang = base + u*6.2832f*2.2f;    // wrap-around swirl
                float rad = R*(0.12f + 0.9f*u);
                // squash to give spherical look
                float x=cx+cosf(ang)*rad;
                float y=cy+sinf(ang)*rad*0.82f;
                if(have){
                    float glow=(0.9f-0.5f*u);
                    float k=(0.16f+0.30f*glow)*(0.7f+0.7f*(mid+rms))*A;
                    // chakra blue with a touch of white toward the core
                    float wcore=clampf(1.f-u*1.6f,0.f,1.f);
                    lineAdd(fb,W,H, prevx,prevy, x,y,
                            0.35f+0.55f*wcore, 0.6f+0.4f*wcore, 1.0f, k, 2.f);
                }
                prevx=x; prevy=y; have=true;
            }
        }
        // counter-rotating inner swirl
        for(int sN=0; sN<strands; sN++){
            float base = -spin*1.3f + sN*6.2832f/strands;
            int seg=32;
            float prevx=0,prevy=0; bool have=false;
            for(int i=0;i<=seg;i++){
                float u=(float)i/seg;
                float ang=base + u*6.2832f*1.6f;
                float rad=R*0.6f*(0.1f+0.9f*u);
                float x=cx+cosf(ang)*rad, y=cy+sinf(ang)*rad*0.82f;
                if(have){
                    float k=(0.08f+0.18f*(1.f-u))*(0.7f+0.6f*mid)*A;
                    lineAdd(fb,W,H, prevx,prevy, x,y, 0.5f,0.75f,1.0f, k, 1.f);
                }
                prevx=x; prevy=y; have=true;
            }
        }
        // rim ring
        for(int i=0;i<40;i++){
            float a0=i/40.f*6.2832f, a1=(i+1)/40.f*6.2832f;
            lineAdd(fb,W,H, cx+cosf(a0)*R, cy+sinf(a0)*R*0.82f,
                            cx+cosf(a1)*R, cy+sinf(a1)*R*0.82f,
                            0.35f,0.6f,1.0f, (0.10f+0.25f*pulse)*A, 1.f);
        }
        // bright pulsing core (kept modest so the chakra swirl stays visible)
        float coreK=(1.1f+2.0f*pulse+0.9f*bass)*A;
        cv->add_glow(cv, cx, cy, 0.6f,0.8f,1.0f, coreK, R*0.5f);
        cv->add_glow(cv, cx, cy, 0.85f,0.93f,1.0f, coreK*1.1f, R*0.2f);
        cv->add_glow(cv, cx, cy, 1.0f,1.0f,1.0f, coreK*1.2f, 5.f+3.f*pulse);
        // outer chakra halo
        cv->add_glow(cv, cx, cy, 0.3f,0.55f,1.0f, (0.5f+1.2f*rms)*A, R*1.4f);
    }

    // ---- 5) chakra wind-lines / wisps (arc streaks around the sphere) ----
    {
        int nw=10;
        for(int i=0;i<nw;i++){
            float ph=hashf(i,7);
            float spd=0.6f+hashf(i,8)*1.2f;
            float a=(float)t*spd + ph*6.2832f;
            float rr=90.f + hashf(i,9)*120.f + 40.f*rms;
            float len=0.5f+0.8f*mid;
            float x0=cx+cosf(a)*rr, y0=cy+sinf(a)*rr*0.85f;
            float x1=cx+cosf(a+len)*rr, y1=cy+sinf(a+len)*rr*0.85f;
            lineAdd(fb,W,H, x0,y0, x1,y1, 0.4f+0.5f*orange,0.6f,1.0f, (0.10f+0.35f*mid)*A, 1.f);
        }
    }

    // ---- 6) drifting leaves (fly faster with treble) ----
    {
        int nleaf=28;
        float driftT=(float)t*(0.35f+1.6f*tre);
        for(int i=0;i<nleaf;i++){
            float sx=hashf(i,101), sy=hashf(i,102);
            float speed=0.4f+hashf(i,103)*0.9f;
            float sway=hashf(i,104)*6.2832f;
            // wrap horizontally as they drift right->left, fall gently
            float px = fmodf(sx*W - (driftT*speed*60.f), (float)W); if(px<0)px+=W;
            float py = fmodf(sy*H + (driftT*speed*22.f) + 30.f*sinf(driftT*0.7f+sway), (float)H);
            float ang = driftT*(1.5f+hashf(i,105)*2.f) + sway;
            float size=5.f+hashf(i,106)*6.f;
            float k=(0.22f+0.5f*tre)*A;
            drawLeaf(fb,W,H, px,py, size, ang, 0.95f, 0.5f+0.2f*orange, 0.08f, k);
        }
    }

    // ---- 7) spinning shuriken & kunai flying across ----
    {
        int nsh=4;
        for(int i=0;i<nsh;i++){
            float ph=hashf(i+50,201);
            float speed=0.5f+hashf(i+50,202)*0.7f;
            float prog=fmodf((float)t*speed*0.35f + ph, 1.f);
            // diagonal path
            float y0=hashf(i+50,203)*H;
            float px = -30.f + prog*(W+60.f);
            float py = y0 + (prog-0.5f)*H*0.5f*(hashf(i+50,204)-0.5f)*2.f;
            float spin=(float)t*(6.f+8.f*tre) + ph*10.f;
            float rad=10.f+hashf(i+50,205)*8.f;
            float k=(0.5f+0.6f*tre)*A;
            drawShuriken(fb,W,H, px,py, rad, spin, 0.25f,0.35f,0.55f, k);
        }
        int nk=2;
        for(int i=0;i<nk;i++){
            float ph=hashf(i+80,301);
            float speed=0.4f+hashf(i+80,302)*0.6f;
            float prog=fmodf((float)t*speed*0.3f + ph, 1.f);
            float x0=hashf(i+80,303)*W;
            float py = -30.f + prog*(H+60.f);
            float px = x0 + (prog-0.5f)*W*0.4f;
            float spin=(float)t*(3.f+4.f*tre) + ph*8.f;
            float len=26.f+hashf(i+80,304)*10.f;
            float k=(0.45f+0.5f*tre)*A;
            drawKunai(fb,W,H, px,py, len, spin, 0.3f,0.4f,0.6f, k);
        }
    }

    // ---- 8) beat flash spokes bursting from core on strong onset ----
    if(pulse>0.25f){
        int spk=12;
        float R0=44.f+22.f*rms;
        for(int i=0;i<spk;i++){
            float a=i*6.2832f/spk + (float)t*0.8f;
            float r1=R0*1.2f, r2=R0*(1.6f+2.0f*pulse);
            lineAdd(fb,W,H, cx+cosf(a)*r1, cy+sinf(a)*r1*0.82f,
                            cx+cosf(a)*r2, cy+sinf(a)*r2*0.82f,
                            0.6f,0.8f,1.0f, 0.5f*pulse*A, 2.f);
        }
    }
}
