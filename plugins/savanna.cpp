// savanna.cpp -- "Savanna" scene plugin.
// An African savanna at golden sunset: a warm amber/orange gradient sky with a
// big low sun and drifting clouds, distant flat-topped acacia-tree silhouettes,
// tall golden foreground grass swaying in the breeze, dark animal silhouettes
// (giraffe, elephant, a small herd of gazelles) walking across, and birds
// drifting high in the sky.
//
// All animation is derived analytically from the absolute time p->time, so any
// frame renders correctly with no cross-frame state (offline render + seeking).
// The player tone-maps the additive HDR buffer (bloom/shake/grain), so we lay a
// full sky+ground base every frame. All output is scaled by p->alpha.

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
// OPAQUE composite (fb = fb*(1-a) + rgb*a): a=0 leaves buffer untouched, so it
// plays fair with scene crossfades. Used for dark silhouettes so they read as
// solid shapes against the bright sky instead of blooming.
static inline void putOver(float* fb, int W, int H, int x, int y,
                           float r, float g, float b, float a){
    if ((unsigned)x >= (unsigned)W || (unsigned)y >= (unsigned)H) return;
    size_t i=((size_t)y*W+x)*3; float ia=1.f-a;
    fb[i]=fb[i]*ia + r*a; fb[i+1]=fb[i+1]*ia + g*a; fb[i+2]=fb[i+2]*ia + b*a;
}
static void ellipseOver(float* fb, int W, int H, float cx, float cy, float rx, float ry,
                        float r, float g, float b, float a){
    if(rx<0.5f||ry<0.5f) return;
    int y0=(int)floorf(cy-ry), y1=(int)ceilf(cy+ry);
    for(int y=y0;y<=y1;y++){
        float dy=(y-cy)/ry; if(dy<-1.f||dy>1.f) continue;
        float ext=rx*sqrtf(1.f-dy*dy);
        int xa=(int)floorf(cx-ext), xb=(int)ceilf(cx+ext);
        for(int x=xa;x<=xb;x++) putOver(fb,W,H,x,y,r,g,b,a);
    }
}
// opaque thick rounded line (limbs, necks, branches)
static void lineOver(float* fb, int W, int H, float x0,float y0,float x1,float y1,
                     float r,float g,float b,float a,float wid){
    float dx=x1-x0, dy=y1-y0; float len=sqrtf(dx*dx+dy*dy); int n=(int)len+1;
    float hw=wid*0.5f; int rr=(int)ceilf(hw);
    for(int i=0;i<=n;i++){
        float t=(float)i/n; float px=x0+dx*t, py=y0+dy*t;
        for(int oy=-rr;oy<=rr;oy++) for(int ox=-rr;ox<=rr;ox++)
            if(ox*ox+oy*oy<=hw*hw) putOver(fb,W,H,(int)(px+0.5f)+ox,(int)(py+0.5f)+oy,r,g,b,a);
    }
}
// soft ground shadow: darken the dust under a body
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

// silhouette tint (warm near-black); distance blends slightly toward sky haze.
static const float SILR=0.055f, SILG=0.032f, SILB=0.028f;

struct Blade { float x, baseY, len, hue, ph; int band; };
struct Tree  { float x; float scale; float depth; };  // depth 0 far .. 1 near
struct Bird  { float x0, y, speed, size, ph; };

struct State {
    int W, H, horizon;
    float sunX, sunY;
    std::vector<Blade> grass;
    std::vector<Tree>  trees;
    std::vector<Bird>  birds;
};

static VfxPluginInfo INFO = {
    VFX_PLUGIN_ABI, "Savanna", "veffects",
    "An African savanna at golden sunset: a low pulsing sun, acacia silhouettes, swaying golden grass, and giraffe, elephant, gazelles and birds crossing to the music."
};
VFX_EXPORT const VfxPluginInfo* vfx_plugin_info(void){ return &INFO; }

VFX_EXPORT void* vfx_plugin_create(int W, int H){
    State* s = new State();          // never NULL: the player disables NULL scenes
    s->W=W; s->H=H;
    s->horizon = (int)(0.625f*H);
    s->sunX = 0.31f*W; s->sunY = 0.505f*H;

    // distant flat-topped acacia trees dotted along the horizon
    auto addTree=[&](float x,float sc,float d){ Tree t; t.x=x; t.scale=sc; t.depth=d; s->trees.push_back(t); };
    addTree(0.10f*W, 26.f, 0.35f);
    addTree(0.52f*W, 20.f, 0.20f);
    addTree(0.72f*W, 34.f, 0.55f);
    addTree(0.88f*W, 44.f, 0.80f);
    addTree(0.40f*W, 15.f, 0.10f);

    // tall golden foreground grass; heights partly driven by spectrum bands
    int NB = 520;
    s->grass.reserve(NB);
    int gtop = s->horizon + (int)(0.06f*H);
    for(int i=0;i<NB;i++){
        Blade b;
        b.x = hashf(i,1)*(W+40.f)-20.f;
        float u = hashf(i,2);                       // 0 near horizon .. 1 bottom
        b.baseY = gtop + u*(H-gtop);
        b.len = (18.f + hashf(i,3)*26.f) * (0.45f + 0.85f*u);   // taller near camera
        b.hue = 0.09f + hashf(i,4)*0.045f;          // amber..gold
        b.ph  = hashf(i,5)*6.2831f;
        b.band = i;                                 // resolved against bandCount at render
        s->grass.push_back(b);
    }

    // birds high in the sky
    for(int i=0;i<7;i++){
        Bird b; b.x0=hashf(i,61)*(W+200.f); b.y=0.10f*H+hashf(i,62)*0.24f*H;
        b.speed=10.f+hashf(i,63)*16.f; b.size=4.f+hashf(i,64)*4.f; b.ph=hashf(i,65)*6.2831f;
        s->birds.push_back(b);
    }
    return s;
}
VFX_EXPORT void vfx_plugin_destroy(void* st){ delete (State*)st; }

// ---- flat-topped acacia silhouette ----
static void drawAcacia(float* fb,int W,int H, const Tree& tr, int hz, float A, float wind){
    float S=tr.scale;
    // distant trees are hazier (lower opacity, lifted toward sky)
    float a = clampf((0.62f + 0.34f*tr.depth)*A, 0.f, 1.f);
    float r=SILR, g=SILG, b=SILB;
    float haze=(1.f-tr.depth)*0.18f; r+=haze*0.9f; g+=haze*0.55f; b+=haze*0.4f;
    float baseX=tr.x, baseY=(float)hz + S*0.05f;
    float topY = baseY - S*1.8f;
    float sway = wind*S*0.03f;
    // trunk
    lineOver(fb,W,H, baseX, baseY, baseX+sway*0.4f, topY+S*0.2f, r,g,b, a, S*0.16f);
    // a few diverging branches fanning up to the canopy
    for(int k=-2;k<=2;k++){
        float bx = baseX + k*S*0.55f + sway;
        lineOver(fb,W,H, baseX+sway*0.4f, topY+S*0.3f, bx, topY, r,g,b, a, S*0.08f);
    }
    // wide, flat umbrella canopy (a couple of stacked flattened ellipses)
    ellipseOver(fb,W,H, baseX+sway, topY,          S*1.55f, S*0.34f, r,g,b, a);
    ellipseOver(fb,W,H, baseX+sway, topY-S*0.22f,  S*1.15f, S*0.24f, r,g,b, a);
}

// ---- generic trotting leg set for the walkers ----
static void legs4(float* fb,int W,int H, float x,float hipY,float S,
                  float legLen,float legW,float stride,
                  float r,float g,float b,float a){
    // near/far pairs, diagonal gait
    struct L{ float off,phase,shade; };
    L L4[4]={ {-0.55f,0.0f,0.80f},{ 0.55f,3.14159f,0.80f},
              {-0.55f,3.14159f,1.0f},{ 0.55f,0.0f,1.0f} };
    for(int i=0;i<4;i++){
        float hx=x+L4[i].off*S;
        float sw=sinf(stride+L4[i].phase);
        float lift=clampf(sw,0.f,1.f);
        float footX=hx+sw*S*0.42f;
        float footY=hipY+legLen-lift*S*0.30f;
        float sh=L4[i].shade;
        lineOver(fb,W,H, hx,hipY, footX,footY, r*sh,g*sh,b*sh, a, legW);
    }
}

// ---- elephant silhouette, facing right ----
static void drawElephant(const VfxCanvas* cv,float* fb,int W,int H,
                         float x,float baseY,float S,double t,float beat,float onset,float A){
    float stride=x*0.09f + (float)t*(1.6f+3.5f*beat);
    float bob=S*0.05f*(0.5f+0.5f*sinf(2.f*stride));
    float cy=baseY-bob;
    float a=clampf(0.97f*A,0.f,1.f);
    float r=SILR,g=SILG,b=SILB;
    ellipseMul(fb,W,H, x, baseY+S*0.72f, S*1.5f, S*0.28f, 1.f-0.30f*A);   // shadow
    legs4(fb,W,H, x, cy+S*0.45f, S, S*0.75f, S*0.34f, stride, r,g,b,a);
    // body
    ellipseOver(fb,W,H, x, cy, S*1.35f, S*0.85f, r,g,b,a);
    ellipseOver(fb,W,H, x-S*0.9f, cy-S*0.15f, S*0.6f, S*0.62f, r,g,b,a);   // haunch
    // head
    float hx=x+S*1.2f, hy=cy-S*0.25f;
    ellipseOver(fb,W,H, hx,hy, S*0.72f, S*0.7f, r,g,b,a);
    // flapping ear
    float ear=sinf((float)t*2.2f + 0.5f)*S*0.12f;
    ellipseOver(fb,W,H, hx-S*0.15f, hy+S*0.05f, S*0.5f+ear*0.3f, S*0.62f, r,g,b,a);
    // curling trunk (segmented, sways)
    float tx=hx+S*0.55f, ty=hy+S*0.35f;
    float px=tx,py=ty;
    for(int k=1;k<=5;k++){
        float u=k/5.f;
        float curl=sinf((float)t*1.5f + u*2.4f)*S*0.18f*u;
        float nx=tx + curl + u*S*0.15f;
        float ny=ty + u*S*1.05f;
        lineOver(fb,W,H, px,py,nx,ny, r,g,b,a, S*(0.24f-0.12f*u));
        px=nx; py=ny;
    }
    // tusk (faint warm ivory)
    lineOver(fb,W,H, hx+S*0.4f, hy+S*0.4f, hx+S*0.6f, hy+S*0.7f, 0.55f,0.42f,0.25f, 0.5f*A, S*0.08f);
    // tail
    lineOver(fb,W,H, x-S*1.3f, cy-S*0.1f, x-S*1.5f, cy+S*0.5f, r,g,b,a, S*0.08f);
    cv->add_glow(cv, x+S*0.2f, cy-S*0.6f, 1.0f,0.6f,0.25f, 0.05f*A, S*1.6f); // sunset rim
}

// ---- giraffe silhouette, facing right ----
static void drawGiraffe(const VfxCanvas* cv,float* fb,int W,int H,
                        float x,float baseY,float S,double t,float beat,float onset,float A){
    float stride=x*0.11f + (float)t*(2.0f+4.0f*beat);
    float bob=S*0.06f*(0.5f+0.5f*sinf(2.f*stride));
    float cy=baseY-bob;
    float a=clampf(0.97f*A,0.f,1.f);
    float r=SILR,g=SILG,b=SILB;
    ellipseMul(fb,W,H, x, baseY+S*1.9f, S*1.2f, S*0.22f, 1.f-0.28f*A);     // shadow
    // very long legs
    {
        struct L{ float off,phase,shade; };
        L L4[4]={ {-0.5f,0.0f,0.82f},{ 0.5f,3.14159f,0.82f},
                  {-0.5f,3.14159f,1.0f},{ 0.5f,0.0f,1.0f} };
        for(int i=0;i<4;i++){
            float hx=x+L4[i].off*S;
            float sw=sinf(stride+L4[i].phase);
            float lift=clampf(sw,0.f,1.f);
            float footX=hx+sw*S*0.4f;
            float footY=cy+S*0.4f + S*2.0f - lift*S*0.35f;
            float sh=L4[i].shade;
            lineOver(fb,W,H, hx,cy+S*0.3f, footX,footY, r*sh,g*sh,b*sh, a, S*0.2f);
        }
    }
    // sloping body (higher at the shoulders/front)
    ellipseOver(fb,W,H, x, cy, S*1.05f, S*0.55f, r,g,b,a);
    ellipseOver(fb,W,H, x+S*0.7f, cy-S*0.2f, S*0.55f, S*0.5f, r,g,b,a);    // shoulder rise
    // long neck angled up-forward, with a little audio sway
    float nsw=sinf((float)t*1.2f)*S*0.1f + onset*S*0.15f;
    float neckX=x+S*0.85f, neckY=cy-S*0.3f;
    float headX=neckX+S*0.95f+nsw, headY=neckY-S*2.2f;
    lineOver(fb,W,H, neckX,neckY, headX,headY, r,g,b,a, S*0.34f);
    // head + snout
    ellipseOver(fb,W,H, headX,headY, S*0.3f,S*0.24f, r,g,b,a);
    ellipseOver(fb,W,H, headX+S*0.28f, headY+S*0.05f, S*0.24f,S*0.15f, r,g,b,a);
    // ossicones (little horns)
    lineOver(fb,W,H, headX-S*0.08f, headY-S*0.2f, headX-S*0.12f, headY-S*0.42f, r,g,b,a, S*0.07f);
    lineOver(fb,W,H, headX+S*0.08f, headY-S*0.2f, headX+S*0.10f, headY-S*0.42f, r,g,b,a, S*0.07f);
    // tail
    float tw=sinf((float)t*4.f)*S*0.15f;
    lineOver(fb,W,H, x-S*1.0f, cy-S*0.2f, x-S*1.15f+tw, cy+S*0.9f, r,g,b,a, S*0.08f);
    cv->add_glow(cv, headX, headY, 1.0f,0.6f,0.25f, 0.05f*A, S*0.9f);      // sunset rim
}

// ---- small gazelle silhouette, facing right (bounding, faster) ----
static void drawGazelle(const VfxCanvas* cv,float* fb,int W,int H,
                        float x,float baseY,float S,double t,float beat,float onset,float A){
    float stride=x*0.16f + (float)t*(3.0f+6.0f*beat+4.0f*onset);
    float bound=fabsf(sinf(stride))*S*0.35f*(0.6f+1.2f*beat);
    float cy=baseY-bound;
    float a=clampf(0.96f*A,0.f,1.f);
    float r=SILR,g=SILG,b=SILB;
    ellipseMul(fb,W,H, x, baseY+S*0.9f, S*0.9f, S*0.16f, 1.f-0.22f*A);     // shadow
    // slim legs (splay while bounding)
    for(int i=0;i<4;i++){
        float off=(i<2?-0.45f:0.45f)*S;
        float ph=(i%2==0)?0.f:3.14159f;
        float sw=sinf(stride+ph);
        float hx=x+off;
        float footX=hx+sw*S*0.6f;
        float footY=cy+S*0.35f+S*0.95f;
        lineOver(fb,W,H, hx,cy+S*0.25f, footX,footY, r,g,b,a, S*0.12f);
    }
    ellipseOver(fb,W,H, x, cy, S*0.85f, S*0.42f, r,g,b,a);                 // body
    // neck + head up-forward
    float neckX=x+S*0.7f, neckY=cy-S*0.15f;
    float headX=neckX+S*0.5f, headY=neckY-S*0.7f;
    lineOver(fb,W,H, neckX,neckY, headX,headY, r,g,b,a, S*0.2f);
    ellipseOver(fb,W,H, headX,headY, S*0.22f,S*0.16f, r,g,b,a);
    // curved horns swept back
    lineOver(fb,W,H, headX,headY-S*0.14f, headX-S*0.3f, headY-S*0.5f, r,g,b,a, S*0.06f);
    lineOver(fb,W,H, headX+S*0.05f,headY-S*0.14f, headX-S*0.22f, headY-S*0.52f, r,g,b,a, S*0.06f);
    // little tail
    lineOver(fb,W,H, x-S*0.8f, cy-S*0.1f, x-S*0.95f, cy+S*0.35f, r,g,b,a, S*0.07f);
}

VFX_EXPORT void vfx_plugin_render(void* st, const VfxCanvas* cv, const VfxParams* p){
    State* s=(State*)st;
    float* fb=cv->fb; int W=cv->width, H=cv->height;
    float A=p->alpha;
    float bass=p->bass, mid=p->mid, tre=p->treble, rms=p->rms, beat=p->beat, onset=p->onset;
    double t=p->time;
    int hz=s->horizon;
    float sx=s->sunX, sy=s->sunY;

    // ---- SKY: amber/orange/gold sunset gradient + radial warmth around the sun ----
    for(int y=0;y<hz;y++){
        float v=(float)y/hz;                        // 0 top .. 1 horizon
        // deep warm dusk up top -> molten gold at the horizon
        float r=mixf(0.42f, 1.15f, powf(v,0.7f));
        float g=mixf(0.14f, 0.62f, v*v);
        float b=mixf(0.20f, 0.16f, v);
        for(int x=0;x<W;x++){
            float dx=(x-sx)*0.7f, dy=(y-sy);
            float d2=dx*dx+dy*dy;
            float halo=180.f*180.f/(d2+180.f*180.f);  // brighten toward the sun
            float rr=r + halo*0.55f;
            float gg=g + halo*0.32f;
            float bb=b + halo*0.08f;
            putAdd(fb,W,H,x,y, rr,gg,bb, 0.9f*A);
        }
    }

    // ---- SUN: big low disk, halo pulses with bass ----
    float sunP=1.f+0.45f*bass;
    cv->add_glow(cv, sx,sy, 1.0f,0.55f,0.18f, 0.55f*sunP*A, 170.f*sunP);
    cv->add_glow(cv, sx,sy, 1.0f,0.68f,0.28f, 0.85f*sunP*A, 90.f*sunP);
    cv->add_glow(cv, sx,sy, 1.0f,0.82f,0.45f, 1.15f*sunP*A, 46.f);
    ellipseOver(fb,W,H, sx,sy, 42.f,42.f, 1.0f,0.80f,0.42f, clampf(0.96f*A,0.f,1.f));

    // ---- CLOUDS: a few warm-lit bands drifting slowly, kept off the sun ----
    for(int c=0;c<4;c++){
        float cw=90.f+hashf(c,11)*70.f;
        float drift=4.f+hashf(c,12)*5.f;
        float baseX=hashf(c,13)*(W+240.f)-120.f;
        float cxp=fmodf(baseX+(float)t*drift, W+300.f)-150.f;
        float cyp=0.10f*H+hashf(c,14)*0.22f*H;
        int puffs=4+(int)(hashf(c,15)*3);
        for(int q=0;q<puffs;q++){
            float ox=(hashf(c,20+q)-0.5f)*cw;
            float oy=(hashf(c,30+q)-0.5f)*cw*0.3f;
            float rr=cw*(0.26f+0.24f*hashf(c,40+q));
            float ddx=cxp+ox-sx, ddy=cyp+oy-sy;
            if(ddx*ddx+ddy*ddy < 120.f*120.f) continue;   // keep the sun readable
            cv->add_glow(cv, cxp+ox, cyp+oy, 1.0f,0.55f,0.28f, 0.16f*A, rr);      // warm underside
            ellipseOver(fb,W,H, cxp+ox, cyp+oy-rr*0.15f, rr*0.7f, rr*0.28f, 0.85f,0.5f,0.28f, clampf(0.35f*A,0.f,1.f));
        }
    }

    // ---- BIRDS: little dark V's drifting high, drift/flap with treble ----
    for(const Bird& bd : s->birds){
        float sp=bd.speed*(1.f+1.4f*tre);
        float bx=fmodf(bd.x0+(float)t*sp, W+200.f)-100.f;
        float by=bd.y + sinf((float)t*0.6f+bd.ph)*10.f;
        float flap=sinf((float)t*(7.f+9.f*tre)+bd.ph);
        float wy=by - fabsf(flap)*bd.size*0.6f;
        float S=bd.size;
        lineOver(fb,W,H, bx,by, bx-S,wy, SILR,SILG,SILB, 0.8f*A, 1.6f);
        lineOver(fb,W,H, bx,by, bx+S,wy, SILR,SILG,SILB, 0.8f*A, 1.6f);
    }

    // ---- GROUND: dusty warm savanna floor, richer/darker toward the camera ----
    for(int y=hz;y<H;y++){
        float u=(float)(y-hz)/(H-hz);               // 0 horizon .. 1 bottom
        float r=mixf(0.72f, 0.34f, u);
        float g=mixf(0.42f, 0.17f, u);
        float b=mixf(0.20f, 0.08f, u);
        float roll=0.03f*sinf((float)y*0.06f+0.4f);
        r+=roll; g+=roll*0.7f;
        for(int x=0;x<W;x++) putAdd(fb,W,H,x,y, r,g,b, 0.9f*A);
    }

    // ---- HEAT HAZE / SHIMMER just above the horizon, breathes with rms ----
    {
        int band=(int)(0.10f*H);
        for(int y=hz-band;y<hz+band/2;y++){
            if(y<0||y>=H) continue;
            float d=1.f-fabsf((float)(y-hz))/(float)band;
            float shimmer=sinf(y*0.6f + (float)t*7.0f)*(0.5f+2.5f*rms);
            float k=clampf(d,0.f,1.f)*(0.04f+0.05f*rms)*(0.6f+0.4f*shimmer);
            if(k<=0.f) continue;
            for(int x=0;x<W;x+=2) putAdd(fb,W,H,x,y, 1.0f,0.6f,0.25f, k*A);
        }
    }

    // ---- ACACIA TREES along the horizon (shimmering slightly with rms) ----
    float wind=sinf((float)t*0.5f)*(1.f+2.0f*mid) + rms*sinf((float)t*9.f)*0.6f;
    for(const Tree& tr : s->trees) drawAcacia(fb,W,H, tr, hz, A, wind);

    // ---- ANIMALS walking across (slow big ones, fast gazelle herd) ----
    auto walkX=[&](float x0,float sp,float extra)->float{
        float margin=160.f, range=W+2.f*margin;
        float v=sp*(1.f+0.8f*beat+1.2f*onset+extra);
        float x=fmodf((float)(t*v)+x0, range); if(x<0)x+=range; return x-margin;
    };
    // giraffe (far/back, tall)
    drawGiraffe(cv,fb,W,H, walkX(120.f,16.f,0.f), 0.70f*H, 20.f, t,beat,onset,A);
    // elephant (nearer, lower, bigger)
    drawElephant(cv,fb,W,H, walkX(430.f,20.f,0.f), 0.82f*H, 26.f, t,beat,onset,A);
    // gazelle herd (small, quick, clustered)
    for(int i=0;i<4;i++){
        float ph=i*70.f;
        float gx=walkX(240.f+ph, 44.f, 0.4f);
        float gy=(0.78f+0.03f*i)*H;
        drawGazelle(cv,fb,W,H, gx, gy, 11.f+0.6f*i, t,beat,onset,A);
    }

    // ---- TALL GOLDEN GRASS in the foreground, sways with mid, heights from bands
    int NB=p->bandCount;
    float sway=(0.6f+3.4f*mid);
    for(const Blade& b : s->grass){
        float bandE = (NB>0) ? p->bands[b.band % NB] : 0.f;
        float len=b.len*(1.f + 0.9f*bandE);
        float ang=sway*sinf(b.x*0.045f+(float)t*1.9f+b.ph);
        float midX=b.x+ang*len*0.28f, midY=b.baseY-len*0.55f;
        float tipX=b.x+ang*len*0.7f,  tipY=b.baseY-len;
        float r,g,bb; cv->hsv(b.hue, 0.85f, 0.85f, &r,&g,&bb);
        // two-segment blade: warm base fading to bright gold tip
        cv->add_line(cv, b.x,b.baseY, midX,midY, r*0.8f,g*0.75f,bb*0.6f, 0.55f*A, 2.0f);
        cv->add_line(cv, midX,midY, tipX,tipY,   r,g,bb,                0.55f*A, 1.4f);
    }
}
