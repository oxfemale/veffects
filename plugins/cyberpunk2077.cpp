// cyberpunk2077.cpp -- "Cyberpunk 2077" scene plugin.
// NIGHT CITY: a dense VERTICAL neon megacity at night. Towering skyscrapers
// packed with lit windows, huge animated holographic billboards (blocky glyph
// panels), flying AV cars crossing with light trails, heavy neon reflections
// and rain, and occasional holo-GLITCH tears. Signature palette: hot cyberpunk
// YELLOW + cyan + magenta/hot-pink on deep blue-black (distinct from Blade
// Runner's amber).
//
// All animation is derived from the absolute time p->time (analytic, no
// cross-frame state), so offline render and seeking reproduce every frame.
// Every contribution is scaled by p->alpha for scene crossfade. The player owns
// tone-mapping, bloom, chromatic aberration and grain; this writes into the
// additive HDR frame buffer.

#include "veffects_plugin.h"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

static inline float clampf(float x, float a, float b){ return x<a?a:(x>b?b:x); }
static inline float smooth01(float x){ x=clampf(x,0.f,1.f); return x*x*(3.f-2.f*x); }
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
static inline void rectFrame(float* fb, int W, int H, int x0,int y0,int x1,int y1,
                             float r,float g,float b,float k){
    for(int x=x0;x<=x1;x++){ putAdd(fb,W,H,x,y0,r,g,b,k); putAdd(fb,W,H,x,y1,r,g,b,k); }
    for(int y=y0;y<=y1;y++){ putAdd(fb,W,H,x0,y,r,g,b,k); putAdd(fb,W,H,x1,y,r,g,b,k); }
}

// Signature Cyberpunk 2077 palette: yellow <-> magenta hero color, cyan accent.
static inline void heroColor(float mixMag, float& r, float& g, float& b){
    // mixMag 0 -> hot yellow, 1 -> magenta/hot-pink
    r = 1.0f;
    g = 0.85f - 0.72f*mixMag;   // yellow keeps green, magenta drops it
    b = 0.02f + 0.62f*mixMag;   // magenta gains blue
}

struct Tower {
    int   x0, x1;      // horizontal footprint
    int   top;         // silhouette top (smaller = taller)
    uint32_t seed;
    int   antenna;
    int   band;        // spectrum band lighting this tower's windows
    int   colr;        // 0 yellow-ish, 1 cyan, 2 magenta window tint
};
struct Board {
    float bx, by;      // center
    float bw, bh;      // half extents
    float phase;
    int   band;
    int   colr;        // 0 yellow, 1 cyan, 2 magenta
    int   central;     // the one big hero ad
    uint32_t seed;
};
struct State {
    int W, H, horizon;
    std::vector<Tower> tow;
    std::vector<int>   topArr;
    std::vector<Board> boards;
    std::vector<float> rowtmp;
};

// soft filled glowing panel body with additive falloff to edges
static void panelFill(float* fb,int W,int H,int cx,int cy,int hw,int hh,
                      float r,float g,float b,float k){
    for(int y=-hh;y<=hh;y++){
        float fy=1.f-fabsf((float)y/hh);
        int py=cy+y;
        for(int x=-hw;x<=hw;x++){
            float fx=1.f-fabsf((float)x/hw);
            float w=fx*fy; w*=w;
            putAdd(fb,W,H,cx+x,py,r,g,b,k*w);
        }
    }
}

// blocky animated glyph grid inside a billboard (holo-ad text panels).
// Cells flip on/off per time + band so the ad "scrolls" / animates.
static void glyphPanel(float* fb,int W,int H,int cx,int cy,int hw,int hh,
                       uint32_t seed,double t,float bandE,
                       float r,float g,float b,float k){
    int x0=cx-hw+2, x1=cx+hw-2, y0=cy-hh+2, y1=cy+hh-2;
    if(x1<=x0||y1<=y0) return;
    int cellW=4, cellH=4;
    int cols=(x1-x0)/cellW; int rows=(y1-y0)/cellH;
    if(cols<1||rows<1) return;
    int frame=(int)(t*6.0);   // animation step
    for(int cyi=0;cyi<rows;cyi++){
        for(int cxi=0;cxi<cols;cxi++){
            uint32_t hs=hashu(seed ^ (uint32_t)(cxi*131u) ^ (uint32_t)(cyi*977u)
                                   ^ (uint32_t)(frame*2246822519u));
            float on=hashf(hs,7);
            // energy raises how many cells are lit
            float thr=0.42f - 0.30f*bandE;
            if(on < thr) continue;
            int px=x0+cxi*cellW, py=y0+cyi*cellH;
            float cell=(0.6f+0.9f*on);
            putAdd(fb,W,H,px,  py,  r,g,b,k*cell);
            putAdd(fb,W,H,px+1,py,  r,g,b,k*cell*0.9f);
            putAdd(fb,W,H,px,  py+1,r,g,b,k*cell*0.9f);
            putAdd(fb,W,H,px+1,py+1,r,g,b,k*cell*0.8f);
        }
    }
}

static VfxPluginInfo INFO = {
    VFX_PLUGIN_ABI, "Cyberpunk 2077", "veffects",
    "Night City: a dense vertical neon megacity with holographic billboards, flying AV traffic, rain and holo-glitch tears in hot yellow, cyan and magenta."
};
VFX_EXPORT const VfxPluginInfo* vfx_plugin_info(void){ return &INFO; }

VFX_EXPORT void* vfx_plugin_create(int W, int H){
    State* s = new State();
    s->W=W; s->H=H; s->horizon=(int)(H*0.90f);   // low horizon -> tall vertical city
    s->topArr.assign(W, s->horizon+8);

    // --- dense VERTICAL skyline: narrow, tall, overlapping towers ---
    int x=-16;
    uint32_t bseed=0;
    while(x < W+16){
        Tower b;
        int bw = 14 + (int)(hashf(bseed,1)*44);   // narrower -> denser
        b.x0=x; b.x1=x+bw;
        float th=hashf(bseed,2);
        float tallness = th*th*0.6f + 0.4f*th;    // bias tall
        int minTop = (int)(H*0.05f);              // reach near the top
        int maxTop = s->horizon - 10;
        b.top = maxTop - (int)((maxTop-minTop)*tallness);
        b.seed = hashu(bseed*2654435761U+7u);
        b.antenna = (tallness>0.6f) ? 1 : 0;
        b.band = (int)(hashf(bseed,3)*64);
        b.colr = (int)(hashf(bseed,4)*3.0f) % 3;
        s->tow.push_back(b);
        for(int xi=b.x0; xi<=b.x1 && xi<W; xi++){
            if(xi<0) continue;
            if(b.top < s->topArr[xi]) s->topArr[xi]=b.top;
        }
        x += (int)(bw*0.68f);   // heavy overlap -> dense silhouette
        bseed++;
    }

    // --- holographic billboards; one large central hero ad + many panels ---
    const int NB=14;
    for(int i=0;i<NB;i++){
        Board b;
        b.central = (i==0) ? 1 : 0;
        if(b.central){
            b.bw = W*0.16f; b.bh = H*0.20f;
            b.bx = W*0.5f;  b.by = H*0.34f;
            b.colr = 0;     // hero yellow (shifts with centroid at render)
        }else{
            b.bw = 12 + hashf(i,11)*30;
            b.bh = 16 + hashf(i,12)*40;   // taller panels -> vertical ads
            b.bx = 20 + hashf(i,13)*(W-40);
            b.by = H*0.08f + hashf(i,14)*(s->horizon - H*0.08f - 30);
            b.colr = (int)(hashf(i,17)*3.0f) % 3;
        }
        b.phase = hashf(i,16)*6.283f;
        b.band  = i;
        b.seed = hashu((uint32_t)i*40503u+3u);
        s->boards.push_back(b);
    }

    s->rowtmp.resize((size_t)W*3);
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
    int horizon=s->horizon;
    int NBands=p->bandCount;

    // centroid drives yellow<->magenta hero balance
    float cen = clampf(p->centroid, 0.f, 1.f);
    float magMix = clampf(0.15f + cen*0.75f, 0.f, 1.f);

    auto tint = [&](int colr, float& r, float& g, float& b){
        if(colr==1){ r=0.10f; g=0.95f; b=1.0f; }        // cyan
        else if(colr==2){ r=1.0f; g=0.12f; b=0.68f; }   // magenta/hot-pink
        else { r=1.0f; g=0.82f; b=0.06f; }              // hot cyberpunk yellow
    };

    // ---------------------------------------------------------------
    // 1) SKY / SMOG -- deep blue-black up top, neon city glow at the base.
    // ---------------------------------------------------------------
    float cityGlow = 0.5f + 1.0f*bass + 0.4f*rms;
    for(int y=0;y<horizon+8;y+=2){
        float v=(float)y/horizon;
        float hz=smooth01((v-0.5f)/0.5f);
        // deep blue-black base
        float r=0.008f + 0.010f*v;
        float g=0.014f + 0.020f*v;
        float b=0.040f + 0.060f*v;
        // neon horizon glow: cool magenta/cyan wash (NOT amber) -- Night City,
        // not Blade Runner. Centroid tilts it magenta<->cyan.
        float glr=0.9f-0.5f*cen, glg=0.10f+0.5f*cen, glb=0.85f;   // magenta->cyan
        float glowK=hz*hz*0.7f*cityGlow;
        r+=glowK*glr*0.14f; g+=glowK*glg*0.10f; b+=glowK*glb*0.16f;
        float base=0.12f;
        float rr=r*base, gg=g*base, bb=b*base;
        for(int x=0;x<W;x+=2){
            if(y < s->topArr[x]){
                float band=0.85f+0.15f*sinf(x*0.02f+(float)t*0.2f);
                putAdd(fb,W,H,x,  y,  rr,gg,bb, band*A);
                putAdd(fb,W,H,x+1,y,  rr,gg,bb, band*A);
                putAdd(fb,W,H,x,  y+1,rr,gg,bb, band*A*0.9f);
                putAdd(fb,W,H,x+1,y+1,rr,gg,bb, band*A*0.9f);
            }
        }
    }
    // low neon haze hugging the base street -- alternating magenta/cyan neon
    for(int i=0;i<14;i++){
        float gx=(i+0.5f)/14.f*W;
        float pulse=0.7f+0.3f*sinf((float)t*0.5f+i);
        float hr,hg,hb;
        if(i&1){ hr=0.1f; hg=0.85f; hb=1.0f; }   // cyan
        else   { hr=1.0f; hg=0.12f; hb=0.72f; }  // magenta
        cv->add_glow(cv, gx, horizon-1, hr,hg,hb,
                     (0.06f+0.10f*cityGlow)*pulse*A, 80.f);
    }

    // ---------------------------------------------------------------
    // 2) TOWER silhouettes: neon rim + dense lit-window equalizer.
    //    Vertical density is the signature -- lots of small windows.
    // ---------------------------------------------------------------
    for(size_t bi=0; bi<s->tow.size(); bi++){
        Tower& b=s->tow[bi];
        int x0=b.x0<0?0:b.x0, x1=b.x1>=W?W-1:b.x1;
        if(x0>=x1) continue;
        float tr,tg,tb; tint(b.colr,tr,tg,tb);
        // neon rim light along the top edge
        for(int x=x0;x<=x1;x++){
            putAdd(fb,W,H,x,b.top,   tr,tg,tb, (0.16f+0.24f*bass)*A);
            putAdd(fb,W,H,x,b.top+1, tr*0.6f,tg*0.6f,tb*0.6f, (0.08f+0.12f*bass)*A);
        }
        // vertical neon edge strips (Night City accent lighting)
        float edgeK=(0.05f+0.14f*mid)*A;
        for(int y=b.top; y<horizon; y+=2){
            putAdd(fb,W,H,x0,y, tr,tg,tb, edgeK*0.5f);
            putAdd(fb,W,H,x1,y, tr,tg,tb, edgeK*0.5f);
        }
        // antenna spire + blinking beacon
        if(b.antenna){
            int mx=(x0+x1)/2;
            lineAdd(fb,W,H, mx,b.top, mx,b.top-16.f, 0.2f,0.3f,0.4f, 0.35f*A);
            float blink=0.5f+0.5f*sinf((float)t*3.0f+bi);
            cv->add_glow(cv, mx,b.top-16.f, 1.0f,0.15f,0.15f, (0.2f+0.6f*blink)*A, 4.f);
        }
        // lit windows: dense grid, per-tower band drives the fill height
        int gw=5, gh=6;
        int cols=(x1-x0)/gw;
        int rows=(horizon-b.top-2)/gh;
        if(cols<1||rows<1) continue;
        float bandE=(NBands>0)? p->bands[b.band % NBands] : mid;
        float litFrac=clampf(0.14f+0.80f*bandE*bandE,0.f,1.f);
        int litRows=(int)(litFrac*rows);
        for(int cxi=0; cxi<cols; cxi++){
            for(int ryi=0; ryi<rows; ryi++){
                uint32_t hs=hashu(b.seed ^ (uint32_t)(cxi*131u) ^ (uint32_t)(ryi*977u));
                // lit rows counted from the bottom up (base brighter)
                int fromBottom=rows-1-ryi;
                float onBias=(fromBottom < litRows)?0.50f:0.08f;
                float tw=hashf(hs,(uint32_t)(t*0.5)+1u);
                if(tw>onBias) continue;
                int wx=x0+cxi*gw+1;
                int wy=b.top+2+ryi*gh;
                if(wy>=horizon-1) continue;
                // window tint: mostly tower color, occasional white-hot
                float warm=hashf(hs,4);
                float r,g,bl;
                if(warm>0.85f){ r=1.0f; g=1.0f; bl=0.9f; }   // white-hot
                else{ r=tr; g=tg; bl=tb; }
                float wk=(0.24f+0.70f*bandE+0.30f*beat)*A;
                putAdd(fb,W,H,wx,  wy,  r,g,bl, wk);
                putAdd(fb,W,H,wx+1,wy,  r,g,bl, wk*0.85f);
                putAdd(fb,W,H,wx,  wy+1,r,g,bl, wk*0.85f);
            }
        }
    }

    // ---------------------------------------------------------------
    // 3) HOLOGRAPHIC BILLBOARDS -- blocky animated glyph panels.
    //    Central hero ad flickers with rms; panels pulse mid + per-band.
    // ---------------------------------------------------------------
    for(size_t i=0;i<s->boards.size();i++){
        Board& b=s->boards[i];
        float bandE=(NBands>0)? p->bands[b.band % NBands] : mid;
        float sway=3.f*sinf((float)t*0.25f+b.phase*1.7f);
        float driftY=3.f*sinf((float)t*0.33f+b.phase);
        float cx=b.bx+sway, cy=b.by+driftY;
        int hw=(int)b.bw, hh=(int)b.bh;

        float r,g,bl;
        float gain;
        if(b.central){
            // hero ad: yellow<->magenta by centroid, flickers hard with rms
            heroColor(magMix,r,g,bl);
            float flick=0.55f+0.45f*sinf((float)t*17.f)+0.6f*rms;
            // holo dropout flicker (analytic, occasional)
            float drop=hashf((uint32_t)(t*9.0), 313u);
            if(drop<0.10f) flick*=0.35f;
            gain=1.4f*clampf(flick,0.2f,2.4f);
        }else{
            tint(b.colr,r,g,bl);
            float buzz=0.9f+0.1f*sinf((float)t*11.f+b.phase*5.f);
            float pulse=(0.5f+0.7f*mid+0.5f*beat+0.9f*bandE)*buzz;
            gain=clampf(pulse,0.15f,2.6f);
        }

        // scan-line body: holograms read as horizontal scanline bands
        for(int yy=-hh; yy<=hh; yy+=2){
            float fy=1.f-fabsf((float)yy/hh);
            float rowk=0.12f*gain*A*fy*fy;
            for(int xx=-hw; xx<=hw; xx++){
                float fx=1.f-fabsf((float)xx/hw);
                putAdd(fb,W,H,(int)cx+xx,(int)cy+yy, r,g,bl, rowk*fx);
            }
        }
        // bright holo frame
        rectFrame(fb,W,H,(int)cx-hw,(int)cy-hh,(int)cx+hw,(int)cy+hh, r,g,bl, 0.5f*gain*A);
        // blocky animated glyph content
        glyphPanel(fb,W,H,(int)cx,(int)cy,hw,hh, b.seed, t, bandE,
                   clampf(r+0.1f,0,1), clampf(g+0.15f,0,1), clampf(bl+0.15f,0,1),
                   0.7f*gain*A);
        // bloom halo
        cv->add_glow(cv, cx,cy, r,g,bl, (0.07f+0.10f*gain)*A, (hw+hh)*0.4f+8.f);
        // wet vertical reflection smear below
        lineAdd(fb,W,H, cx,cy+hh, cx,cy+hh+28.f, r,g,bl, 0.05f*gain*A);
    }

    // ---------------------------------------------------------------
    // 4) FLYING AV CARS -- crossing traffic with light trails.
    //    Density/speed rise with bass.
    // ---------------------------------------------------------------
    int NCAR = 6 + (int)(6.f*clampf(bass*1.3f,0.f,1.f));   // more cars on bass
    float speedBoost = 1.f + 0.9f*bass;
    for(int i=0;i<NCAR;i++){
        float dir = (i%2? -1.f:1.f);
        float speed = dir*(50.f + hashf(i,31)*80.f)*speedBoost;
        float yy = H*0.10f + hashf(i,32)*(horizon-H*0.10f-40);
        float slope = (hashf(i,33)-0.5f)*0.4f;
        float span = W+140.f;
        float ph = hashf(i,34);
        float xx = fmodf((float)(ph*span + t*speed), span);
        if(xx<0) xx+=span; xx-=70.f;
        float len = 28.f + hashf(i,35)*36.f;
        float hx0=xx, hy0=yy;
        float hx1=xx-dir*len, hy1=yy-slope*len;
        // trail color alternates cyan / yellow / magenta
        int cc=i%3; float tr,tg,tb; tint(cc,tr,tg,tb);
        lineAdd(fb,W,H, hx0,hy0, hx1,hy1, tr*0.7f,tg*0.7f,tb*0.7f, 0.30f*A);
        // white-hot head + colored glow
        cv->add_glow(cv, hx0,hy0, 1.0f,0.95f,0.85f, 0.5f*A, 3.5f);
        cv->add_glow(cv, hx0,hy0, tr,tg,tb, 0.30f*A, 6.0f);
    }

    // ---------------------------------------------------------------
    // 5) RAIN -- heavy diagonal streaks; density with treble.
    // ---------------------------------------------------------------
    float rainAmt = 0.4f + 0.9f*tre + 0.3f*rms;
    int NRAIN = 240 + (int)(240.f*clampf(tre*1.4f,0.f,1.f));
    float slopeX = 0.24f;
    for(int i=0;i<NRAIN;i++){
        float col=hashf(i,51);
        float sp =280.f+hashf(i,52)*280.f;
        float ph =hashf(i,53);
        float travel=H+60.f;
        float yy=fmodf((float)(ph*travel + t*sp), travel)-30.f;
        float xx=col*(W+60.f)-30.f + yy*slopeX;
        xx=fmodf(xx,(float)(W+60.f)); if(xx<0)xx+=W+60.f; xx-=30.f;
        float len=9.f+hashf(i,54)*12.f;
        float k=(0.05f+0.14f*hashf(i,55))*rainAmt*A;
        // cool blue-white streaks catching neon
        lineAdd(fb,W,H, xx,yy, xx-slopeX*len, yy-len, 0.5f,0.7f,0.95f, k);
    }
    // brighter foreground rain
    int NFG=44;
    for(int i=0;i<NFG;i++){
        float col=hashf(i,61);
        float sp=380.f+hashf(i,62)*220.f;
        float ph=hashf(i,63);
        float travel=H+80.f;
        float yy=fmodf((float)(ph*travel + t*sp), travel)-40.f;
        float xx=col*(W+80.f)-40.f + yy*slopeX;
        xx=fmodf(xx,(float)(W+80.f)); if(xx<0)xx+=W+80.f; xx-=40.f;
        float len=16.f+hashf(i,64)*18.f;
        float k=(0.12f+0.14f*hashf(i,65))*rainAmt*A;
        lineAdd(fb,W,H, xx,yy, xx-slopeX*len, yy-len, 0.65f,0.8f,1.0f, k);
    }

    // ---------------------------------------------------------------
    // 6) WET STREET reflection glow at the very bottom.
    // ---------------------------------------------------------------
    for(int x=0;x<W;x+=2){
        float g=0.4f+0.6f*sinf(x*0.03f+(float)t*0.4f);
        // magenta/cyan wet-street reflection, no amber
        float mix=0.5f+0.5f*sinf(x*0.05f+(float)t*0.6f);
        float hr=1.0f-0.9f*mix, hg=0.12f+0.6f*mix, hb=0.75f+0.2f*mix;
        putAdd(fb,W,H,x,H-1, hr,hg,hb, (0.05f+0.10f*cityGlow)*g*A);
        putAdd(fb,W,H,x,H-2, hr,hg,hb, (0.03f+0.06f*cityGlow)*g*A);
        putAdd(fb,W,H,x,H-3, 0.1f,0.6f,0.9f, (0.02f+0.04f*cityGlow)*g*A);
    }

    // ---------------------------------------------------------------
    // 7) HOLO-GLITCH TEARS -- fire on beat/onset. Horizontal slice shift
    //    with a magenta/cyan chroma split. Destructive: full-scene only.
    // ---------------------------------------------------------------
    float glitch = clampf(beat*1.1f + onset*0.7f, 0.f, 1.f);
    if(A > 0.98f && glitch > 0.16f){
        int slices=1+(int)(glitch*5);
        for(int q=0;q<slices;q++){
            uint32_t hs=hashu((uint32_t)(p->frameNo*131 + q*977));
            int y0=(int)(hashf(hs,1)*H);
            int hgt=4+(int)(hashf(hs,2)*26);
            int dx=(int)((hashf(hs,3)-0.5f)*80.f*glitch);
            for(int y=y0; y<y0+hgt && y<H; y++){
                float* row=&fb[(size_t)y*W*3];
                memcpy(s->rowtmp.data(), row, (size_t)W*3*sizeof(float));
                for(int x=0;x<W;x++){
                    int sx=x-dx; if(sx<0)sx+=W; if(sx>=W)sx-=W;
                    // chroma split: R from shifted, B from opposite shift
                    int sxb=x+dx; if(sxb<0)sxb+=W; if(sxb>=W)sxb-=W;
                    row[x*3+0]=s->rowtmp[sx*3+0];
                    row[x*3+1]=s->rowtmp[sx*3+1];
                    row[x*3+2]=s->rowtmp[sxb*3+2];
                }
            }
            // bright tear seam
            float sr,sg,sb; tint((q&1)?2:1, sr,sg,sb);  // magenta / cyan
            for(int x=0;x<W;x++) putAdd(fb,W,H,x,y0, sr,sg,sb, 0.25f*glitch*A);
        }
    }
}
