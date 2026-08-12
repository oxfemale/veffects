// underwater.cpp -- "Underwater" scene plugin.
// A serene undersea world: a blue-to-teal depth gradient, slanting caustic light
// rays shimmering down from the surface, rising bubble streams, schools of little
// side-view fish that dart and turn, swaying kelp, a drifting jellyfish, coral
// silhouettes and tiny plankton specks.
//
// All animation is derived analytically from the absolute time p->time, so any
// frame renders correctly without simulation history (offline render + seeking).
// The player tone-maps the additive HDR buffer (bloom/shake/grain), so we lay a
// full bright water base every frame. All output is scaled by p->alpha.

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
// OPAQUE composite (fb = fb*(1-a) + rgb*a); at a=0 leaves buffer untouched so it
// plays fair with scene crossfades. Used for fish/coral so their colors read.
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
// filled triangle (opaque) via bounding box + edge sign test -- for fish tails
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
// additive soft line (kelp / tentacles)
static void lineAdd(float* fb, int W, int H, float x0,float y0,float x1,float y1,
                    float r,float g,float b,float k,float wid){
    float dx=x1-x0, dy=y1-y0; float len=sqrtf(dx*dx+dy*dy); int n=(int)len+1;
    for(int i=0;i<=n;i++){ float t=(float)i/n; float px=x0+dx*t, py=y0+dy*t;
        int hw=(int)wid;
        for(int oy=-hw;oy<=hw;oy++) for(int ox=-hw;ox<=hw;ox++){
            float d=sqrtf((float)(ox*ox+oy*oy)); float f=clampf(1.f-d/(wid+0.5f),0.f,1.f);
            putAdd(fb,W,H,(int)px+ox,(int)py+oy, r,g,b, k*f);
        }
    }
}
// opaque thick line for coral silhouettes
static void lineOver(float* fb, int W, int H, float x0,float y0,float x1,float y1,
                     float r,float g,float b,float a,float wid){
    float dx=x1-x0, dy=y1-y0; float len=sqrtf(dx*dx+dy*dy); int n=(int)len+1;
    for(int i=0;i<=n;i++){ float t=(float)i/n; float px=x0+dx*t, py=y0+dy*t;
        int hw=(int)wid;
        for(int oy=-hw;oy<=hw;oy++) for(int ox=-hw;ox<=hw;ox++)
            if(ox*ox+oy*oy<=hw*hw) putOver(fb,W,H,(int)px+ox,(int)py+oy, r,g,b, a);
    }
}

struct Fish {
    float phase;   // 0..1 start along its track
    float y;       // base vertical band
    float speed;   // px/s across
    int   dir;     // +1 -> right, -1 -> left
    float size;
    int   school;  // school id
    float hue;     // 0 warm orange, 1 silver-blue
    float bob;     // bob amplitude
};
struct Bubble { int stream; float phase; };
struct State {
    int W, H;
    std::vector<Fish> fish;
    int nStreams = 9;
    int nKelp = 11;
    int nPlank = 130;
};

static VfxPluginInfo INFO = {
    VFX_PLUGIN_ABI, "Underwater", "veffects",
    "A serene undersea world of caustic light rays, rising bubbles, darting fish schools, swaying kelp, a drifting jellyfish and plankton."
};
VFX_EXPORT const VfxPluginInfo* vfx_plugin_info(void){ return &INFO; }

VFX_EXPORT void* vfx_plugin_create(int W, int H){
    State* s=new State(); s->W=W; s->H=H;
    int NF=22;
    for(int i=0;i<NF;i++){
        Fish f;
        f.school = i/5;                                   // ~5 fish per school
        f.phase  = hashf(i,1);
        f.dir    = (f.school%2==0)? 1 : -1;
        f.y      = 0.20f*H + hashf(i,2)*0.60f*H;
        f.speed  = 26.f + hashf(i,3)*26.f;
        f.size   = 6.f + hashf(i,4)*5.f;
        f.hue    = (hashf(i,5)<0.6f)? 0.f : 1.f;
        f.bob    = 5.f + hashf(i,6)*8.f;
        s->fish.push_back(f);
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
    int NB=p->bandCount;

    // deep light swell with bass (slow) -- gently brightens the whole column
    float swell = 1.f + 0.16f*bass + 0.05f*sinf((float)t*0.5f);

    // ---- WATER: blue-to-teal depth gradient, lighter near the surface ----
    // fold in slanting caustic god-rays (drifting) + fine caustic shimmer so the
    // whole thing is one full-frame pass.
    float rayDrift = (float)t*22.f;                       // rays slide sideways
    float shimAmt  = 0.35f + 1.1f*rms;                    // shimmer brightens w/ rms
    for(int y=0;y<H;y++){
        float v=(float)y/H;                               // 0 surface .. 1 deep
        // base gradient
        float br=mixf(0.16f,0.02f,v);
        float bg=mixf(0.55f,0.10f,v);
        float bb=mixf(0.70f,0.30f,v);
        float depthLight=clampf(1.f-v*1.25f,0.f,1.f);     // caustics strongest up top
        float depthLight2=depthLight*depthLight;
        for(int x=0;x<W;x++){
            float r=br, g=bg, b=bb;
            if(depthLight2>0.01f){
                // slanting god-rays: vertical-ish bands sheared by depth, drifting
                float sc = (float)x - v*260.f - rayDrift;
                float ray=0.f;
                for(int k=0;k<5;k++){
                    float center = k*150.f + 30.f*sinf((float)t*0.4f + k*1.7f);
                    float bandMod = (NB>0)? (0.6f+0.9f*p->bands[(k*3)%NB]) : 1.f;
                    float dd=(sc-center)/46.f;
                    ray += expf(-dd*dd)*bandMod;
                }
                // fine caustic mesh shimmer
                float sh = sinf(x*0.045f + (float)t*1.6f) * sinf(y*0.05f - (float)t*1.1f)
                         + 0.5f*sinf((x+y)*0.03f + (float)t*2.2f);
                float caust = (0.30f*ray + 0.22f*clampf(sh,0.f,1.5f))*depthLight2*shimAmt;
                r+=caust*0.55f; g+=caust*0.95f; b+=caust*1.0f;
            }
            putAdd(fb,W,H,x,y, r*swell,g*swell,b*swell, 0.95f*A);
        }
    }
    // broad surface light swell glow (bass)
    cv->add_glow(cv, W*0.5f, -10.f, 0.5f,0.9f,1.0f, (0.25f+0.5f*bass)*A, 260.f);

    // ---- CORAL silhouettes along the bottom (opaque, deep teal/purple) ----
    for(int c=0;c<4;c++){
        float baseX = (c+0.5f)/4.f*W + (hashf(c,80)-0.5f)*40.f;
        float baseY = H-1.f;
        float ch = 42.f + hashf(c,81)*46.f;
        float cr=0.05f+0.05f*hashf(c,82), cg=0.10f+0.08f*hashf(c,83), cb=0.16f+0.10f*hashf(c,84);
        // main trunk with a couple of branches
        float tx=baseX, ty=baseY, ttx=baseX+(hashf(c,85)-0.5f)*30.f, tty=baseY-ch;
        lineOver(fb,W,H, tx,ty, ttx,tty, cr,cg,cb, 0.9f, 3.f);
        for(int br2=0;br2<3;br2++){
            float u=0.4f+0.2f*br2;
            float bxp=mixf(tx,ttx,u), byp=mixf(ty,tty,u);
            float ex=bxp+(hashf(c,90+br2)-0.5f)*ch*0.9f;
            float ey=byp-ch*(0.35f+0.25f*hashf(c,95+br2));
            lineOver(fb,W,H, bxp,byp, ex,ey, cr,cg,cb, 0.85f, 2.f);
        }
    }

    // ---- SWAYING KELP rooted at the bottom (sways with mid) ----
    for(int kk=0;kk<s->nKelp;kk++){
        float rootX = (kk+0.5f)/s->nKelp*W + (hashf(kk,20)-0.5f)*22.f;
        float rootY = H+4.f;
        float hgt   = 120.f + hashf(kk,21)*140.f;
        int seg=12;
        float sway = 12.f + 26.f*mid;
        float ph   = hashf(kk,22)*6.28f;
        float px=rootX, py=rootY;
        float g0=0.30f+0.25f*hashf(kk,23);
        for(int i=1;i<=seg;i++){
            float f=(float)i/seg;
            float ny=rootY-hgt*f;
            float nx=rootX + sinf((float)t*0.9f + ph + f*2.4f)*sway*f*f;
            float k=(0.5f+0.5f*(1.f-f));
            lineAdd(fb,W,H, px,py, nx,ny, 0.06f,g0,0.14f, 0.7f*k*A, 2.2f-1.2f*f);
            px=nx; py=ny;
        }
    }

    // ---- JELLYFISH drifting (gentle vertical bob) ----
    {
        float jx=W*0.20f + 40.f*sinf((float)t*0.13f);
        float jy=H*0.32f + 26.f*sinf((float)t*0.4f + 1.f) + 8.f*bass;
        float pulse=1.f+0.12f*sinf((float)t*1.6f);
        float br=0.9f, bg=0.7f, bb=1.0f;                  // soft pink-violet
        cv->add_glow(cv, jx,jy, br,bg,bb, 0.30f*A, 34.f*pulse);
        ellipseOver(fb,W,H, jx,jy, 22.f*pulse,16.f*pulse, br,bg,bb, clampf(0.35f*A,0.f,1.f));
        // hanging tentacles
        for(int tt=0;tt<7;tt++){
            float ox=(tt-3)*5.f;
            float sx0=jx+ox, sy0=jy+10.f;
            float ex=sx0 + sinf((float)t*1.4f + tt)*8.f;
            float ey=sy0+46.f+hashf(tt,7)*18.f;
            lineAdd(fb,W,H, sx0,sy0, (sx0+ex)*0.5f+6.f*sinf((float)t*1.1f+tt),(sy0+ey)*0.5f,
                    br,bg,bb, 0.30f*A, 1.2f);
            lineAdd(fb,W,H, (sx0+ex)*0.5f+6.f*sinf((float)t*1.1f+tt),(sy0+ey)*0.5f, ex,ey,
                    br,bg,bb, 0.22f*A, 1.0f);
        }
    }

    // ---- SCHOOLS OF FISH: swim across, dart & turn, shift on beat/onset ----
    for(size_t fi=0; fi<s->fish.size(); ++fi){
        Fish& f=s->fish[fi];
        float margin=60.f;
        float span=W+2.f*margin;
        // faster on beat; a little dart wobble in speed
        float sp=f.speed*(1.f + 0.9f*beat + 0.4f*onset);
        float dart=1.f + 0.35f*sinf((float)t*3.0f + f.phase*6.28f);
        float travel=fmodf(f.phase*span + (float)t*sp*dart, span);
        float x = (f.dir>0)? (travel-margin) : (W+margin-travel);
        // school shifts vertically on beats + steady bob
        float shift = 14.f*beat*sinf(f.school*1.9f + (float)t*0.8f);
        float y = f.y + f.bob*sinf((float)t*1.6f + f.phase*6.28f) + shift;
        // instantaneous turn: swim direction can briefly flip for a dart
        int dir=f.dir;
        float sz=f.size;
        float r,g,b;
        if(f.hue<0.5f){ r=1.0f; g=0.52f; b=0.18f; }       // warm orange (pops on blue)
        else          { r=0.80f; g=0.90f; b=1.0f; }       // silver-blue
        float a=clampf(0.92f*A,0.f,1.f);
        // body
        ellipseOver(fb,W,H, x,y, sz, sz*0.5f, r,g,b, a);
        // tail (triangle behind, opposite the swim direction)
        float backX = x - dir*sz*0.95f;
        triOver(fb,W,H, backX, y,
                backX - dir*sz*0.9f, y - sz*0.55f,
                backX - dir*sz*0.9f, y + sz*0.55f,
                r*0.85f,g*0.85f,b*0.85f, a);
        // eye
        putOver(fb,W,H, (int)(x+dir*sz*0.55f), (int)(y-sz*0.15f), 0.03f,0.03f,0.05f, a);
        // tiny sheen
        cv->add_glow(cv, x+dir*sz*0.2f, y-sz*0.3f, r,g,b, 0.10f*A, sz*0.8f);
    }

    // ---- RISING BUBBLE STREAMS (rise faster & denser with treble) ----
    int perStream = 7 + (int)(6.f*tre);                   // denser with treble
    float riseBase = 40.f + 90.f*tre;                     // faster with treble
    for(int stm=0; stm<s->nStreams; ++stm){
        float baseX = (stm+0.5f)/s->nStreams*W + (hashf(stm,30)-0.5f)*30.f;
        float spd = riseBase*(0.7f+0.6f*hashf(stm,31));
        float travel=H+40.f;
        for(int q=0;q<perStream;q++){
            float ph = hashf(stm, 40+q);
            float yy = H+20.f - fmodf(ph*travel + (float)t*spd, travel);
            // gentle horizontal wobble as they rise
            float xx = baseX + sinf(yy*0.05f + ph*6.28f + (float)t*1.2f)*6.f;
            float rad = 1.4f + hashf(stm,50+q)*2.6f;
            cv->add_glow(cv, xx,yy, 0.7f,0.95f,1.0f, 0.28f*A, rad*2.2f);
            putAdd(fb,W,H,(int)xx,(int)yy, 0.9f,1.0f,1.0f, 0.9f*A);        // bright core
            putAdd(fb,W,H,(int)(xx-rad*0.4f),(int)(yy-rad*0.4f), 1.0f,1.0f,1.0f, 0.6f*A); // highlight
        }
    }

    // ---- PLANKTON: tiny drifting specks ----
    for(int i=0;i<s->nPlank;i++){
        float bx=hashf(i,60), by=hashf(i,61);
        float dx=sinf((float)t*0.3f + i*0.7f)*10.f;
        float dy=cosf((float)t*0.22f + i*1.3f)*8.f + (float)t*(4.f+8.f*hashf(i,62));
        float x=fmodf(bx*W + dx, (float)W);
        float y=fmodf(by*H + dy, (float)H); if(x<0)x+=W; if(y<0)y+=H;
        float tw=0.4f+0.6f*fabsf(sinf((float)t*2.0f + i));
        putAdd(fb,W,H,(int)x,(int)y, 0.7f,0.95f,1.0f, (0.10f+0.18f*tw)*A);
    }
}
