// hell.cpp -- "Hell" scene plugin.
// An infernal underworld: a blackened jagged cavern of magma-cracked rock over a
// glowing river/lake of molten lava, towering fire pillars and licking flames,
// drifting embers and sparks, brimstone smoke, and a looming horned demonic
// silhouette with burning eyes brooding above a hellgate, all lit blood-red.
//
// All animation is derived analytically from the absolute time p->time, so any
// frame renders correctly without simulation history (offline render + seeking).
// The additive HDR buffer is tone-mapped by the player (bloom/shake/grain); all
// output is scaled by p->alpha.

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
// smooth value noise in 1D (for flickering flame widths / cavern rim)
static inline float vnoise(float x, uint32_t seed){
    int i = (int)floorf(x); float f = x - i;
    float a = hashf((uint32_t)i, seed), b = hashf((uint32_t)(i+1), seed);
    float u = f*f*(3.f-2.f*f);
    return mixf(a,b,u);
}
// additive HDR write
static inline void putAdd(float* fb, int W, int H, int x, int y,
                          float r, float g, float b, float k){
    if ((unsigned)x >= (unsigned)W || (unsigned)y >= (unsigned)H) return;
    size_t i = ((size_t)y*W + x)*3;
    fb[i] += r*k; fb[i+1] += g*k; fb[i+2] += b*k;
}
// OPAQUE composite: fb = fb*(1-a) + rgb*a. Stamps dark rock/silhouette so it
// reads as black even where lava glow is behind it.
static inline void putOver(float* fb, int W, int H, int x, int y,
                           float r, float g, float b, float a){
    if ((unsigned)x >= (unsigned)W || (unsigned)y >= (unsigned)H) return;
    size_t i=((size_t)y*W+x)*3; float ia=1.f-a;
    fb[i]=fb[i]*ia + r*a; fb[i+1]=fb[i+1]*ia + g*a; fb[i+2]=fb[i+2]*ia + b*a;
}
static inline void lineAdd(float* fb, int W, int H, float x0,float y0,float x1,float y1,
                           float r,float g,float b,float k){
    float dx=x1-x0, dy=y1-y0; float len=sqrtf(dx*dx+dy*dy);
    int n=(int)len+1; if(n<1)n=1;
    for(int i=0;i<=n;i++){ float t=(float)i/n; putAdd(fb,W,H,(int)(x0+dx*t),(int)(y0+dy*t),r,g,b,k); }
}
// map a 0..1 "heat" to an infernal color (deep blood-red -> orange -> yellow-white)
static inline void fireColor(float h, float& r, float& g, float& b){
    h = clampf(h,0.f,1.f);
    r = mixf(0.55f, 1.00f, clampf(h*1.6f,0.f,1.f));
    g = mixf(0.02f, 0.85f, h*h);
    b = mixf(0.00f, 0.45f, clampf((h-0.6f)/0.4f,0.f,1.f)*clampf((h-0.6f)/0.4f,0.f,1.f));
}

struct Spark { float phase, x0, vx, vy, hue, siz; };
struct State {
    int W, H;
    std::vector<Spark> ember;   // drifting embers rising from the lava
    std::vector<Spark> spark;   // fast bright sparks bursting on beats
};

VFX_EXPORT const VfxPluginInfo* vfx_plugin_info(void){
    static VfxPluginInfo INFO = {
        VFX_PLUGIN_ABI, "Hell", "veffects",
        "An infernal underworld of molten lava rivers, towering fire pillars, drifting embers, brimstone smoke and a looming horned demon, all driven by the audio."
    };
    return &INFO;
}

VFX_EXPORT void* vfx_plugin_create(int W, int H){
    State* s = new State();
    s->W = W; s->H = H;
    const int NE = 260;
    s->ember.resize(NE);
    for(int i=0;i<NE;i++){
        Spark& e = s->ember[i];
        e.phase = hashf(i,11);
        e.x0    = hashf(i,12);                 // 0..1 across width
        e.vx    = (hashf(i,13)-0.5f)*2.f;      // lateral drift factor
        e.vy    = 0.5f + 0.7f*hashf(i,14);     // rise speed factor
        e.hue   = hashf(i,15);
        e.siz   = 0.6f + hashf(i,16);
    }
    const int NS = 200;
    s->spark.resize(NS);
    for(int i=0;i<NS;i++){
        Spark& e = s->spark[i];
        e.phase = hashf(i+999,21);
        e.x0    = hashf(i+999,22);
        e.vx    = (hashf(i+999,23)-0.5f)*2.f;
        e.vy    = 0.7f + 0.8f*hashf(i+999,24);
        e.hue   = hashf(i+999,25);
        e.siz   = 0.5f + hashf(i+999,26);
    }
    return s; // never NULL
}
VFX_EXPORT void vfx_plugin_destroy(void* st){ delete (State*)st; }

VFX_EXPORT void vfx_plugin_render(void* st, const VfxCanvas* cv, const VfxParams* p){
    State* s = (State*)st;
    float* fb = cv->fb; int W = cv->width, H = cv->height;
    float A = p->alpha; if(A<=0.f) return;
    double t = p->time;
    float bass=p->bass, mid=p->mid, tre=p->treble, rms=p->rms, beat=p->beat, onset=p->onset;
    int NB = p->bandCount;

    // ---- audio-driven intensities ---------------------------------------
    float glow    = 0.30f + 0.85f*bass + 0.45f*rms;          // base lava/fire glow
    float leap    = 1.f + 1.6f*beat + 1.4f*onset;            // flame surge on beats
    float throb   = 0.5f + 1.1f*rms;                         // menacing overall throb
    float billow  = 0.45f + 1.1f*mid;                        // smoke billow
    float sparkle = 0.4f + 1.3f*tre;                         // ember/spark sparkle

    const float lavaY = H*0.72f;                             // horizon of the lava lake

    // ---- 1) sky/cavern void: near-black with a hot updraft glow from below
    for(int y=0;y<(int)lavaY;y++){
        float v = (float)y/lavaY;                            // 0 top .. 1 at lava line
        // rising warmth toward the lava; a slow ominous throb pulse
        float up = v*v;
        float base = 0.010f + 0.05f*up*glow*throb;
        float rr = base*1.0f + 0.010f;
        float gg = base*0.30f + 0.004f;
        float bb = base*0.06f + 0.006f;
        for(int x=0;x<W;x+=1) putAdd(fb,W,H,x,y, rr,gg,bb, A);
    }

    // ---- 2) molten lava lake with a glowing cracked surface --------------
    for(int y=(int)lavaY;y<H;y++){
        float d = (float)(y - lavaY)/(H - lavaY);            // 0 at shore .. 1 foreground
        // heat waves shimmering across the lava; brighter in the near field
        for(int x=0;x<W;x++){
            float ripple = sinf(x*0.05f + (float)t*1.7f + y*0.11f)
                         + 0.6f*sinf(x*0.13f - (float)t*2.3f + y*0.07f);
            float veins = vnoise(x*0.04f + (float)t*0.3f, 7u + (uint32_t)y);
            float hot = clampf(0.35f + 0.30f*d + 0.18f*ripple + 0.35f*veins*veins, 0.f, 1.f);
            hot = clampf(hot * (0.8f + 0.9f*glow), 0.f, 1.2f);
            float rr,gg,bb; fireColor(hot, rr,gg,bb);
            float k = (0.10f + 0.55f*d) * (0.7f + 0.8f*glow) * throb;
            putAdd(fb,W,H,x,y, rr,gg,bb, k*A);
        }
    }
    // bright molten shoreline where lava meets rock
    for(int x=0;x<W;x++){
        float wob = vnoise(x*0.06f, 3u)*6.f + sinf(x*0.09f+(float)t*1.3f)*3.f;
        int sy = (int)(lavaY + wob);
        float rr,gg,bb; fireColor(0.95f, rr,gg,bb);
        putAdd(fb,W,H,x,sy,   rr,gg,bb, (0.9f+1.3f*glow)*A);
        putAdd(fb,W,H,x,sy+1, 1.0f,0.55f,0.15f, (0.6f+0.9f*glow)*A);
        cv->add_glow(cv, (float)x, (float)sy, 1.0f,0.4f,0.08f, 0.02f*(0.6f+glow)*A, 8.f);
    }

    // ---- 3) glowing magma cracks in the blackened cavern walls -----------
    // jagged rock ledges on left and right with veins of molten light
    for(int side=0; side<2; side++){
        float sgn = side? 1.f : -1.f;
        float baseX = side? (float)W : 0.f;
        for(int c=0;c<3;c++){
            float sx = baseX - sgn*(20.f + c*40.f + vnoise(c*3.7f, 5u+side)*30.f);
            float sy = mixf(H*0.10f, lavaY, hashf(c*7+side,41));
            float x = sx, y = sy;
            int segs = 10;
            for(int j=0;j<segs;j++){
                float nx = x + sgn*(6.f+hashf((uint32_t)(c*17+j),50u+side)*22.f);
                float ny = y + (hashf((uint32_t)(c*13+j),60u+side)-0.35f)*30.f;
                float ph = 0.5f+0.5f*sinf((float)t*3.f + j*0.9f + c*2.f + side*1.5f);
                float rr,gg,bb; fireColor(0.5f + 0.4f*ph, rr,gg,bb);
                lineAdd(fb,W,H, x,y,nx,ny, rr,gg,bb, (0.25f+0.7f*glow)*ph*A);
                x=nx; y=ny;
            }
        }
    }

    // ---- 4) towering fire pillars / flame columns (band-driven) ----------
    int NP = 5;
    for(int c=0;c<NP;c++){
        float fx = mixf(W*0.12f, W*0.88f, (NP>1)?(float)c/(NP-1):0.5f)
                 + sinf((float)t*0.4f + c*1.3f)*8.f;
        float bandGain = 1.f;
        if(NB>0){ int bi=(c*NB)/NP; if(bi>=NB)bi=NB-1; bandGain = 0.5f + 1.7f*p->bands[bi]; }
        float baseH = H*0.20f;
        float colH = baseH * (0.7f + 0.9f*glow) * leap * (0.6f+0.8f*bandGain);
        float baseY = lavaY + 4.f;
        int steps = (int)(colH*0.9f); if(steps<1) steps=1;
        for(int i=0;i<steps;i++){
            float u = (float)i/steps;                        // 0 base .. 1 tip
            float rise = u*colH;
            float y = baseY - rise;
            // flame tapers and sways more toward the tip; flickers over time
            float sway = sinf((float)t*4.5f + c*2.1f + u*5.f)*14.f*u
                       + vnoise(u*6.f + (float)t*5.f, 80u+c)*20.f*u;
            float x = fx + sway;
            float width = mixf(9.f, 1.5f, u) * (0.7f+0.6f*bandGain);
            float flick = 0.6f + 0.4f*vnoise((float)t*9.f + c*3.f + u*3.f, 90u+c);
            float hot = clampf((1.f-u)*1.1f + 0.2f, 0.f, 1.f);
            float rr,gg,bb; fireColor(hot, rr,gg,bb);
            float k = (0.35f + 0.9f*glow) * (1.f-u*0.75f) * flick * leap;
            cv->add_glow(cv, x, y, rr,gg,bb, k*A*0.6f, width);
        }
        // hot glowing root pooled at the base of each pillar
        cv->add_glow(cv, fx, baseY, 1.0f,0.5f,0.12f, (0.5f+1.1f*glow+0.8f*beat)*A, 22.f*(0.7f+0.6f*bandGain));
    }

    // ---- 5) drifting embers rising and cooling ---------------------------
    const float ELIFE = 3.2f;
    for(size_t i=0;i<s->ember.size();i++){
        Spark& e = s->ember[i];
        float age = fmodf((float)t*(0.55f+0.5f*e.vy) + e.phase*ELIFE, ELIFE);
        float a01 = age/ELIFE;
        float rise = (60.f + 55.f*e.vy)*age;
        float y = lavaY - rise + 10.f;
        float x = e.x0*W + e.vx*(18.f+70.f*a01) + sinf(age*2.3f + i)*10.f;
        float flick = 0.5f + 0.5f*sinf((float)t*20.f + i*2.3f + e.hue*6.28f);
        float k = (0.30f + 1.0f*sparkle) * (1.f - a01) * flick;
        float rr,gg,bb; fireColor(0.5f + 0.4f*e.hue, rr,gg,bb);
        cv->add_glow(cv, x, y, rr,gg,bb, k*A*0.7f, (1.0f + 1.0f*flick)*e.siz);
    }

    // ---- 6) fast bright sparks bursting up on beats/onsets ---------------
    float burst = clampf(beat*1.1f + onset*0.9f, 0.f, 1.4f);
    if(burst > 0.05f){
        const float SLIFE = 1.1f;
        for(size_t i=0;i<s->spark.size();i++){
            Spark& e = s->spark[i];
            float age = fmodf((float)t*(1.1f+0.6f*e.vy) + e.phase*SLIFE, SLIFE);
            float a01 = age/SLIFE;
            float rise = (150.f+120.f*e.vy)*age - 90.f*age*age; // ballistic
            float y = lavaY - rise;
            float x = e.x0*W + e.vx*(40.f+60.f*a01);
            float k = burst * (1.f - a01) * (0.6f + 0.6f*sparkle);
            float rr,gg,bb; fireColor(0.8f + 0.2f*e.hue, rr,gg,bb);
            cv->add_glow(cv, x, y, rr,gg,bb, k*A*0.6f, 1.1f*e.siz);
        }
    }

    // ---- 7) brimstone smoke billowing up, lit red on its underside -------
    {
        int NA = 60;
        for(int i=0;i<NA;i++){
            float ph = hashf(i+40,31);
            float ALIFE = 6.0f;
            float age = fmodf((float)t*(0.6f+0.3f*hashf(i,32)) + ph*ALIFE, ALIFE);
            float a01 = age/ALIFE;
            float rise = 55.f*age*(0.9f+0.4f*billow);
            float y = lavaY - rise;
            float spread = (40.f + 170.f*a01);
            float sway = sinf(age*1.1f + i*1.7f) * 26.f*a01;
            float x = mixf(W*0.15f, W*0.85f, hashf(i,33)) + sway + (hashf(i,34)-0.5f)*spread*0.5f;
            float rad = 26.f + 70.f*a01;
            float underlit = clampf(1.3f - 2.6f*a01, 0.f, 1.f);
            // dark, blood-red brimstone: dense only low (underlit), fades to near-black
            float dens = (0.028f + 0.055f*billow) * (1.f - 0.6f*a01);
            float rr = dens*(0.55f + 1.7f*underlit);
            float gg = dens*(0.14f + 0.30f*underlit);
            float bb = dens*(0.10f + 0.04f*underlit);
            cv->add_glow(cv, x, y, rr,gg,bb, A, rad);
        }
    }

    // ---- 8) looming horned demon silhouette above a hellgate -------------
    // Drawn opaque so it stays a black shadow against the fire glow, with two
    // burning eyes that pulse with the beat.
    {
        float dcx = W*0.5f;
        float headTop = H*0.14f + 6.f*sinf((float)t*0.5f);   // slow ominous bob
        float headBot = H*0.44f;
        float headR   = W*0.14f;
        // head: a rounded dark mass
        int y0=(int)(headTop-headR*0.2f), y1=(int)headBot;
        for(int y=y0;y<y1;y++){
            float ty = (float)(y - headTop)/(headBot - headTop); // 0 top..1 chin
            // width bulges at the brow, narrows to a jaw
            float wprof = sinf(clampf(ty,0.f,1.f)*3.14159f*0.9f + 0.15f);
            float hw = headR * (0.45f + 0.85f*wprof);
            for(int x=(int)(dcx-hw); x<=(int)(dcx+hw); x++){
                putOver(fb,W,H,x,y, 0.008f,0.002f,0.004f, 0.94f);
            }
        }
        // two curved horns sweeping up-and-out from the brow
        for(int side=0; side<2; side++){
            float sgn = side? 1.f : -1.f;
            float hx = dcx + sgn*headR*0.55f;
            float hy = headTop + headR*0.15f;
            int hs=64;
            for(int j=0;j<=hs;j++){
                float u=(float)j/hs;
                float nx = hx + sgn*(u*headR*0.9f + u*u*headR*0.4f);
                float ny = hy - u*headR*1.5f + u*u*headR*0.4f;
                float w = mixf(11.f, 1.2f, u*u*0.6f+u*0.4f);   // stay thick, taper to a point
                for(int oy=-(int)w; oy<=(int)w; oy++)
                    for(int ox=-(int)w; ox<=(int)w; ox++)
                        if((float)(ox*ox+oy*oy)<=w*w)
                            putOver(fb,W,H,(int)nx+ox,(int)ny+oy, 0.010f,0.003f,0.004f, 0.95f);
                // faint ember rim-light along the outer horn edge
                putAdd(fb,W,H,(int)(nx+sgn*w),(int)ny, 0.9f,0.25f,0.05f, 0.10f*glow*A);
            }
        }
        // burning eyes
        float eyePulse = 0.6f + 0.9f*beat + 0.5f*throb;
        for(int side=0; side<2; side++){
            float sgn = side? 1.f : -1.f;
            float ex = dcx + sgn*headR*0.34f;
            float ey = headTop + (headBot-headTop)*0.42f;
            float rr,gg,bb; fireColor(0.85f, rr,gg,bb);
            cv->add_glow(cv, ex, ey, rr,gg,bb, (0.8f+1.6f*eyePulse)*A, 5.f+3.f*beat);
            cv->add_glow(cv, ex, ey, 1.0f,0.9f,0.5f, (0.5f+1.0f*eyePulse)*A, 2.2f);
        }
        // a menacing grin of glowing cracks under the brow
        for(int m=0;m<5;m++){
            float mu=(float)m/4.f;
            float mx = dcx + (mu-0.5f)*headR*0.9f;
            float my = headTop + (headBot-headTop)*0.68f + sinf(mu*3.14159f)*6.f;
            float rr,gg,bb; fireColor(0.7f, rr,gg,bb);
            putAdd(fb,W,H,(int)mx,(int)my, rr,gg,bb, (0.4f+0.9f*glow)*A);
            cv->add_glow(cv, mx,my, 1.0f,0.4f,0.1f, 0.15f*glow*A, 3.f);
        }

        // hellgate: two dark jagged spires flanking the demon, framing the fire
        for(int side=0; side<2; side++){
            float sgn = side? 1.f : -1.f;
            float gx = dcx + sgn*W*0.30f;
            for(int y=(int)(H*0.20f); y<(int)lavaY; y++){
                float tv=(float)(y-H*0.20f)/(lavaY-H*0.20f);
                float jag = vnoise(tv*7.f, 100u+side)*18.f;
                float hw = mixf(10.f, 34.f, tv) + jag;
                for(int x=(int)(gx-hw); x<=(int)(gx+hw); x++)
                    putOver(fb,W,H,x,y, 0.012f,0.004f,0.005f, 0.85f);
                // molten edge line facing the center
                float rr,gg,bb; fireColor(0.4f+0.4f*(0.5f+0.5f*sinf((float)t*2.f+tv*6.f)), rr,gg,bb);
                putAdd(fb,W,H,(int)(gx - sgn*hw), y, rr,gg,bb, (0.2f+0.6f*glow)*A);
            }
        }
    }

    // ---- 9) overall infernal vignette throb (keeps edges hot-dark) -------
    {
        float pulse = 0.5f + 0.5f*sinf((float)t*1.5f);
        float k = (0.015f + 0.03f*throb*pulse)*A;
        for(int x=0;x<W;x+=3){
            putAdd(fb,W,H,x,H-1, 1.0f,0.25f,0.05f, k);
            putAdd(fb,W,H,x,0,   0.4f,0.08f,0.02f, k*0.5f);
        }
    }
}
