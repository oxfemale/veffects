// nirvana.cpp -- "Nirvana" scene plugin (after Gabriele Salvatores' 1997 film).
// A self-aware videogame character (Solo) trapped inside a decaying digital
// world: a magenta neon perspective grid dissolving into a corrupted skyline,
// scrolling glyph fragments, datamosh corruption blocks that pulse on the beat,
// a flickering central eye/portal, VHS scanlines and beat-driven slice tears
// with RGB channel offset.
//
// All animation is derived from the absolute time p->time, so any frame renders
// correctly with no simulation history (offline render and seeking both work).

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

// Neon palette: shift magenta <-> cyan by spectral centroid `c` in [0..1].
// c low -> hot magenta/pink; c high -> electric cyan; mid -> violet.
static inline void neon(float c, float& r, float& g, float& b){
    c = clampf(c, 0.f, 1.f);
    // magenta (1.0,0.10,0.75)  ->  violet (0.55,0.20,1.0)  ->  cyan (0.10,0.95,1.0)
    if(c < 0.5f){ float t=c*2.f;
        r = 1.00f + (0.55f-1.00f)*t; g = 0.10f + (0.20f-0.10f)*t; b = 0.75f + (1.00f-0.75f)*t;
    } else { float t=(c-0.5f)*2.f;
        r = 0.55f + (0.10f-0.55f)*t; g = 0.20f + (0.95f-0.20f)*t; b = 1.00f + (1.00f-1.00f)*t;
    }
}

// procedural corrupted glyph: a few strokes in a cw x ch cell (magenta code frag)
static void drawGlyph(float* fb, int W, int H, int px, int py, int cw, int ch,
                      uint32_t seed, float r, float g, float b, float k){
    int x0 = px+1, y0 = py+1, iw = cw-2, ih = ch-2;
    int nh = 2 + (int)(hashf(seed,1)*2.99f);
    for(int s=0;s<nh;s++){
        int ry = y0 + (int)(hashf(seed, 10+s)*(ih-2));
        int xa = x0 + (int)(hashf(seed, 20+s)*iw*0.4f);
        int xb = x0 + iw - (int)(hashf(seed, 30+s)*iw*0.4f);
        for(int x=xa;x<=xb;x++) putAdd(fb,W,H,x,ry,r,g,b,k);
    }
    int nv = 1 + (int)(hashf(seed,2)*2.99f);
    for(int s=0;s<nv;s++){
        int rx = x0 + (int)(hashf(seed, 40+s)*(iw-2));
        int ya = y0 + (int)(hashf(seed, 50+s)*ih*0.3f);
        int yb = y0 + ih - (int)(hashf(seed, 60+s)*ih*0.3f);
        for(int y=ya;y<=yb;y++) putAdd(fb,W,H,rx,y,r,g,b,k);
    }
}

struct State {
    int W, H;
    int cw = 10, ch = 14;
    std::vector<float> rowtmp;
};

static VfxPluginInfo INFO = {
    VFX_PLUGIN_ABI, "Nirvana", "veffects",
    "A magenta neon perspective grid dissolving into a corrupted skyline with datamosh glitch blocks, scrolling code fragments and a flickering central eye trapped in digital decay."
};
VFX_EXPORT const VfxPluginInfo* vfx_plugin_info(void){ return &INFO; }

VFX_EXPORT void* vfx_plugin_create(int W, int H){
    State* s = new State();
    s->W = W; s->H = H;
    s->rowtmp.resize((size_t)W*3);
    return s;
}
VFX_EXPORT void vfx_plugin_destroy(void* st){ delete (State*)st; }

VFX_EXPORT void vfx_plugin_render(void* st, const VfxCanvas* cv, const VfxParams* p){
    State* s = (State*)st;
    float* fb = cv->fb; int W = cv->width, H = cv->height;
    float A = p->alpha;
    float bass=p->bass, mid=p->mid, tre=p->treble, rms=p->rms, beat=p->beat;
    float onset=p->onset, cen=p->centroid; double t=p->time;
    float glitch = clampf(beat*1.1f + onset*0.7f, 0.f, 1.f);

    // primary neon color (magenta<->cyan by centroid) and a fixed cyan accent
    float nr,ng,nb; neon(cen, nr,ng,nb);
    const float cr=0.15f, cg=0.95f, cb=1.0f;          // cyan accent
    float cx = W*0.5f;
    float horizon = H*0.54f;

    // ---- 1) background: deep violet -> black vertical wash, brighter near horizon
    for(int y=0;y<H;y+=2){
        float d = fabsf((float)y - horizon)/H;
        float m = 0.006f + 0.020f*(1.f-clampf(d*2.2f,0.f,1.f));
        float br = 0.35f + 0.20f*rms;
        for(int x=0;x<W;x+=2)
            putAdd(fb,W,H,x,y, 0.28f*br, 0.03f*br, 0.42f*br, m*A);
    }

    // ---- 2) perspective neon grid floor (decaying, scrolls toward viewer w/ bass)
    float span = (float)(H - horizon);
    float scroll = fmodf((float)t*(0.35f + 1.4f*bass), 1.f);
    int NL = 18;
    for(int i=1;i<=NL;i++){
        float dpt = (float)i - scroll;
        if(dpt < 0.35f) continue;
        float yy = horizon + span/dpt;
        if(yy > H+2) continue;
        float fade = clampf(1.f/(dpt*0.55f), 0.f, 1.f);
        // decay: some lines flicker / drop out (memory falling apart)
        float flick = hashf((uint32_t)i, (uint32_t)(t*3.0));
        float lk = (0.10f + 0.55f*fade) * (0.6f + 0.7f*flick) * (0.8f+0.6f*rms);
        for(int x=0;x<W;x+=1) putAdd(fb,W,H,x,(int)yy, nr,ng,nb, lk*A);
    }
    // vanishing verticals
    int NV = 16;
    for(int j=-NV;j<=NV;j++){
        float bx = cx + (float)j*(W*0.5f/NV)*1.7f;
        float fade = 1.f - fabsf((float)j)/(NV+1);
        float lk = (0.08f + 0.28f*fade)*(0.8f+0.5f*bass);
        lineAdd(fb,W,H, cx, horizon, bx, (float)H, nr*0.9f,ng*0.9f,nb, lk*A);
    }
    // bright horizon line
    for(int x=0;x<W;x++){
        putAdd(fb,W,H,x,(int)horizon, cr,cg,cb, (0.5f+0.6f*rms)*A);
        putAdd(fb,W,H,x,(int)horizon-1, nr,ng,nb, 0.3f*A);
    }

    // ---- 3) corrupted skyline / equalizer above horizon (driven by bands)
    int NB = p->bandCount;
    if(NB>0){
        int bw = W/NB;
        for(int i=0;i<NB;i++){
            float e = p->bands[i];
            // glitch: occasionally a band spikes/corrupts
            float corr = hashf((uint32_t)i*53u, (uint32_t)(t*4.0));
            float bh = (e*e*95.f + 6.f) * (0.7f + 0.9f*corr) * (1.f + 0.6f*glitch*(corr>0.7f));
            int bx = i*bw;
            int topY = (int)(horizon - bh);
            // building body (dim neon), edges brighter
            for(int y=topY; y<(int)horizon; y++){
                float vy = (float)(y-topY)/(bh+1.f);
                float k = (0.05f + 0.16f*(1.f-vy));
                for(int x=1;x<bw-1;x++) putAdd(fb,W,H, bx+x, y, nr,ng,nb, k*A);
            }
            // neon roof edge + cyan window sparkle from treble
            for(int x=0;x<bw;x++) putAdd(fb,W,H, bx+x, topY, cr,cg,cb, (0.35f+0.5f*e)*A);
            lineAdd(fb,W,H, bx,topY, bx,horizon, nr,ng,nb, 0.35f*A);
            if(tre>0.02f){
                int wc = 1+(int)(tre*4.f);
                for(int w=0;w<wc;w++){
                    int wx = bx + 2 + (int)(hashf((uint32_t)(i*17+w),(uint32_t)(t*9.0))*(bw-4));
                    int wy = topY + 2 + (int)(hashf((uint32_t)(i*29+w),(uint32_t)(t*11.0))*(bh-2));
                    putAdd(fb,W,H, wx, wy, cr,cg,cb, (0.4f+0.6f*tre)*A);
                }
            }
        }
    }

    // ---- 4) central eye / portal (self-aware character, flickering)
    float ecx = cx, ecy = horizon - 78.f - 10.f*rms;
    float flick = 0.55f + 0.45f*sinf((float)t*7.3f) + 0.3f*(hashf((uint32_t)(t*20.0),7)-0.5f);
    flick = clampf(flick, 0.1f, 1.2f);
    float R = 34.f + 6.f*sinf((float)t*1.3f) + 22.f*beat;
    // outer glow halo
    cv->add_glow(cv, ecx, ecy, nr,ng,nb, (0.5f+1.6f*beat)*flick*A, R*1.9f);
    // eye ring (almond outline)
    int RS=48;
    for(int i=0;i<RS;i++){
        float a0=(float)i/RS*6.2832f, a1=(float)(i+1)/RS*6.2832f;
        float rr0 = R*(0.55f+0.45f*fabsf(cosf(a0)));   // almond squash
        float rr1 = R*(0.55f+0.45f*fabsf(cosf(a1)));
        lineAdd(fb,W,H, ecx+cosf(a0)*rr0, ecy+sinf(a0)*rr0*0.62f,
                        ecx+cosf(a1)*rr1, ecy+sinf(a1)*rr1*0.62f,
                        cr,cg,cb, (0.5f*flick)*A);
    }
    // pupil scanning left/right, cyan core
    float pupx = ecx + 9.f*sinf((float)t*0.9f);
    cv->add_glow(cv, pupx, ecy, cr,cg,cb, (1.2f*flick)*A, 9.f);
    cv->add_glow(cv, pupx, ecy, nr,ng,nb, (0.9f*flick)*A, 4.f);
    // radiating cracks (digital decay)
    for(int a=0;a<7;a++){
        float an = a*0.897f + (float)t*0.2f;
        float r0 = R*0.7f, r1 = R*(1.4f + 0.8f*hashf(a,(uint32_t)(t*2.0)));
        lineAdd(fb,W,H, ecx+cosf(an)*r0, ecy+sinf(an)*r0*0.62f,
                        ecx+cosf(an)*r1, ecy+sinf(an)*r1*0.62f,
                        nr,ng,nb, 0.28f*flick*A);
    }

    // ---- 5) scrolling code fragments (magenta glyphs that dissolve)
    int gcols = W/s->cw;
    int grows = (int)(horizon/ s->ch) + 1;
    for(int c=0;c<gcols;c++){
        // only some columns active (sparse, corrupted memory)
        if(hashf((uint32_t)c,101) > 0.42f) continue;
        float spd = 26.f + hashf((uint32_t)c,102)*70.f + 90.f*bass;
        float off = fmodf((float)t*spd + hashf((uint32_t)c,103)*horizon, horizon+ s->ch);
        for(int r=0;r<grows;r++){
            int py = (int)(off) + r*s->ch - grows*s->ch;
            // wrap into [0,horizon)
            int yy = ((py % (int)(horizon+s->ch)) + (int)(horizon+s->ch)) % (int)(horizon+s->ch);
            if(yy > (int)horizon - s->ch) continue;
            float diss = hashf((uint32_t)(c*131+r), (uint32_t)(t*5.0));
            if(diss < 0.35f) continue;                 // dissolving away
            uint32_t gs = hashu((uint32_t)(c*911+r*7) ^ (uint32_t)(t*6.0));
            float k = (0.20f + 0.5f*diss)*(0.7f+0.6f*tre);
            drawGlyph(fb,W,H, c*s->cw, yy, s->cw, s->ch, gs, nr,ng,nb, k*A);
        }
    }

    // ---- 6) datamosh corruption blocks pulsing on the beat
    int nblk = (int)(2 + glitch*10.f);
    for(int q=0;q<nblk;q++){
        uint32_t bs = hashu((uint32_t)(p->frameNo*263 + q*613));
        int bw = 12 + (int)(hashf(bs,1)*70.f*(0.4f+glitch));
        int bh = 6  + (int)(hashf(bs,2)*30.f);
        int bx = (int)(hashf(bs,3)*(W-bw));
        int by = (int)(hashf(bs,4)*(H-bh));
        // channel-swapped noisy block, brighter with beat
        float useCyan = hashf(bs,5) > 0.5f ? 1.f : 0.f;
        float rr = useCyan? cr:nr, gg = useCyan? cg:ng, bb = useCyan? cb:nb;
        float bk = (0.10f + 0.5f*glitch)*(0.5f+0.8f*beat);
        for(int y=0;y<bh;y+=2)
            for(int x=0;x<bw;x++){
                float n = hashf(bs^(uint32_t)(x*97+y*131), (uint32_t)(t*30.0));
                if(n<0.45f) continue;
                putAdd(fb,W,H, bx+x, by+y, rr,gg,bb, bk*n*A);
            }
    }

    // ---- 7) VHS scanlines (subtle) + moving tracking bar
    for(int y=0;y<H;y+=3)
        for(int x=0;x<W;x+=2)
            putAdd(fb,W,H,x,y, nr*0.4f, ng*0.4f, nb*0.6f, 0.02f*A);
    float trackY = fmodf((float)t*70.f, (float)H);
    for(int x=0;x<W;x++){
        putAdd(fb,W,H,x,(int)trackY, cr,cg,cb, 0.10f*A);
        putAdd(fb,W,H,x,(int)trackY+1, nr,ng,nb, 0.06f*A);
    }

    // ---- destructive glitch: only when scene fully visible ----
    if(A > 0.98f){
        // slight scanline darkening for CRT feel
        for(int y=1;y<H;y+=3){
            float* row=&fb[(size_t)y*W*3];
            for(int x=0;x<W*3;x++) row[x]*=0.78f;
        }
        if(glitch > 0.12f){
            // horizontal slice tears with per-slice RGB channel offset
            int slices = 1 + (int)(glitch*6);
            for(int q=0;q<slices;q++){
                uint32_t hs = hashu((uint32_t)(p->frameNo*151 + q*829));
                int y0 = (int)(hashf(hs,1)*H);
                int hgt = 3 + (int)(hashf(hs,2)*26);
                int dx  = (int)((hashf(hs,3)-0.5f)*80.f*glitch);
                int cdx = 2 + (int)(hashf(hs,4)*8.f*glitch);  // chroma split
                for(int y=y0; y<y0+hgt && y<H; y++){
                    float* row=&fb[(size_t)y*W*3];
                    memcpy(s->rowtmp.data(), row, (size_t)W*3*sizeof(float));
                    for(int x=0;x<W;x++){
                        int sxR = x-dx-cdx; if(sxR<0)sxR+=W; if(sxR>=W)sxR-=W;
                        int sxG = x-dx;     if(sxG<0)sxG+=W; if(sxG>=W)sxG-=W;
                        int sxB = x-dx+cdx; if(sxB<0)sxB+=W; if(sxB>=W)sxB-=W;
                        row[x*3+0]=s->rowtmp[sxR*3+0];
                        row[x*3+1]=s->rowtmp[sxG*3+1];
                        row[x*3+2]=s->rowtmp[sxB*3+2];
                    }
                }
            }
        }
    }
}
