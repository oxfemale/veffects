// dune.cpp -- "Dune" scene plugin.
// The desert planet Arrakis: rolling ochre SAND DUNES under a huge pale sky with
// TWO MOONS, a distant sandstorm haze on the horizon, shimmering SPICE glitter in
// the dunes, a tiny ornithopter, and a colossal SANDWORM breaching from the sand
// with a round ringed maw of teeth and cascading sand -- summoned higher by the
// rhythm.
//
// All animation is derived analytically from the absolute time p->time and the
// per-frame audio, so any frame renders correctly without simulation history
// (offline render and seeking both work). The player tone-maps the additive HDR
// buffer (bloom/shake/grain), so we lay a full sky+sand base every frame. All
// output is scaled by p->alpha.

#include "veffects_plugin.h"
#include <cmath>
#include <cstdint>
#include <vector>

static inline float clampf(float x, float a, float b){ return x<a?a:(x>b?b:x); }
static inline float lerpf(float a, float b, float t){ return a+(b-a)*t; }
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
// OPAQUE composite (fb = fb*(1-a) + rgb*a). Coverage a is scaled by scene alpha
// so at A=0 it leaves the buffer untouched. Used for solid shapes (dunes, worm,
// moons) so their warm colors read cleanly instead of blooming white.
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
// shaded filled disk (top-lit): lighter toward the top, darker toward the bottom.
static void discOver(float* fb, int W, int H, float cx, float cy, float rad,
                     float r, float g, float b, float a){
    int y0=(int)floorf(cy-rad), y1=(int)ceilf(cy+rad);
    for(int y=y0;y<=y1;y++){
        float dy=(y-cy)/rad; if(dy<-1.f||dy>1.f) continue;
        float ext=rad*sqrtf(1.f-dy*dy);
        float sh=clampf(0.72f - 0.42f*dy, 0.25f, 1.25f); // top brighter
        int xa=(int)floorf(cx-ext), xb=(int)ceilf(cx+ext);
        for(int x=xa;x<=xb;x++) putOver(fb,W,H,x,y, r*sh,g*sh,b*sh, a);
    }
}
static void lineOver(float* fb, int W, int H, float x0,float y0,float x1,float y1,
                     float r,float g,float b,float a,float wid){
    float dx=x1-x0, dy=y1-y0; float len=sqrtf(dx*dx+dy*dy); int n=(int)len+1;
    float hw=wid*0.5f; int rr=(int)ceilf(hw);
    for(int i=0;i<=n;i++){
        float t=(float)i/n; float px=x0+dx*t, py=y0+dy*t;
        for(int oy=-rr;oy<=rr;oy++) for(int ox=-rr;ox<=rr;ox++)
            if(ox*ox+oy*oy<=hw*hw) putOver(fb,W,H,(int)px+ox,(int)py+oy,r,g,b,a);
    }
}
static inline void bez(float p0x,float p0y,float p1x,float p1y,float p2x,float p2y,
                       float u,float& ox,float& oy){
    float iu=1.f-u;
    ox=iu*iu*p0x + 2.f*iu*u*p1x + u*u*p2x;
    oy=iu*iu*p0y + 2.f*iu*u*p1y + u*u*p2y;
}

struct State { int W, H; int horizon; };

static VfxPluginInfo INFO = {
    VFX_PLUGIN_ABI, "Dune", "veffects",
    "A colossal sandworm breaches from the rolling ochre dunes of Arrakis beneath twin moons, its rise summoned by the rhythm amid shimmering spice."
};
VFX_EXPORT const VfxPluginInfo* vfx_plugin_info(void){ return &INFO; }

VFX_EXPORT void* vfx_plugin_create(int W, int H){
    State* s = new State();          // non-NULL even though nearly stateless
    s->W=W; s->H=H; s->horizon=(int)(0.54f*H);
    return s;
}
VFX_EXPORT void vfx_plugin_destroy(void* st){ delete (State*)st; }

VFX_EXPORT void vfx_plugin_render(void* st, const VfxCanvas* cv, const VfxParams* p){
    State* s=(State*)st;
    float* fb=cv->fb; int W=cv->width, H=cv->height;
    float A=p->alpha;
    float bass=p->bass, mid=p->mid, tre=p->treble, rms=p->rms, beat=p->beat, onset=p->onset;
    double t=p->time;
    int hz=s->horizon;
    int NB=p->bandCount;
    const float* bands=p->bands;

    // ---- SKY: pale-ochre wash, deeper up top -> dustier/brighter at the horizon
    for(int y=0;y<hz;y++){
        float v=(float)y/hz;                       // 0 top .. 1 horizon
        float r=lerpf(0.60f, 0.82f, v);
        float g=lerpf(0.49f, 0.67f, v);
        float b=lerpf(0.33f, 0.45f, v);
        for(int x=0;x<W;x++) putAdd(fb,W,H,x,y, r,g,b, 0.9f*A);
    }

    // ---- TWO MOONS: a large pale one and a smaller amber one, slow analytic drift
    {
        float m1x=0.24f*W + 10.f*sinf((float)t*0.05f);
        float m1y=0.16f*H + 6.f*sinf((float)t*0.07f+1.f);
        cv->add_glow(cv, m1x,m1y, 0.95f,0.90f,0.78f, 0.42f*A, 74.f);
        discOver(fb,W,H, m1x,m1y, 34.f, 0.94f,0.90f,0.80f, clampf(0.96f*A,0.f,1.f));
        // faint crater mottling
        for(int c=0;c<5;c++){
            float a=hashf(c,7)*6.2831f, rr=hashf(c,8)*22.f;
            ellipseOver(fb,W,H, m1x+cosf(a)*rr, m1y+sinf(a)*rr, 4.f+hashf(c,9)*4.f,
                        3.f+hashf(c,10)*3.f, 0.80f,0.75f,0.66f, clampf(0.35f*A,0.f,1.f));
        }
        float m2x=0.62f*W + 8.f*sinf((float)t*0.045f+2.f);
        float m2y=0.11f*H + 5.f*sinf((float)t*0.06f);
        cv->add_glow(cv, m2x,m2y, 0.95f,0.72f,0.42f, 0.34f*A, 40.f);
        discOver(fb,W,H, m2x,m2y, 18.f, 0.93f,0.74f,0.48f, clampf(0.95f*A,0.f,1.f));
    }

    // ---- ORNITHOPTER: tiny dark dragonfly silhouette gliding across the sky
    {
        float span=W+160.f;
        float ox=fmodf((float)t*34.f, span)-80.f;
        float oy=0.30f*H + 22.f*sinf((float)t*0.8f);
        float flap=sinf((float)t*18.f)*7.f;
        float da=clampf(0.85f*A,0.f,1.f);
        ellipseOver(fb,W,H, ox,oy, 7.f,2.4f, 0.10f,0.07f,0.05f, da);          // fuselage
        lineOver(fb,W,H, ox-1,oy, ox-11,oy-6-flap, 0.10f,0.07f,0.05f, da,1.6f); // wings
        lineOver(fb,W,H, ox-1,oy, ox-11,oy+6+flap, 0.10f,0.07f,0.05f, da,1.6f);
        lineOver(fb,W,H, ox+3,oy, ox-6, oy-5-flap*0.8f, 0.10f,0.07f,0.05f, da,1.4f);
        lineOver(fb,W,H, ox+3,oy, ox-6, oy+5+flap*0.8f, 0.10f,0.07f,0.05f, da,1.4f);
    }

    // ---- SANDSTORM HAZE: dusty tan band smeared along the horizon, breathes w/ rms
    {
        float hazeI=(0.30f+0.85f*rms)*A;
        for(int y=hz-26;y<hz+18;y++){
            if(y<0||y>=H) continue;
            float d=fabsf((float)y-hz)/26.f; float fall=clampf(1.f-d,0.f,1.f);
            for(int x=0;x<W;x+=1){
                float w=0.6f+0.4f*sinf(x*0.02f - (float)t*0.6f);
                putAdd(fb,W,H,x,y, 0.86f,0.72f,0.50f, hazeI*fall*w*0.5f);
            }
        }
    }

    // ---- SAND BASE: warm ochre near the horizon deepening to rust in foreground
    for(int y=hz;y<H;y++){
        float u=(float)(y-hz)/(H-hz);
        float r=lerpf(0.76f, 0.60f, u);
        float g=lerpf(0.54f, 0.30f, u);
        float b=lerpf(0.31f, 0.13f, u);
        for(int x=0;x<W;x++) putAdd(fb,W,H,x,y, r,g,b, 0.9f*A);
    }

    // ---- DUNES: layered ochre ridgelines, far (hazy/pale) to near (rich rust).
    // Each layer is an opaque band from its crest down to the next layer's base,
    // so the union tiles the sand once (cheap) while crests overlap for depth.
    const int K=5;
    float aD=clampf(A,0.f,1.f);
    float baseY[K+1];
    for(int k=0;k<=K;k++) baseY[k]=hz + (0.015f + 0.108f*k)*H;
    for(int k=0;k<K;k++){
        float tn=(float)k/(K-1);                          // 0 far .. 1 near
        // crest (lit) and trough (shadow) colors interpolated far->near
        float cr[3]={ lerpf(0.80f,0.93f,tn), lerpf(0.66f,0.56f,tn), lerpf(0.48f,0.24f,tn) };
        float sh[3]={ lerpf(0.56f,0.42f,tn), lerpf(0.42f,0.21f,tn), lerpf(0.27f,0.10f,tn) };
        float amp=9.f + 20.f*tn;
        float freq=0.0075f + 0.0045f*tn;
        float phase=k*1.7f + (float)t*(0.6f+0.5f*tn);     // dunes drift slowly
        float floorY=baseY[k+1]; if(k==K-1) floorY=(float)H;
        for(int x=0;x<W;x++){
            // ridge silhouette: layered sines + a band-energy bump ("bands" drive height)
            float rg = 0.55f + 0.30f*sinf(x*freq + phase)
                             + 0.15f*sinf(x*freq*2.7f + phase*1.6f);
            float bump=0.f;
            if(NB>0){
                float fb2=(float)x/W*(NB-1);
                int bi=(int)fb2; float ff=fb2-bi;
                float be=bands[bi]*(1.f-ff) + bands[(bi+1<NB)?bi+1:bi]*ff;
                bump=be*(10.f+22.f*tn);
            }
            float crestY=baseY[k] - amp*rg - bump;
            int y0=(int)crestY; if(y0<hz-2) y0=hz-2;
            int y1=(int)floorY;  if(y1>H) y1=H;
            float span=(float)(y1-y0); if(span<1.f) span=1.f;
            for(int y=y0;y<y1;y++){
                float u=((float)y-crestY)/span;           // 0 crest .. 1 base
                float sm=clampf(u*1.6f,0.f,1.f);
                float r=lerpf(cr[0],sh[0],sm), g=lerpf(cr[1],sh[1],sm), b=lerpf(cr[2],sh[2],sm);
                putOver(fb,W,H,x,y, r,g,b, aD);
            }
            // sunlit crest highlight
            putOver(fb,W,H,x,y0, lerpf(0.92f,0.98f,tn), lerpf(0.82f,0.70f,tn),
                    lerpf(0.60f,0.38f,tn), clampf(0.6f*A,0.f,1.f));
        }
    }

    // ---- HEAT SHIMMER: faint wavering highlights above the dunes, driven by mid
    {
        float shim=(0.12f+0.9f*mid)*A;
        for(int i=0;i<70;i++){
            float x=hashf(i,31)*W;
            float baseYs=hz + 0.10f*H + hashf(i,32)*0.30f*H;
            float wob=sinf((float)t*3.0f + i*0.9f + x*0.03f);
            float y=baseYs + wob*(3.f+9.f*mid);
            putAdd(fb,W,H,(int)x,(int)y, 0.95f,0.80f,0.55f, shim*0.5f*(0.5f+0.5f*wob));
        }
    }

    // ---- SPICE: tiny cinnamon/orange sparkles glittering in the dunes (treble)
    {
        int NS=90;
        for(int i=0;i<NS;i++){
            float x=hashf(i,51)*W;
            float y=hz + 0.06f*H + hashf(i,52)*(0.9f*(H-hz));
            float ph=hashf(i,53)*6.2831f;
            float tw=0.5f+0.5f*sinf((float)t*(5.f+6.f*hashf(i,54)) + ph);
            float k=(0.05f + 0.9f*tre)*tw*tw*A;
            if(k<0.02f) continue;
            float hueR=lerpf(0.98f,0.72f,hashf(i,55));  // cinnamon..amber
            cv->add_glow(cv, x,y, hueR,0.45f,0.12f, k, 2.6f);
        }
    }

    // ---- SANDWORM: a colossal body arcing out of the sand, ringed maw of teeth,
    // sand cascading off. The rhythm SUMMONS it: bass/beat/onset drive rise height.
    {
        // smoothed "summons" envelope (p->beat & p->bass are already smoothed),
        // plus a slow breathing term so it always undulates a little.
        float summon=clampf(0.20f + 0.70f*bass + 0.85f*beat + 0.45f*onset
                            + 0.14f*sinf((float)t*0.5f), 0.f, 1.f);
        float cxBase=0.44f*W + 46.f*sinf((float)t*0.13f);      // slow lateral sway
        float apexY = hz - summon*0.50f*H;                     // higher w/ the beat
        // spine: tail buried in the sand -> head reared up at the apex
        float p0x=cxBase-70.f, p0y=hz+0.16f*H;                 // tail (in sand)
        float p2x=cxBase+52.f, p2y=apexY;                      // head tip (sky)
        float p1x=cxBase-6.f,  p1y=apexY-30.f;                 // control (over apex)

        // body: overlapping shaded discs, thick at the base tapering toward the head,
        // with darker segmentation rings for that armoured, ridged worm look.
        float Rbase=30.f + 6.f*rms;
        int NSEG=42;
        for(int i=0;i<=NSEG;i++){
            float u=(float)i/NSEG;
            float x,y; bez(p0x,p0y,p1x,p1y,p2x,p2y,u,x,y);
            float taper=1.f - 0.62f*u;
            float rad=Rbase*taper;
            float ring=0.78f + 0.22f*sinf(u*22.f);            // segmentation grooves
            float r=0.74f*ring, g=0.40f*ring, b=0.20f*ring;
            discOver(fb,W,H, x,y, rad, r,g,b, clampf(0.97f*A,0.f,1.f));
            // dorsal spice-dust sheen along the sunlit top edge
            if(i%2==0)
                putAdd(fb,W,H,(int)x,(int)(y-rad*0.7f), 0.9f,0.6f,0.3f, 0.25f*A);
        }

        // head/maw at the apex: concentric rings of a circular mouth full of teeth
        float hxp,hyp; bez(p0x,p0y,p1x,p1y,p2x,p2y,1.f,hxp,hyp);
        float Rm=Rbase*0.42f + 20.f;                          // maw outer radius
        discOver(fb,W,H, hxp,hyp, Rm, 0.62f,0.34f,0.18f, clampf(0.97f*A,0.f,1.f)); // lip
        // concentric flesh rings
        ellipseOver(fb,W,H, hxp,hyp, Rm*0.80f,Rm*0.80f, 0.50f,0.26f,0.14f, clampf(0.95f*A,0.f,1.f));
        ellipseOver(fb,W,H, hxp,hyp, Rm*0.58f,Rm*0.58f, 0.34f,0.16f,0.09f, clampf(0.95f*A,0.f,1.f));
        ellipseOver(fb,W,H, hxp,hyp, Rm*0.34f,Rm*0.34f, 0.10f,0.04f,0.03f, clampf(0.97f*A,0.f,1.f)); // dark throat
        cv->add_glow(cv, hxp,hyp, 0.9f,0.55f,0.25f, (0.3f+1.4f*beat)*A, Rm*0.9f); // maw glow, pulses
        // ring of pale teeth pointing inward toward the gullet
        int NT=16;
        for(int ti=0;ti<NT;ti++){
            float a=(float)ti/NT*6.2831f + (float)t*0.25f;
            float ca=cosf(a), sa=sinf(a);
            float ox0=hxp+ca*Rm*0.82f, oy0=hyp+sa*Rm*0.82f;   // outer
            float ix0=hxp+ca*Rm*0.46f, iy0=hyp+sa*Rm*0.46f;   // inner (tip)
            lineOver(fb,W,H, ox0,oy0, ix0,iy0, 0.94f,0.90f,0.80f, clampf(0.9f*A,0.f,1.f), 3.2f);
            putOver(fb,W,H,(int)ix0,(int)iy0, 1.0f,0.97f,0.9f, clampf(0.9f*A,0.f,1.f));
        }

        // CASCADING SAND: sheets sloughing off the worm's flanks and falling back
        int NC=120;
        for(int i=0;i<NC;i++){
            float u=hashf(i,61);
            float bx,by; bez(p0x,p0y,p1x,p1y,p2x,p2y,u,bx,by);
            float side=(hashf(i,62)<0.5f?-1.f:1.f);
            float rad=Rbase*(1.f-0.62f*u);
            bx += side*rad*(0.6f+0.4f*hashf(i,63));
            float ph=hashf(i,64);
            float fall=fmodf((float)t*(90.f+120.f*hashf(i,65)) + ph*400.f, 260.f);
            float x=bx + side*fall*0.10f;
            float y=by + fall;
            if(y<hz-40 || y>H) continue;
            float fade=clampf(1.f-fall/260.f,0.f,1.f);
            float k=(0.25f+0.7f*tre)*fade*A;
            putAdd(fb,W,H,(int)x,(int)y,   0.90f,0.68f,0.42f, k);
            putAdd(fb,W,H,(int)x,(int)y-1, 0.80f,0.58f,0.34f, k*0.6f);
        }
    }
}
