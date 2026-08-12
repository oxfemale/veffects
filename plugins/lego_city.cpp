// lego_city.cpp -- "Lego City" scene plugin.
// A bright, playful daytime LEGO world: a green studded baseplate, a skyline of
// stacked-brick buildings in primary LEGO colours with round studs on top and
// window "equalizer" grids that light up per spectrum band, a rolling brick car
// and a waving minifig, plus little bricks that rain and hop across the sky.
//
// Audio reactivity: buildings bounce and pop an extra brick on the beat/onset,
// brick/window lights pulse with bass and per-band energy (skyline equalizer),
// and small bricks rain/hop with treble.
//
// All animation is derived from the absolute time p->time, so any frame renders
// correctly without simulation history (offline render and seeking both work).

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
static inline void fillRect(float* fb, int W, int H, int x0,int y0,int x1,int y1,
                            float r,float g,float b,float k){
    if(x0>x1){int t=x0;x0=x1;x1=t;} if(y0>y1){int t=y0;y0=y1;y1=t;}
    if(x1<0||y1<0||x0>=W||y0>=H) return;
    if(x0<0)x0=0; if(y0<0)y0=0; if(x1>=W)x1=W-1; if(y1>=H)y1=H-1;
    for(int y=y0;y<=y1;y++) for(int x=x0;x<=x1;x++) putAdd(fb,W,H,x,y,r,g,b,k);
}
// filled disc with a soft top-left highlight -> reads as a shiny LEGO stud
static inline void fillDisc(float* fb, int W, int H, float cx,float cy,float rad,
                            float r,float g,float b,float k){
    int x0=(int)floorf(cx-rad), x1=(int)ceilf(cx+rad);
    int y0=(int)floorf(cy-rad), y1=(int)ceilf(cy+rad);
    float r2=rad*rad;
    for(int y=y0;y<=y1;y++) for(int x=x0;x<=x1;x++){
        float dx=x+0.5f-cx, dy=y+0.5f-cy; float d2=dx*dx+dy*dy;
        if(d2>r2) continue;
        float hl = clampf(1.f - (dx+dy)/(rad*2.2f), 0.f, 1.f); // brighter toward top-left
        float sh = 0.7f + 0.55f*hl;
        putAdd(fb,W,H,x,y, r*sh, g*sh, b*sh, k);
    }
}
static inline void lineAdd(float* fb, int W, int H, float x0,float y0,float x1,float y1,
                           float r,float g,float b,float k,float wdt){
    float dx=x1-x0, dy=y1-y0; float len=sqrtf(dx*dx+dy*dy);
    int n=(int)len+1; if(n<1)n=1; int hw=(int)(wdt*0.5f);
    for(int i=0;i<=n;i++){ float t=(float)i/n; int px=(int)(x0+dx*t), py=(int)(y0+dy*t);
        for(int oy=-hw;oy<=hw;oy++) for(int ox=-hw;ox<=hw;ox++)
            putAdd(fb,W,H,px+ox,py+oy,r,g,b,k); }
}

struct Bldg { float x; int w; int baseLayers; int colIdx; float phase; };
struct State {
    int W, H;
    int baseY;          // top of the green baseplate
    int layerH = 26;    // one LEGO brick layer height (px)
    std::vector<Bldg> bl;
};

// primary LEGO palette (linear-ish); index 0..4
static void legoColor(int i, float& r, float& g, float& b){
    switch(i&7){
        case 0: r=1.00f; g=0.10f; b=0.07f; break; // red
        case 1: r=1.00f; g=0.74f; b=0.06f; break; // yellow
        case 2: r=0.10f; g=0.34f; b=1.00f; break; // blue
        case 3: r=0.13f; g=0.80f; b=0.16f; break; // green
        case 4: r=1.00f; g=1.00f; b=1.00f; break; // white
        default:r=1.00f; g=0.45f; b=0.05f; break; // orange
    }
}

static VfxPluginInfo INFO = {
    VFX_PLUGIN_ABI, "Lego City", "veffects",
    "A cheerful daytime LEGO city of stacked-brick buildings with studs and window equalizers that bounce and light up to the music."
};
VFX_EXPORT const VfxPluginInfo* vfx_plugin_info(void){ return &INFO; }

VFX_EXPORT void* vfx_plugin_create(int W, int H){
    State* s = new State();
    s->W=W; s->H=H; s->baseY = (int)(H*0.80f);
    int N = 11;
    float span = (float)W / N;
    for(int i=0;i<N;i++){
        Bldg b;
        b.w = (int)(span*0.72f);
        b.x = i*span + (span - b.w)*0.5f;
        b.baseLayers = 2 + (int)(hashf(i,7)*5.99f);     // 2..7 bricks tall
        b.colIdx = (int)(hashf(i,3)*4.999f);            // red/yellow/blue/green/white
        b.phase = hashf(i,9)*6.2831853f;
        s->bl.push_back(b);
    }
    return s;
}
VFX_EXPORT void vfx_plugin_destroy(void* st){ delete (State*)st; }

// draw a stacked-brick building: filled body, layer seams, studs on top
static void drawBuilding(float* fb,int W,int H,int x,int w,int topY,int botY,int layerH,
                         float r,float g,float b,float K){
    fillRect(fb,W,H, x,topY, x+w-1,botY, r,g,b, 1.0f*K);
    // vertical shading: brighter left face, darker right -> chunky plastic look
    fillRect(fb,W,H, x,topY, x+(int)(w*0.32f),botY, 1,1,1, 0.05f*K);
    fillRect(fb,W,H, x+(int)(w*0.78f),topY, x+w-1,botY, 0,0,0, 0.0f); // (no-op subtract; keep additive)
    // horizontal brick seams (slightly brighter edge lines)
    for(int y=topY+layerH; y<botY; y+=layerH)
        for(int xx=x; xx<x+w; xx++) putAdd(fb,W,H,xx,y, 1,1,1, 0.06f*K);
    // studs across the top
    int sr = (int)(w*0.5f/3.0f);           // stud radius for ~3 studs
    if(sr<3)sr=3;
    int nst = (w) / (sr*2 + 2); if(nst<1)nst=1; if(nst>4)nst=4;
    float gap = (float)w / nst;
    for(int j=0;j<nst;j++){
        float scx = x + gap*(j+0.5f);
        float scy = topY - sr*0.55f;
        fillDisc(fb,W,H, scx,scy, (float)sr*0.9f, r,g,b, 0.85f*K);
    }
}

VFX_EXPORT void vfx_plugin_render(void* st, const VfxCanvas* cv, const VfxParams* p){
    State* s=(State*)st; float* fb=cv->fb; int W=cv->width, H=cv->height;
    float A=p->alpha;
    float bass=p->bass, tre=p->treble, rms=p->rms;
    float beat=p->beat, onset=p->onset; double t=p->time;
    int baseY=s->baseY, layerH=s->layerH;
    float K = 0.78f * A;                              // master brightness knob

    // ---- soft daytime sky (gradient, brighter near the horizon) ----
    for(int y=0;y<baseY;y++){
        float v=(float)y/baseY;
        float r=0.24f+0.34f*v, g=0.48f+0.32f*v, b=1.0f;
        float m=(0.34f+0.06f*rms);
        for(int x=0;x<W;x++) putAdd(fb,W,H,x,y, r,g,b, m*A);
    }
    // ---- sun (top-right) + gentle beat halo ----
    cv->add_glow(cv, W*0.84f, H*0.16f, 1.0f,0.95f,0.55f, (0.9f+0.6f*beat)*A, 46.f);
    cv->add_glow(cv, W*0.84f, H*0.16f, 1.0f,1.0f,0.85f, 1.1f*A, 20.f);
    // ---- drifting clouds (analytic from time) ----
    for(int c=0;c<4;c++){
        float cy=H*(0.12f+0.11f*c);
        float cx=fmodf((float)t*(6.f+c*3.f)+c*220.f, (float)W+160.f)-80.f;
        for(int k=0;k<4;k++)
            cv->add_glow(cv, cx+k*24.f-36.f, cy+(k&1)*8.f, 1,1,1, 0.35f*A, 20.f+(k==1||k==2?10.f:0.f));
    }

    // ---- green studded baseplate ----
    fillRect(fb,W,H, 0,baseY, W-1,H-1, 0.13f,0.72f,0.16f, 0.5f*A);
    fillRect(fb,W,H, 0,baseY, W-1,baseY+3, 1,1,1, 0.10f*A);   // bright front lip
    for(int gy=baseY+12; gy<H; gy+=26)
        for(int gx=14; gx<W; gx+=26)
            fillDisc(fb,W,H, (float)gx,(float)gy, 5.f, 0.16f,0.82f,0.20f, 0.55f*A);

    // ---- skyline of stacked-brick buildings ----
    int NB=p->bandCount;
    for(size_t i=0;i<s->bl.size();i++){
        Bldg& b=s->bl[i];
        // band energy driving this building's lights/equalizer
        float e = 0.f;
        if(NB>0){ int bi=(int)((float)i/s->bl.size()*NB); if(bi>=NB)bi=NB-1; e=p->bands[bi]; }
        // beat pop: an extra brick appears on strong beats/onset
        float pop = clampf(beat*1.1f + onset*0.7f, 0.f, 1.f);
        int extra = (int)(pop*2.f + e*1.5f);
        int layers = b.baseLayers + extra;
        // whole-building hop (bounce) synced to beat + own phase
        float bob = beat*7.f*(0.5f+0.5f*sinf((float)t*6.0f + b.phase));
        int botY = baseY - 1 - (int)bob;
        int topY = botY - layers*layerH;

        float r,g,bcol; legoColor(b.colIdx, r,g,bcol);
        float pulse = (0.82f + 0.5f*bass + 0.35f*e);     // bass/band brightness pulse
        drawBuilding(fb,W,H, (int)b.x, b.w, topY, botY, layerH, r,g,bcol, K*pulse);

        // window equalizer: grid of windows, lit from the bottom up by band energy
        int wc = (b.w>=40)?3:2;
        int wrows = layers;
        int mw=(int)(b.w*0.16f), mh=(int)(layerH*0.5f);
        float cellw=(float)b.w/wc;
        int litRows = (int)(e*wrows + 0.5f) + (int)(beat*1.5f);
        for(int ry=0; ry<wrows; ry++){
            int by = botY - (ry+1)*layerH + (layerH-mh)/2;
            bool lit = ry < litRows;
            for(int cx2=0; cx2<wc; cx2++){
                int wx=(int)(b.x + cellw*(cx2+0.5f) - mw/2);
                if(lit){
                    float wl = 0.9f + 0.7f*e;
                    fillRect(fb,W,H, wx,by, wx+mw,by+mh, 1.0f,0.92f,0.45f, 0.8f*K*wl);
                }else{
                    fillRect(fb,W,H, wx,by, wx+mw,by+mh, 0.12f,0.16f,0.24f, 0.35f*K);
                }
            }
        }
        // rooftop antenna light that blinks on the beat for tall buildings
        if(layers>=6){
            float tcx=b.x+b.w*0.5f;
            lineAdd(fb,W,H, tcx,(float)topY, tcx,(float)topY-14.f, 0.7f,0.7f,0.75f, 0.4f*K, 2.f);
            cv->add_glow(cv, tcx, topY-14.f, 1.0f,0.2f,0.15f, (0.4f+1.6f*beat)*A, 5.f);
        }
    }

    // ---- a rolling brick car on the baseplate ----
    {
        float cw=64, ch=20;
        float cx=fmodf((float)t*46.f, (float)W+140.f)-70.f;
        float cy=baseY-2 - ch;                    // sits on the plate
        float hop=fabsf(sinf((float)t*8.f))*4.f*beat;
        cy-=hop;
        // body (red) + cabin (white) + studs
        fillRect(fb,W,H, (int)cx,(int)cy, (int)(cx+cw),(int)(cy+ch), 1.0f,0.12f,0.08f, 0.6f*K);
        fillRect(fb,W,H, (int)(cx+cw*0.22f),(int)(cy-ch*0.7f), (int)(cx+cw*0.78f),(int)cy, 1,1,1, 0.55f*K);
        fillRect(fb,W,H, (int)(cx+cw*0.30f),(int)(cy-ch*0.55f),(int)(cx+cw*0.70f),(int)(cy-4), 0.3f,0.7f,1.0f, 0.5f*K); // window
        for(int j=0;j<3;j++) fillDisc(fb,W,H, cx+cw*(0.25f+0.25f*j), cy-ch*0.7f-3.f, 4.f, 1.0f,0.12f,0.08f, 0.8f*K);
        // black wheels with yellow hubs
        fillDisc(fb,W,H, cx+cw*0.24f, cy+ch, 8.f, 0.05f,0.05f,0.06f, 0.9f*K);
        fillDisc(fb,W,H, cx+cw*0.76f, cy+ch, 8.f, 0.05f,0.05f,0.06f, 0.9f*K);
        fillDisc(fb,W,H, cx+cw*0.24f, cy+ch, 3.f, 1.0f,0.8f,0.1f, 0.9f*K);
        fillDisc(fb,W,H, cx+cw*0.76f, cy+ch, 3.f, 1.0f,0.8f,0.1f, 0.9f*K);
    }

    // ---- a waving minifig standing on the plate ----
    {
        float mx=W*0.10f, feetY=baseY-2;
        float wave = sinf((float)t*3.f + beat*3.f);
        // legs (blue)
        fillRect(fb,W,H, (int)mx-6,(int)feetY-16, (int)mx-1,(int)feetY, 0.10f,0.34f,1.0f, 0.6f*K);
        fillRect(fb,W,H, (int)mx+1,(int)feetY-16, (int)mx+6,(int)feetY, 0.10f,0.34f,1.0f, 0.6f*K);
        // torso (red)
        fillRect(fb,W,H, (int)mx-8,(int)feetY-32, (int)mx+8,(int)feetY-15, 1.0f,0.12f,0.08f, 0.62f*K);
        // arms (yellow), one waving
        lineAdd(fb,W,H, mx-8,feetY-30, mx-15,feetY-24+wave*5.f, 1.0f,0.74f,0.06f, 0.6f*K, 3.f);
        lineAdd(fb,W,H, mx+8,feetY-30, mx+16,feetY-34-wave*6.f, 1.0f,0.74f,0.06f, 0.6f*K, 3.f);
        // head (yellow disc) + smile hint
        fillDisc(fb,W,H, mx, feetY-40, 8.f, 1.0f,0.78f,0.08f, 0.7f*K);
        putAdd(fb,W,H,(int)mx-3,(int)feetY-42, 0,0,0,0.f);
        cv->add_glow(cv, mx, feetY-40, 1.0f,0.8f,0.2f, 0.25f*A, 10.f);
    }

    // ---- little bricks that rain and hop with treble ----
    int NBrk = 26;
    float rainAmt = 0.35f + tre;                 // treble drives count/energy
    for(int i=0;i<NBrk;i++){
        float hx=hashf(i,101), hs=hashf(i,102), hc=hashf(i,103);
        if(hx > rainAmt*0.9f + 0.1f) continue;   // fewer bricks when treble is low
        float speed=40.f+hs*80.f;
        float travel=baseY+40.f;
        float bx = hx*W;
        float yy = fmodf(hashf(i,104)*travel + (float)t*speed*(0.6f+tre), travel) - 20.f;
        // hop: horizontal wiggle
        bx += sinf((float)t*6.f + i)*6.f*tre;
        int col=(int)(hc*4.999f); float r,g,b; legoColor(col, r,g,b);
        int bw=12, bh=8;
        fillRect(fb,W,H, (int)bx,(int)yy,(int)bx+bw,(int)yy+bh, r,g,b, 0.6f*K);
        fillDisc(fb,W,H, bx+3.5f, yy-2.f, 3.f, r,g,b, 0.75f*K);
        fillDisc(fb,W,H, bx+8.5f, yy-2.f, 3.f, r,g,b, 0.75f*K);
    }
}
