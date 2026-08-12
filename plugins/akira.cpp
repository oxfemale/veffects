// akira.cpp -- "Akira" (Neo-Tokyo) scene plugin.
// Kaneda's red motorcycle blazing across a neon Neo-Tokyo skyline trailing long
// red light-ribbons, speed lines down the road, and the iconic expanding
// white-red psychic shockwave with crackling energy tendrils and flying debris.
//
// All animation is derived from the absolute time p->time (analytic, no
// cross-frame state), so any frame renders correctly when seeking or rendering
// offline. Every contribution is multiplied by p->alpha and summed additively
// into the shared HDR buffer; the player tone-maps and adds bloom.

#include "veffects_plugin.h"
#include <cmath>
#include <cstdint>
#include <vector>

static inline float clampf(float x, float a, float b){ return x<a?a:(x>b?b:x); }
static inline float fracf(float x){ return x - floorf(x); }
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
// thick line built from parallel offsets, for glowing ribbons
static inline void ribbonAdd(float* fb, int W, int H, float x0,float y0,float x1,float y1,
                             float r,float g,float b,float k,float w){
    float dx=x1-x0, dy=y1-y0; float len=sqrtf(dx*dx+dy*dy); if(len<0.001f) len=0.001f;
    float nx=-dy/len, ny=dx/len;
    int hw=(int)(w*0.5f); if(hw<0)hw=0;
    for(int o=-hw;o<=hw;o++){
        float fall = 1.f - fabsf((float)o)/(hw+1.f);
        lineAdd(fb,W,H, x0+nx*o,y0+ny*o, x1+nx*o,y1+ny*o, r,g,b, k*fall);
    }
}

static VfxPluginInfo INFO = {
    VFX_PLUGIN_ABI, "Akira", "veffects",
    "Kaneda's red bike streaks across neon Neo-Tokyo trailing light-ribbons while the iconic white-red psychic shockwave explodes with crackling tendrils."
};
VFX_EXPORT const VfxPluginInfo* vfx_plugin_info(void){ return &INFO; }

// The scene is stateless (all animation is analytic from p->time), but the
// player disables any plugin whose create() returns NULL, so hand back a small
// owned allocation to keep the scene active.
struct State { int W, H; };
VFX_EXPORT void* vfx_plugin_create(int W, int H){ State* s=new State(); s->W=W; s->H=H; return s; }
VFX_EXPORT void  vfx_plugin_destroy(void* st){ delete (State*)st; }

VFX_EXPORT void vfx_plugin_render(void* st, const VfxCanvas* cv, const VfxParams* p){
    (void)st;
    float* fb = cv->fb; int W = cv->width, H = cv->height;
    float A = p->alpha;
    float bass=p->bass, mid=p->mid, tre=p->treble, rms=p->rms, beat=p->beat, onset=p->onset;
    double t=p->time;
    float horizon = H*0.52f;

    // ---------------------------------------------------------------------
    // 0) Night-city sky gradient: deep black up top warming to red/amber haze
    //    near the horizon. City glow intensity tracks rms.
    // ---------------------------------------------------------------------
    float glow = 0.5f + 0.9f*rms;
    for(int y=0;y<(int)horizon;y+=2){
        float gy = (float)y/horizon;              // 0 top .. 1 horizon
        float h  = gy*gy;                          // concentrate haze low
        float rr = 0.006f + 0.10f*h*glow;
        float gg = 0.003f + 0.030f*h*glow;
        float bb = 0.010f + 0.020f*h + 0.010f*(1.f-h); // faint electric-blue up high
        for(int x=0;x<W;x+=2) putAdd(fb,W,H,x,y, rr,gg,bb, A);
    }
    // dark reflective ground below horizon
    for(int y=(int)horizon;y<H;y+=2){
        float gy=(float)(y-horizon)/(H-horizon);
        float m=0.010f+0.020f*(1.f-gy);
        for(int x=0;x<W;x+=2) putAdd(fb,W,H,x,y, m*1.2f, m*0.4f, m*0.25f, A);
    }

    // ---------------------------------------------------------------------
    // 1) Neo-Tokyo skyline: procedural towers with neon window lights.
    //    p->bands drive per-building light banks. Buildings parallax-scroll.
    // ---------------------------------------------------------------------
    int NB = p->bandCount;
    float scroll = (float)t*22.f*(0.6f+0.8f*bass);   // city rushes by with bass
    int nB = 26;
    for(int b=0;b<nB;b++){
        uint32_t sd = hashu((uint32_t)b*2654435761u);
        float bw = 20.f + hashf(sd,1)*34.f;
        float bh = 40.f + hashf(sd,2)*160.f;
        float spanX = W + 120.f;
        float bx = fmodf(b*(spanX/nB) - scroll*(0.4f+0.6f*hashf(sd,7)), spanX) ; // parallax
        if(bx < -80.f) bx += spanX;
        float top = horizon - bh;
        // silhouette (dark, faint edge)
        float edge = 0.05f + 0.05f*hashf(sd,3);
        for(int y=(int)top;y<(int)horizon;y+=1){
            putAdd(fb,W,H,(int)bx, y, 0.20f,0.05f,0.10f, edge*0.5f*A);
            putAdd(fb,W,H,(int)(bx+bw), y, 0.10f,0.10f,0.22f, edge*0.5f*A);
        }
        // fill a hair of dark body so towers read as solid black masses
        for(int y=(int)top;y<(int)horizon;y+=3)
            for(int x=(int)bx;x<(int)(bx+bw);x+=3)
                putAdd(fb,W,H,x,y, 0.01f,0.004f,0.012f, A);
        // neon window grid, lit by a spectrum band
        float band = (NB>0)? p->bands[(b*7+3)%NB] : 0.4f;
        int cols=(int)(bw/6.f), rows=(int)(bh/8.f);
        // pick a neon hue per building: mostly red/amber, occasional blue
        float pick = hashf(sd,9);
        float wr,wg,wb;
        if(pick>0.82f){ wr=0.25f; wg=0.55f; wb=1.0f; }        // electric blue accent
        else if(pick>0.5f){ wr=1.0f; wg=0.45f; wb=0.08f; }    // amber
        else { wr=1.0f; wg=0.12f; wb=0.10f; }                 // hot red
        for(int cxi=0;cxi<cols;cxi++)
            for(int ry=0;ry<rows;ry++){
                uint32_t ws=hashu(sd ^ (uint32_t)(cxi*131+ry*977));
                float on = hashf(ws,1);
                // windows flicker with band energy + slow time pulse
                float lit = on*0.6f + band*0.8f + 0.2f*sinf((float)t*1.3f+ws*0.001f);
                if(lit < 0.75f) continue;
                int wx=(int)(bx+3+cxi*6), wy=(int)(top+4+ry*8);
                float k=(0.25f+0.7f*band)*A;
                putAdd(fb,W,H,wx,wy, wr,wg,wb, k);
                putAdd(fb,W,H,wx+1,wy, wr,wg,wb, k*0.6f);
            }
        // rooftop beacon
        if(hashf(sd,11)>0.6f){
            float bl=0.4f+0.6f*fabsf(sinf((float)t*2.f+sd*0.01f));
            cv->add_glow(cv, bx+bw*0.5f, top, 1.0f,0.2f,0.15f, 0.5f*bl*A, 4.f);
        }
    }

    // ---------------------------------------------------------------------
    // 2) The road: perspective speed-lines rushing toward the viewer.
    //    Lines lengthen / speed up with bass. A vanishing point sits at center.
    // ---------------------------------------------------------------------
    float vpx = W*0.5f, vpy = horizon;
    int nLanes = 22;
    float speed = (float)t*(1.2f+2.2f*bass);
    for(int i=0;i<nLanes;i++){
        float lane = (i/(float)(nLanes-1))*2.f - 1.f;       // -1..1 across
        // streak advances from horizon toward bottom, looping
        float ph = fracf(hashf((uint32_t)i,5) + speed*0.5f);
        float z0 = ph;                    // 0 near horizon .. 1 near camera
        float z1 = clampf(ph + 0.08f + 0.10f*bass, 0.f, 1.f);
        auto road = [&](float ln, float z, float& sx, float& sy){
            float persp = z*z;                              // nonlinear spread
            sy = vpy + (H-vpy)*persp;
            sx = vpx + ln * (W*0.85f) * persp;
        };
        float x0,y0,x1,y1; road(lane,z0,x0,y0); road(lane,z1,x1,y1);
        float br = (0.12f+0.5f*z0)*(0.6f+0.8f*bass);
        // amber road lighting with red edges
        ribbonAdd(fb,W,H, x0,y0,x1,y1, 1.0f,0.55f,0.12f, br*A, 1.f+2.f*z0);
    }
    // roadside light strips converging to vanishing point
    for(int s=-1;s<=1;s+=2){
        for(int seg=0;seg<10;seg++){
            float ph=fracf(seg*0.1f + speed*0.5f);
            float z0=ph, z1=clampf(ph+0.05f,0,1);
            auto edge=[&](float z,float& sx,float& sy){ float pp=z*z; sy=vpy+(H-vpy)*pp; sx=vpx+ s*0.98f*(W*0.85f)*pp; };
            float ax,ay,bx2,by2; edge(z0,ax,ay); edge(z1,bx2,by2);
            lineAdd(fb,W,H, ax,ay,bx2,by2, 1.0f,0.7f,0.2f, (0.2f+0.6f*z0)*A);
        }
    }

    // ---------------------------------------------------------------------
    // 3) Kaneda's red motorcycle: sweeps across the frame trailing long red
    //    light-ribbons. Horizontal position is analytic from time; trail length
    //    grows with bass. Bike bobs slightly and rides just above the road.
    // ---------------------------------------------------------------------
    float sweep = fracf((float)t*0.11f);                    // 0..1 across screen loop
    float dir = (fmodf((float)t*0.11f,2.f) < 1.f) ? 1.f : -1.f; // alternate direction
    float bx = (dir>0)? (-60.f + sweep*(W+120.f)) : (W+60.f - sweep*(W+120.f));
    float by = horizon + 34.f + 10.f*sinf((float)t*3.1f) + 12.f*bass;
    float trailLen = 90.f + 320.f*bass;                     // ribbons lengthen with bass
    // long trailing light-ribbons behind the bike (opposite of travel dir)
    int TR=26;
    for(int q=TR;q>=1;q--){
        float f0=(float)q/TR, f1=(float)(q-1)/TR;
        float tx0 = bx - dir*trailLen*f0, tx1 = bx - dir*trailLen*f1;
        float ty0 = by + 4.f*sinf((float)t*6.f + q*0.5f);
        float ty1 = by + 4.f*sinf((float)t*6.f + (q-1)*0.5f);
        float fade = f1;                                    // brighter near bike
        float k = (0.10f + 0.9f*(1.f-fade))*A;
        float wdt = 2.f + 7.f*(1.f-fade);
        ribbonAdd(fb,W,H, tx0,ty0,tx1,ty1, 1.0f,0.10f,0.06f, k*1.4f, wdt);
        // amber inner core near the bike
        if((1.f-fade)>0.6f)
            ribbonAdd(fb,W,H, tx0,ty0,tx1,ty1, 1.0f,0.5f,0.15f, k, wdt*0.4f);
    }
    // the bike itself: bright hot body + headlight glow + wheel sparks
    cv->add_glow(cv, bx, by, 1.0f,0.15f,0.10f, (1.4f+1.2f*rms)*A, 10.f);
    cv->add_glow(cv, bx+dir*14.f, by-3.f, 1.0f,0.9f,0.7f, (0.9f+0.8f*beat)*A, 5.f); // headlight
    // chassis strokes
    ribbonAdd(fb,W,H, bx-dir*16.f,by, bx+dir*16.f,by-2.f, 1.0f,0.2f,0.12f, 1.6f*A, 6.f);
    ribbonAdd(fb,W,H, bx-dir*10.f,by+4.f, bx+dir*12.f,by+4.f, 1.0f,0.35f,0.1f, 1.0f*A, 3.f);
    // headlight beam cone forward
    lineAdd(fb,W,H, bx+dir*16.f,by-2.f, bx+dir*70.f,by-18.f, 1.0f,0.85f,0.55f, 0.35f*A);
    lineAdd(fb,W,H, bx+dir*16.f,by+2.f, bx+dir*70.f,by+14.f, 1.0f,0.85f,0.55f, 0.35f*A);

    // ---------------------------------------------------------------------
    // 4) The iconic Akira psychic blast: an expanding sphere of white-red
    //    energy with a shockwave ring, crackling tendrils, and debris.
    //    It pulses/expands on beat & onset; tendrils shimmer with treble.
    // ---------------------------------------------------------------------
    float ecx = W*0.36f, ecy = horizon - 40.f;
    // explosion "age": each beat re-ignites a fresh cycle. Use a slow analytic
    // cycle modulated by beat so it both breathes and punches on hits.
    float cycle = fracf((float)t*0.16f);                    // 0..1 slow blast cycle
    float blast = clampf(0.35f + 1.1f*beat + 0.8f*onset, 0.f, 1.6f);
    float coreR = 8.f + 70.f*cycle + 40.f*blast;            // growing core radius
    float ringR = 20.f + 230.f*cycle;                       // shockwave expands outward
    float ringFade = 1.f - cycle;                           // ring dims as it grows

    // white-hot core sphere (kept hot-red weighted so detail survives bloom)
    cv->add_glow(cv, ecx, ecy, 1.0f,0.75f,0.55f, (1.0f+0.9f*blast)*A, coreR*0.42f);
    cv->add_glow(cv, ecx, ecy, 1.0f,0.30f,0.14f, (1.0f+0.9f*blast)*A, coreR);
    // filled bright core disc so bloom has something solid to grab
    int cr=(int)(coreR*0.42f);
    for(int y=-cr;y<=cr;y++)
        for(int x=-cr;x<=cr;x++){
            float d=sqrtf((float)(x*x+y*y)); if(d>cr) continue;
            float f=1.f-d/(cr+1.f);
            putAdd(fb,W,H, (int)ecx+x,(int)ecy+y, 1.0f,0.5f+0.35f*f,0.28f+0.4f*f, (0.35f+0.8f*blast)*f*f*A);
        }
    // expanding shockwave ring (bright rim, crackly)
    {
        int steps=140;
        for(int i=0;i<steps;i++){
            float a=(float)i/steps*6.2831853f;
            float wob = 1.f + 0.10f*sinf(a*7.f + (float)t*5.f) + 0.06f*sinf(a*13.f - (float)t*8.f);
            float rr=ringR*wob;
            float x=ecx+cosf(a)*rr, y=ecy+sinf(a)*rr*0.9f;
            float k=(0.4f+1.2f*ringFade)*(0.8f+0.6f*blast)*A;
            putAdd(fb,W,H,(int)x,(int)y, 1.0f,0.45f,0.25f, k);
            putAdd(fb,W,H,(int)x,(int)(y+1), 1.0f,0.8f,0.6f, k*0.5f);
        }
    }

    // ---------------------------------------------------------------------
    // 5) Crackling energy tendrils / lightning arcs radiating from the core.
    //    Count & jitter scale with treble; they reach out to ~ringR.
    // ---------------------------------------------------------------------
    int nTend = 10 + (int)(tre*18.f);
    for(int i=0;i<nTend;i++){
        uint32_t sd=hashu((uint32_t)i*40503u ^ (uint32_t)(t*3.0));
        float baseA = (float)i/nTend*6.2831853f + (float)t*0.7f;
        float reach = ringR*(0.5f+0.6f*hashf(sd,1));
        int segs=6;
        float px=ecx, py=ecy;
        float ang=baseA;
        for(int s=0;s<segs;s++){
            float step=reach/segs;
            ang += (hashf(sd,(uint32_t)(s+ (int)(t*9.0)))-0.5f)*(1.2f+2.5f*tre);
            float nx=px+cosf(ang)*step;
            float ny=py+sinf(ang)*step*0.92f;
            float k=(0.5f+1.3f*tre)*(1.f-(float)s/segs)*A;
            // electric-blue tinted white crackle
            lineAdd(fb,W,H, px,py,nx,ny, 0.8f,0.9f,1.0f, k);
            lineAdd(fb,W,H, px,py,nx,ny, 1.0f,0.5f,0.3f, k*0.5f);
            px=nx; py=ny;
        }
        cv->add_glow(cv, px,py, 0.7f,0.85f,1.0f, 0.4f*tre*A, 2.5f);
    }

    // ---------------------------------------------------------------------
    // 6) Flying debris + red streak trails blasted out from the explosion,
    //    speeding up on onset. Positions analytic from a per-particle seed.
    // ---------------------------------------------------------------------
    int nDeb = 60;
    float dt2 = cycle;                                       // shared expansion phase
    for(int i=0;i<nDeb;i++){
        uint32_t sd=hashu((uint32_t)i*668265263u);
        float a=hashf(sd,1)*6.2831853f;
        float sp=40.f+hashf(sd,2)*260.f;
        float life=fracf(hashf(sd,3)+ (float)t*0.5f);        // each debris loops
        float rr=life*sp*(1.f+0.6f*onset);
        float x=ecx+cosf(a)*rr, y=ecy+sinf(a)*rr*0.85f - life*20.f; // slight upward
        float k=(1.f-life)*(0.6f+0.8f*bass)*A;
        // streak trail
        float tx=ecx+cosf(a)*(rr-14.f), ty=ecy+sinf(a)*(rr-14.f)*0.85f - life*20.f;
        lineAdd(fb,W,H, tx,ty, x,y, 1.0f,0.25f,0.12f, k*0.8f);
        putAdd(fb,W,H,(int)x,(int)y, 1.0f,0.6f,0.4f, k*1.5f);
    }

    // ---------------------------------------------------------------------
    // 7) Beat-driven horizontal speed lines sweeping across whole frame for
    //    that sense of blazing motion (subtle, additive).
    // ---------------------------------------------------------------------
    int nSpeed=10;
    for(int i=0;i<nSpeed;i++){
        float yy=fracf(hashf((uint32_t)i,17)+ (float)t*0.8f)*H;
        float len=60.f+300.f*bass;
        float sx=fracf(hashf((uint32_t)i,18)*1.3f + (float)t*(0.5f+bass))*W;
        float k=(0.05f+0.25f*bass)*A;
        lineAdd(fb,W,H, sx,yy, sx+len,yy, 1.0f,0.4f,0.2f, k);
    }
}
