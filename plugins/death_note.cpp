// death_note.cpp -- "Death Note" scene plugin.
// A dark gothic world: an open ruled notebook whose blood-red names scratch
// themselves in line by line, a tumbling red apple, ornate cross/filigree
// framing, a pair of glowing Shinigami eyes, heavy rain and a spreading ink
// stain. Black + blood-red + bone-white + sickly gold.
//
// All animation is derived from the absolute time p->time (analytic), so any
// frame renders correctly without simulation history (seek / offline render).

#include "veffects_plugin.h"
#include <cmath>
#include <cstdint>
#include <cstring>
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
static inline void lineAdd(float* fb, int W, int H, float x0,float y0,float x1,float y1,
                           float r,float g,float b,float k){
    float dx=x1-x0, dy=y1-y0; float len=sqrtf(dx*dx+dy*dy);
    int n=(int)len+1; if(n<1)n=1;
    for(int i=0;i<=n;i++){ float t=(float)i/n; putAdd(fb,W,H,(int)(x0+dx*t),(int)(y0+dy*t),r,g,b,k); }
}
// soft filled disk (falloff toward the rim)
static inline void diskAdd(float* fb, int W, int H, float cx, float cy, float rad,
                           float r, float g, float b, float k){
    int x0=(int)(cx-rad), x1=(int)(cx+rad), y0=(int)(cy-rad), y1=(int)(cy+rad);
    if(x0<0)x0=0; if(y0<0)y0=0; if(x1>=W)x1=W-1; if(y1>=H)y1=H-1;
    float r2=rad*rad;
    for(int y=y0;y<=y1;y++)for(int x=x0;x<=x1;x++){
        float ddx=x-cx, ddy=y-cy, d2=ddx*ddx+ddy*ddy;
        if(d2>r2) continue;
        float f=1.f-sqrtf(d2)/rad;      // 1 center -> 0 rim
        putAdd(fb,W,H,x,y, r,g,b, k*f*f);
    }
}

struct RainDrop { float x, phase, speed, len; };
struct State {
    int W, H;
    std::vector<RainDrop> rain;
};

static VfxPluginInfo INFO = {
    VFX_PLUGIN_ABI, "Death Note", "veffects",
    "A gothic open notebook scratches blood-red names line by line beneath Shinigami eyes, a tumbling apple, cross filigree, rain and a spreading ink stain."
};
VFX_EXPORT const VfxPluginInfo* vfx_plugin_info(void){ return &INFO; }

VFX_EXPORT void* vfx_plugin_create(int W, int H){
    State* s = new State();
    s->W=W; s->H=H;
    s->rain.resize(150);
    for(int i=0;i<(int)s->rain.size();i++){
        RainDrop& d=s->rain[i];
        d.x     = hashf(i,11)*W;
        d.phase = hashf(i,12);
        d.speed = 260.f + hashf(i,13)*260.f;
        d.len   = 10.f + hashf(i,14)*22.f;
    }
    return s;
}
VFX_EXPORT void vfx_plugin_destroy(void* st){ delete (State*)st; }

VFX_EXPORT void vfx_plugin_render(void* st, const VfxCanvas* cv, const VfxParams* p){
    State* s=(State*)st;
    float* fb=cv->fb; int W=cv->width, H=cv->height;
    float A=p->alpha;
    float bass=p->bass, mid=p->mid, tre=p->treble, rms=p->rms;
    float beat=p->beat, onset=p->onset;
    double t=p->time;

    // ---- heartbeat throb (lub-dub) driving the whole scene ----
    float hbFreq = (p->bpm>20.f ? p->bpm/60.f : 1.05f) * 0.5f; // one heartbeat per ~2 beats
    float ph = (float)(t*hbFreq - floor(t*hbFreq));
    float lub = expf(-((ph)*(ph))/(2.f*0.045f*0.045f));
    float dpb = ph-0.20f;
    float dub = 0.65f*expf(-(dpb*dpb)/(2.f*0.040f*0.040f));
    float hb  = clampf(lub+dub, 0.f, 1.f);
    float throb = 1.f + 0.30f*bass + 0.55f*hb*(0.35f+bass);   // global brightness pulse

    // ---- 1) background: near-black with a throbbing blood vignette ----
    float cx=W*0.5f, cy=H*0.5f;
    for(int y=0;y<H;y+=2){
        for(int x=0;x<W;x+=2){
            float dx=(x-cx)/(W*0.5f), dy=(y-cy)/(H*0.5f);
            float d=dx*dx+dy*dy;
            float glow=clampf(1.f-d,0.f,1.f);
            float red=(0.010f + 0.055f*glow*glow*throb);
            putAdd(fb,W,H,x,y, red, red*0.05f, red*0.04f, A);
            // faint bone haze up high (where the eyes live)
            float hy=1.f-(float)y/H;
            putAdd(fb,W,H,x,y, 0.006f*hy, 0.006f*hy, 0.007f*hy, A);
        }
    }

    // ---- 2) heavy rain (density scales with treble) ----
    int nrain=(int)(30 + tre*(float)(s->rain.size()-30));
    if(nrain>(int)s->rain.size()) nrain=(int)s->rain.size();
    for(int i=0;i<nrain;i++){
        RainDrop& d=s->rain[i];
        float travel=H+d.len;
        float yy=fmodf(d.phase*travel + (float)t*d.speed, travel);
        float slant=6.f; // gentle wind
        float x0=d.x, x1=d.x - slant;
        float k=(0.05f + 0.05f*tre);
        lineAdd(fb,W,H, x0, yy-d.len, x1, yy, 0.45f,0.50f,0.62f, k*A);
    }

    // ---- 3) the open Death Note (two ruled pages) ----
    int bx0=150, bx1=490, by0=150, by1=372;      // book bounds
    int gut=W/2;                                  // gutter (spine) center
    float pageK = 0.085f*(0.85f+0.15f*hb);        // aged parchment glow
    for(int y=by0;y<by1;y++){
        for(int x=bx0;x<bx1;x++){
            // shade: darker toward outer edges and toward the gutter (page curl)
            float ex = clampf(1.f - fabsf((x-gut)/(float)(bx1-gut))*0.35f, 0.f, 1.f);
            float gutd = 1.f - expf(-fabsf((float)(x-gut))/8.f)*0.75f; // spine shadow
            float ey = clampf(1.f - fabsf((y-(by0+by1)*0.5f)/(float)((by1-by0)*0.5f))*0.25f, 0.f, 1.f);
            float sh = ex*ey*gutd;
            putAdd(fb,W,H,x,y, pageK*sh, pageK*0.94f*sh, pageK*0.78f*sh, A);
        }
    }
    // dark leather edge + spine
    for(int y=by0-2;y<by1+2;y++){
        putAdd(fb,W,H,bx0-1,y, 0.02f,0.005f,0.005f, A);
        putAdd(fb,W,H,bx1,y,   0.02f,0.005f,0.005f, A);
        putAdd(fb,W,H,gut,y,   0.f,0.f,0.f, A); // gutter line kept dark
    }

    // ruled lines + the writing that fills in line by line
    int linesPerPage=12;
    int lineTop=by0+16, lineSpacing=(by1-16-lineTop)/linesPerPage;
    // one page-fill cycle: names scratch top->bottom, then the page "turns"
    float cycleLen=(float)(linesPerPage*2)+3.f;
    float cyclePos=fmodf((float)t*0.85f, cycleLen);   // analytic writing progress
    // a fresh scratch flare on each onset/beat
    float scratch=clampf(onset*1.4f + beat*0.4f, 0.f, 1.f);

    for(int page=0;page<2;page++){
        int px0 = page==0 ? bx0+14 : gut+14;
        int px1 = page==0 ? gut-14 : bx1-14;
        for(int l=0;l<linesPerPage;l++){
            int ly=lineTop + l*lineSpacing;
            // ruled line
            for(int x=px0;x<px1;x++)
                putAdd(fb,W,H,x,ly+lineSpacing-3, 0.05f,0.045f,0.035f, A);

            int globalLine = page*linesPerPage + l;
            float prog = cyclePos - (float)globalLine;   // >1 done, 0..1 writing, <0 empty
            if(prog<=0.f) continue;
            float frac = prog>=1.f ? 1.f : prog;
            bool head = (prog>0.f && prog<1.f);

            // handwriting: jittery blood-red scribble along the line
            uint32_t seed = hashu((uint32_t)globalLine*2654435761U);
            int segs=44;
            int wy=ly+lineSpacing-6;
            float baseK = 0.55f + 0.25f*bass;
            if(head) baseK += 1.1f*scratch;              // the pen bites in on the beat
            float px=px0, ppy=wy;
            for(int sI=0;sI<segs;sI++){
                float u=(float)sI/segs;
                if(u>frac) break;
                float nx=px0+(px1-px0)*u;
                // cursive wobble + per-stroke jitter -> looks like written names
                float wob=sinf(u*38.f + hashf(seed,sI)*6.28f)*3.2f
                        + (hashf(seed,100+sI)-0.5f)*2.2f;
                // word gaps
                float gap=hashf(seed, 200+(sI/6));
                float ny=wy+wob;
                float k=baseK;
                if(gap<0.14f){ px=nx; ppy=ny; continue; }  // pen lift between words
                lineAdd(fb,W,H, px,ppy, nx,ny, 0.85f,0.06f,0.04f, k*A);
                if(head && sI>segs*frac-3.f) // wet ink at the pen tip
                    cv->add_glow(cv, nx,ny, 0.9f,0.1f,0.06f, 0.5f*scratch*A, 4.f);
                px=nx; ppy=ny;
            }
        }
    }

    // ---- 4) spreading red ink stain (radius scales with rms) ----
    float stCycle=fmodf((float)t, 9.f);
    float stR=(6.f + stCycle*3.2f + rms*26.f);     // grows over the cycle + loudness
    float sx= (float)bx0+70.f, sy=(float)by1-34.f;  // bleeding up from lower-left page
    float stFade=clampf(1.f-stCycle/9.f,0.f,1.f)*0.6f + 0.4f;
    // irregular blotch: a few overlapping soft disks with hashed offsets
    for(int b=0;b<7;b++){
        float an=b*0.9f + (float)t*0.05f;
        float rr=stR*(0.4f+hashf((uint32_t)b,7)*0.8f);
        float ox=cosf(an)*stR*0.5f, oy=sinf(an)*stR*0.35f;
        diskAdd(fb,W,H, sx+ox, sy+oy, rr, 0.55f,0.02f,0.02f, 0.06f*stFade*A);
    }
    cv->add_glow(cv, sx,sy, 0.6f,0.03f,0.02f, 0.25f*stFade*A, stR*0.9f);

    // ---- 5) tumbling red apple ----
    float apC=fmodf((float)t, 5.5f);
    float ay=-24.f + apC*(H+48.f)/5.5f;
    float ax=cx + sinf((float)t*1.15f)*60.f + (apC-2.75f)*8.f;
    float rot=(float)t*4.2f;
    float aR=13.f;
    // motion-blur ghosts along the fall path
    for(int g=1;g<=4;g++){
        float gc=apC - g*0.10f; if(gc<0.f) continue;
        float gy=-24.f + gc*(H+48.f)/5.5f;
        float gx=cx + sinf((float)(t-g*0.10f)*1.15f)*60.f + (gc-2.75f)*8.f;
        diskAdd(fb,W,H, gx,gy, aR*0.9f, 0.6f,0.03f,0.02f, 0.12f*A);
    }
    // apple body
    diskAdd(fb,W,H, ax,ay, aR, 0.95f,0.05f,0.03f, 1.35f*throb*A);
    diskAdd(fb,W,H, ax,ay, aR*0.72f, 1.0f,0.12f,0.06f, 0.9f*throb*A);
    // rotating bone highlight (sells the tumble)
    float hx=ax+cosf(rot)*aR*0.4f, hy2=ay+sinf(rot)*aR*0.4f;
    diskAdd(fb,W,H, hx,hy2, aR*0.30f, 1.0f,0.85f,0.7f, 0.5f*A);
    // dark blossom notch + stem + leaf
    diskAdd(fb,W,H, ax,ay+aR*0.55f, aR*0.22f, 0.f,0.f,0.f, 0.6f*A);
    lineAdd(fb,W,H, ax,ay-aR*0.8f, ax+2.f,ay-aR*1.5f, 0.35f,0.18f,0.05f, 0.9f*A);
    lineAdd(fb,W,H, ax+2.f,ay-aR*1.3f, ax+9.f,ay-aR*1.7f, 0.55f,0.45f,0.1f, 0.7f*A);
    cv->add_glow(cv, ax,ay, 0.9f,0.04f,0.03f, 0.22f*throb*A, aR*1.25f);

    // ---- 6) glowing Shinigami eyes in the dark (flare with beat) ----
    float eyeFlare=0.5f + 2.6f*beat + 0.6f*hb;
    float esx=W*0.5f, esy=92.f;
    float egap=44.f + 3.f*sinf((float)t*0.8f);
    float sway=sinf((float)t*0.5f)*4.f;
    for(int e=0;e<2;e++){
        float ex=esx + (e? egap : -egap) + sway;
        float ey=esy + (e? 2.f*sinf((float)t*0.7f) : -2.f*sinf((float)t*0.7f));
        // outer menacing halo
        cv->add_glow(cv, ex,ey, 1.0f,0.06f,0.04f, 0.35f*eyeFlare*A, 26.f);
        // eyeball
        diskAdd(fb,W,H, ex,ey, 11.f, 0.95f,0.08f,0.05f, (0.9f+0.6f*eyeFlare)*A);
        diskAdd(fb,W,H, ex,ey, 6.5f, 1.0f,0.20f,0.10f, (0.8f+0.8f*eyeFlare)*A);
        // vertical slit pupil (dark)
        for(int yy=-7;yy<=7;yy++){
            float w=1.6f*(1.f-fabsf((float)yy)/8.f);
            for(float xx=-w;xx<=w;xx+=1.f)
                putAdd(fb,W,H,(int)(ex+xx),(int)(ey+yy), 0.f,0.f,0.f, 0.9f*A);
        }
        // bright core spark
        cv->add_glow(cv, ex,ey, 1.0f,0.4f,0.2f, 0.5f*eyeFlare*A, 3.f);
    }

    // ---- 7) ornate cross + filigree framing (dim sickly gold / bone) ----
    float goldK=(0.11f + 0.06f*hb)*A;
    float gr=1.0f, gg=0.42f, gb=0.05f;   // sickly amber-gold (red-biased so it reads gold, not green)
    // outer double border
    // 2px thick so the player's chromatic aberration can't strip the red channel
    // off the thin vertical edges (which would leave them looking green).
    auto rect=[&](int x0,int y0,int x1,int y1,float k){
        for(int x=x0;x<=x1;x++)for(int o=0;o<2;o++){ putAdd(fb,W,H,x,y0+o,gr,gg,gb,k); putAdd(fb,W,H,x,y1-o,gr,gg,gb,k); }
        for(int y=y0;y<=y1;y++)for(int o=0;o<2;o++){ putAdd(fb,W,H,x0+o,y,gr,gg,gb,k); putAdd(fb,W,H,x1-o,y,gr,gg,gb,k); }
    };
    rect(10,10,W-11,H-11, goldK);
    rect(16,16,W-17,H-17, goldK*0.6f);
    // corner filigree flourishes (quarter arcs + dots)
    auto flourish=[&](float ox,float oy,float sxd,float syd){
        for(int i=0;i<=14;i++){
            float a=(float)i/14*1.5708f;
            float fx=ox + sxd*(22.f*sinf(a));
            float fy=oy + syd*(22.f*(1.f-cosf(a)));
            putAdd(fb,W,H,(int)fx,(int)fy, gr,gg,gb, goldK*1.4f);
            if(i%4==0) cv->add_glow(cv, fx,fy, gr,gg,gb, goldK*0.8f, 2.5f);
        }
    };
    flourish(20,20, 1,1); flourish(W-20,20,-1,1);
    flourish(20,H-20,1,-1); flourish(W-20,H-20,-1,-1);
    // gothic cross above the book (faint, throbbing)
    float crx=cx, cry=110.f;
    float ck=(0.06f + 0.05f*hb)*A;
    lineAdd(fb,W,H, crx, cry-34, crx, cry+30, gr*1.1f,gg,gb, ck*1.5f);   // upright
    lineAdd(fb,W,H, crx-18, cry-14, crx+18, cry-14, gr*1.1f,gg,gb, ck*1.5f); // cross-bar
    // diamond finials
    for(int d=0;d<4;d++){
        float fx = d==0?crx : d==1?crx : d==2?crx-18 : crx+18;
        float fy = d==0?cry-34 : d==1?cry+30 : cry-14;
        if(d==2||d==3) fy=cry-14;
        for(int q=0;q<4;q++){
            float a=q*1.5708f;
            lineAdd(fb,W,H, fx+cosf(a)*3, fy+sinf(a)*3, fx+cosf(a+1.5708f)*3, fy+sinf(a+1.5708f)*3,
                    gr,gg,gb, ck);
        }
    }
    cv->add_glow(cv, crx,cry-14, gr,gg,gb, 0.10f*hb*A, 14.f);
}
