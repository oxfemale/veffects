// dogs_river.cpp -- "Dogs by the River" scene plugin.
// A bright, wholesome daytime pastoral: a warm gradient sky with a pulsing sun
// and drifting fluffy clouds, a green rolling field of swaying grass, a winding
// blue river with shimmering ripples, and a few cute stylized dogs trotting past.
//
// All animation is derived analytically from the absolute time p->time, so any
// frame renders correctly without simulation history (offline render + seeking).
// The player tone-maps the additive HDR buffer (bloom/shake/grain), so we lay
// down a full bright sky/ground base every frame. All output is scaled by p->alpha.

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
// solid additive filled ellipse
static void fillEllipse(float* fb, int W, int H, float cx, float cy, float rx, float ry,
                        float r, float g, float b, float k){
    int y0=(int)floorf(cy-ry), y1=(int)ceilf(cy+ry);
    for(int y=y0;y<=y1;y++){
        float dy=(y-cy)/ry; if(dy<-1.f||dy>1.f) continue;
        float ext=rx*sqrtf(1.f-dy*dy);
        int xa=(int)floorf(cx-ext), xb=(int)ceilf(cx+ext);
        for(int x=xa;x<=xb;x++) putAdd(fb,W,H,x,y,r,g,b,k);
    }
}
// OPAQUE composite (fb = fb*(1-a) + rgb*a). Coverage a is scaled by scene alpha
// so at A=0 it leaves the buffer untouched (plays fair with scene crossfades).
// Used for the dogs so their coat colors read cleanly instead of blooming white.
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
// soft ground shadow: multiply the field darker under a dog
static void ellipseMul(float* fb, int W, int H, float cx, float cy, float rx, float ry, float m){
    int y0=(int)floorf(cy-ry), y1=(int)ceilf(cy+ry);
    for(int y=y0;y<=y1;y++){
        float dy=(y-cy)/ry; if(dy<-1.f||dy>1.f) continue;
        float ext=rx*sqrtf(1.f-dy*dy);
        int xa=(int)floorf(cx-ext), xb=(int)ceilf(cx+ext);
        for(int x=xa;x<=xb;x++){
            if((unsigned)x>=(unsigned)W||(unsigned)y>=(unsigned)H) continue;
            size_t i=((size_t)y*W+x)*3; fb[i]*=m; fb[i+1]*=m; fb[i+2]*=m;
        }
    }
}
// opaque thick line (rounded) for legs/tail
static void lineOver(float* fb, int W, int H, float x0,float y0,float x1,float y1,
                     float r,float g,float b,float a,float wid){
    float dx=x1-x0, dy=y1-y0; float len=sqrtf(dx*dx+dy*dy); int n=(int)len+1;
    float hw=wid*0.5f;
    for(int i=0;i<=n;i++){
        float t=(float)i/n; float px=x0+dx*t, py=y0+dy*t;
        int rr=(int)ceilf(hw);
        for(int oy=-rr;oy<=rr;oy++) for(int ox=-rr;ox<=rr;ox++)
            if(ox*ox+oy*oy<=hw*hw) putOver(fb,W,H,(int)px+ox,(int)py+oy,r,g,b,a);
    }
}

struct Dog {
    float x0;       // start offset along travel
    float speed;    // base px/sec
    float depth;    // 0 far (small/high) .. 1 near (big/low)
    float col[3];   // coat color
    float belly[3]; // lighter belly/chest
};
struct Blade { float x, y, len, hue, ph; };

struct State {
    int W, H;
    int horizon;
    std::vector<Dog> dogs;
    std::vector<Blade> grass;
};

static VfxPluginInfo INFO = {
    VFX_PLUGIN_ABI, "Dogs by the River", "veffects",
    "Cute dogs trot through a sunny riverside meadow of swaying grass under drifting clouds, all reacting to the music."
};
VFX_EXPORT const VfxPluginInfo* vfx_plugin_info(void){ return &INFO; }

VFX_EXPORT void* vfx_plugin_create(int W, int H){
    State* s = new State();
    s->W=W; s->H=H;
    s->horizon = (int)(0.46f*H);

    // a small pack of charming dogs, distinct warm coats (bright so they read
    // clearly against the additive green field)
    auto add=[&](float x0,float sp,float d,float r,float g,float b,
                 float br,float bg,float bb){
        Dog dg; dg.x0=x0; dg.speed=sp; dg.depth=d;
        dg.col[0]=r; dg.col[1]=g; dg.col[2]=b;
        dg.belly[0]=br; dg.belly[1]=bg; dg.belly[2]=bb;
        s->dogs.push_back(dg);
    };
    add(  40.f, 34.f, 0.30f, 0.86f,0.58f,0.20f, 0.96f,0.80f,0.52f); // golden retriever
    add( 260.f, 46.f, 0.62f, 0.45f,0.26f,0.13f, 0.64f,0.42f,0.24f); // chocolate
    add( 470.f, 40.f, 0.92f, 0.92f,0.86f,0.72f, 0.99f,0.95f,0.86f); // cream/white
    add( 650.f, 52.f, 0.48f, 0.74f,0.34f,0.20f, 0.90f,0.56f,0.40f); // rusty red

    // a few hundred short grass blades scattered across the foreground field
    int NB = 460;
    s->grass.reserve(NB);
    for(int i=0;i<NB;i++){
        Blade b;
        b.x = hashf(i,1)*W;
        float u = hashf(i,2);                 // 0 near river .. 1 bottom
        int gtop = s->horizon + (int)(0.20f*H);
        b.y = gtop + u*(H-gtop);
        b.len = 5.f + hashf(i,3)*9.f + u*8.f; // taller nearer camera
        b.hue = 0.24f + hashf(i,4)*0.06f;     // green variety
        b.ph  = hashf(i,5)*6.2831f;
        s->grass.push_back(b);
    }
    return s;
}
VFX_EXPORT void vfx_plugin_destroy(void* st){ delete (State*)st; }

// one stylized side-view dog facing right; motion analytic from t + audio
static void drawDog(const VfxCanvas* cv, float* fb, int W, int H,
                    const Dog& d, double t, float beat, float onset,
                    float treble, float A){
    // size / vertical placement from depth
    float size = 16.f + d.depth*22.f;
    float baseY = (0.66f + d.depth*0.26f) * H;      // body center Y
    // travel: faster with the beat; wraps across screen (+margins)
    float margin = 120.f;
    float range = W + 2.f*margin;
    float spd = d.speed * (1.f + 0.9f*beat + 1.4f*onset);
    float x = fmodf((float)(t*spd) + d.x0, range);
    if(x<0) x+=range;
    x -= margin;

    // trotting gait: leg phase tied to horizontal position (no foot sliding) +
    // a little audio-driven cadence; body bobs twice per stride
    float stride = x*0.10f + (float)t*(2.0f + 5.0f*beat);
    float bob = size*(0.10f + 0.10f*beat)*(0.5f + 0.5f*sinf(2.f*stride));
    float cy = baseY - bob;

    const float* c = d.col; const float* bl = d.belly;
    float aBody = clampf(0.92f*A, 0.f, 1.f);   // opaque coverage for coat
    float aSoft = clampf(0.80f*A, 0.f, 1.f);

    // soft ground shadow anchoring the dog
    ellipseMul(fb,W,H, x, baseY + size*1.15f, size*1.2f, size*0.30f, 1.f - 0.28f*A);

    // legs (draw behind body): four legs, diagonal trot pairing
    float frontHipX = x + size*0.55f;
    float backHipX  = x - size*0.55f;
    float hipY = cy + size*0.30f;
    float legLen = size*0.95f;
    float legW = size*0.22f;
    struct L{ float hx; float off; float kk; };
    L legs[4] = {
        { backHipX,  0.0f,        0.72f }, // back-far
        { frontHipX, 3.14159f,    0.72f }, // front-far
        { backHipX,  3.14159f,    1.00f }, // back-near
        { frontHipX, 0.0f,        1.00f }, // front-near
    };
    for(int i=0;i<4;i++){
        float sw = sinf(stride + legs[i].off);
        float lift = clampf(sinf(stride + legs[i].off), 0.f, 1.f); // raise on forward swing
        float footX = legs[i].hx + sw*size*0.45f;
        float footY = hipY + legLen - lift*size*0.35f;
        float kk = legs[i].kk;
        lineOver(fb,W,H, legs[i].hx, hipY, footX, footY,
                 c[0]*0.85f*kk, c[1]*0.82f*kk, c[2]*0.80f*kk, aBody, legW);
    }

    // wagging tail (behind body), treble raises wag amplitude/speed
    float wagAmp = 0.5f + 1.6f*treble;
    float wag = sinf((float)t*(9.f + 10.f*treble) + x*0.05f) * wagAmp;
    float tailBaseX = x - size*1.05f, tailBaseY = cy - size*0.15f;
    float tMidX = tailBaseX - size*0.55f, tMidY = tailBaseY - size*0.35f - wag*size*0.25f;
    float tTipX = tMidX - size*0.45f,     tTipY = tMidY - size*0.55f - wag*size*0.55f;
    lineOver(fb,W,H, tailBaseX,tailBaseY, tMidX,tMidY, c[0],c[1],c[2], aBody, size*0.24f);
    lineOver(fb,W,H, tMidX,tMidY, tTipX,tTipY,       bl[0],bl[1],bl[2], aBody, size*0.20f);

    // body (oval) + lighter belly
    ellipseOver(fb,W,H, x, cy, size*1.20f, size*0.66f, c[0],c[1],c[2], aBody);
    ellipseOver(fb,W,H, x, cy+size*0.34f, size*0.98f, size*0.32f, bl[0],bl[1],bl[2], aSoft);

    // head (front, slightly up) + snout
    float hx = x + size*1.08f, hy = cy - size*0.46f;
    ellipseOver(fb,W,H, hx, hy, size*0.58f, size*0.55f, c[0],c[1],c[2], aBody);
    ellipseOver(fb,W,H, hx+size*0.40f, hy+size*0.20f, size*0.38f, size*0.26f, bl[0],bl[1],bl[2], aBody);
    // nose (dark)
    ellipseOver(fb,W,H, hx+size*0.74f, hy+size*0.18f, size*0.11f, size*0.11f, 0.08f,0.05f,0.04f, aBody);
    // floppy ear (hangs back from top of head)
    lineOver(fb,W,H, hx-size*0.18f, hy-size*0.42f, hx-size*0.52f, hy+size*0.40f,
             c[0]*0.78f,c[1]*0.70f,c[2]*0.60f, aBody, size*0.34f);
    // friendly eye: white + dark pupil
    ellipseOver(fb,W,H, hx+size*0.16f, hy-size*0.06f, size*0.10f, size*0.11f, 0.98f,0.98f,0.98f, aBody);
    ellipseOver(fb,W,H, hx+size*0.20f, hy-size*0.05f, size*0.05f, size*0.06f, 0.05f,0.03f,0.02f, aBody);
    // gentle warm rim glow so the dog pops against the field
    cv->add_glow(cv, x, cy, c[0],c[1],c[2], 0.10f*A, size*1.4f);
}

VFX_EXPORT void vfx_plugin_render(void* st, const VfxCanvas* cv, const VfxParams* p){
    State* s=(State*)st;
    float* fb=cv->fb; int W=cv->width, H=cv->height;
    float A=p->alpha;
    float bass=p->bass, mid=p->mid, tre=p->treble, rms=p->rms, beat=p->beat, onset=p->onset;
    double t=p->time;
    int hz=s->horizon;

    // ---- SKY: warm blue up top fading to pale near the horizon (row-stepped) ----
    for(int y=0;y<hz;y++){
        float v=(float)y/hz;                 // 0 top .. 1 horizon
        float r=0.30f + 0.58f*v;             // -> pale warm
        float g=0.54f + 0.34f*v;
        float b=0.86f - 0.18f*v;
        // gentle daylight lift
        r*=1.02f;
        for(int x=0;x<W;x++) putAdd(fb,W,H,x,y, r,g,b, 0.9f*A);
    }

    // ---- SUN: distinct warm disk + layered halo, pulses gently with bass ----
    float sx=0.80f*W, sy=0.19f*H;
    float sunP = 1.f + 0.40f*bass;
    cv->add_glow(cv, sx,sy, 1.0f,0.78f,0.40f, 0.55f*sunP*A, 130.f*sunP);
    cv->add_glow(cv, sx,sy, 1.0f,0.85f,0.52f, 0.80f*sunP*A, 62.f*sunP);
    cv->add_glow(cv, sx,sy, 1.0f,0.92f,0.66f, 1.10f*sunP*A, 30.f);
    ellipseOver(fb,W,H, sx,sy, 26.f,26.f, 1.0f,0.93f,0.68f, clampf(0.95f*A,0.f,1.f));

    // ---- CLOUDS: fluffy clusters drifting slowly with time (kept off the sun) ----
    for(int c=0;c<4;c++){
        float cw = 70.f + hashf(c,11)*55.f;
        float drift = 5.f + hashf(c,12)*6.f;
        float baseX = hashf(c,13)*(W+240.f)-120.f;
        float cxp = fmodf(baseX + (float)t*drift, W+300.f) - 150.f;
        float cyp = 0.09f*H + hashf(c,14)*0.15f*H;
        int puffs = 5 + (int)(hashf(c,15)*3);
        for(int q=0;q<puffs;q++){
            float ox = (hashf(c,20+q)-0.5f)*cw;
            float oy = (hashf(c,30+q)-0.5f)*cw*0.35f;
            float rr = cw*(0.28f + 0.26f*hashf(c,40+q));
            // skip a puff if it would sit on the sun (keep the sun readable)
            float ddx=cxp+ox-sx, ddy=cyp+oy-sy;
            if(ddx*ddx+ddy*ddy < 95.f*95.f) continue;
            cv->add_glow(cv, cxp+ox, cyp+oy, 1.0f,0.99f,0.97f, 0.30f*A, rr);
            ellipseOver(fb,W,H, cxp+ox, cyp+oy, rr*0.72f, rr*0.46f, 1.0f,0.99f,0.98f, clampf(0.62f*A,0.f,1.f));
        }
    }

    // ---- GROUND: rolling green field, brighter near horizon -> richer foreground
    for(int y=hz;y<H;y++){
        float u=(float)(y-hz)/(H-hz);         // 0 horizon .. 1 bottom
        float r=0.42f - 0.24f*u;
        float g=0.64f - 0.22f*u;
        float b=0.28f - 0.14f*u;
        // subtle rolling brightness bands (analytic)
        float roll=0.05f*sinf((float)y*0.05f + 0.6f);
        r+=roll; g+=roll*1.2f;
        for(int x=0;x<W;x++) putAdd(fb,W,H,x,y, r,g,b, 0.9f*A);
    }

    // ---- RIVER: winding blue band with shimmering ripples (rms) ----
    float rBase = hz + 0.15f*H;
    for(int x=0;x<W;x++){
        float wind = 20.f*sinf(x*0.011f + 0.7f) + 8.f*sinf(x*0.031f);
        float cyR = rBase + wind;
        float th  = 12.f + 5.f*sinf(x*0.02f + 1.3f);   // half-thickness
        int y0=(int)(cyR-th), y1=(int)(cyR+th);
        for(int y=y0;y<=y1;y++){
            float dv=(y-cyR)/th; if(dv<-1.f||dv>1.f) continue;
            float core=1.f-dv*dv;                       // brightest mid-stream
            float r=0.16f+0.10f*core, g=0.42f+0.20f*core, b=0.78f+0.20f*core;
            putAdd(fb,W,H,x,y, r,g,b, (0.75f+0.35f*core)*A);
        }
        // shimmering highlights riding the surface, shimmer with rms + time
        float sh = sinf(x*0.09f - (float)t*2.4f) * sinf(x*0.017f + (float)t*1.1f);
        float shi = clampf(sh,0.f,1.f)*(0.35f+1.1f*rms);
        if(shi>0.05f){
            int yy=(int)(cyR - th*0.35f);
            putAdd(fb,W,H,x,yy,   0.9f,0.97f,1.0f, shi*0.9f*A);
            putAdd(fb,W,H,x,yy-1, 0.8f,0.92f,1.0f, shi*0.5f*A);
        }
    }
    // sparkle glints on the water (bass/treble)
    for(int i=0;i<26;i++){
        float gx=hashf(i,71)*W;
        float wind = 20.f*sinf(gx*0.011f + 0.7f) + 8.f*sinf(gx*0.031f);
        float gy=rBase+wind + (hashf(i,72)-0.5f)*16.f;
        float tw=0.5f+0.5f*sinf((float)t*6.f + i*1.7f);
        cv->add_glow(cv, gx,gy, 0.95f,1.0f,1.0f, (0.10f+0.5f*rms)*tw*A, 3.5f);
    }

    // ---- GRASS: short blades swaying with mid ----
    float sway = (0.5f + 3.0f*mid);
    for(const Blade& b : s->grass){
        float ang = sway*sinf(b.x*0.05f + (float)t*1.8f + b.ph);
        float tipX = b.x + ang*b.len*0.5f;
        float tipY = b.y - b.len;
        float r,g,bb; cv->hsv(b.hue, 0.75f, 0.7f, &r,&g,&bb);
        cv->add_line(cv, b.x, b.y, tipX, tipY, r,g,bb, 0.7f*A, 1.6f);
    }

    // ---- DOGS trotting across the meadow (drawn last, in front) ----
    for(const Dog& d : s->dogs)
        drawDog(cv, fb, W, H, d, t, beat, onset, tre, A);
}
