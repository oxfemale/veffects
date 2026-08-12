// data_network.cpp -- "Data Network" scene plugin.
// A glowing graph of routers/hosts linked by fibers, with bright data packets
// streaming node-to-node, arrival flashes, expanding ping rings, scrolling
// hex/binary readouts and a bottom bandwidth equalizer, all in terminal
// green/cyan-blue that shifts with the timbre.
//
// All animation is derived from the absolute time p->time (analytic), so any
// frame renders correctly without simulation history: offline render and
// seeking both reproduce the exact same picture.

#include "veffects_plugin.h"
#include <cmath>
#include <cstdint>
#include <vector>

static inline float clampf(float x, float a, float b){ return x<a?a:(x>b?b:x); }
static inline float mixf(float a, float b, float t){ return a+(b-a)*t; }
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
    if(len>2000.f) return;
    int n=(int)len+1; if(n<1)n=1;
    for(int i=0;i<=n;i++){ float t=(float)i/n; putAdd(fb,W,H,(int)(x0+dx*t),(int)(y0+dy*t),r,g,b,k); }
}

struct Node { float x, y; float phase; int kind; };   // kind: 0 host, 1 switch, 2 router
struct Link { int a, b; int band; float len; float speed; float phase; };

struct State {
    int W, H;
    std::vector<Node> nodes;
    std::vector<Link> links;
};

// A tiny 3x5 hex-glyph renderer (bit rows, MSB left) for readouts.
static const uint16_t HEXFONT[16] = {
    0x7B6F, // 0
    0x2492, // 1
    0x73E7, // 2
    0x73CF, // 3
    0x5BC9, // 4
    0x79CF, // 5
    0x79EF, // 6
    0x7249, // 7
    0x7BEF, // 8
    0x7BCF, // 9
    0x7BED, // A
    0x5BAF, // B  (approx)
    0x7927, // C
    0x6B6E, // D  (approx)
    0x79E7, // E
    0x79E4, // F
};
static void drawHexDigit(float* fb, int W, int H, int px, int py, int d,
                         float r, float g, float b, float k){
    uint16_t bits = HEXFONT[d & 15];
    for(int row=0; row<5; row++){
        for(int col=0; col<3; col++){
            int bit = (bits >> (14 - (row*3+col))) & 1;
            if(bit) putAdd(fb, W, H, px+col, py+row, r, g, b, k);
        }
    }
}

static VfxPluginInfo INFO = {
    VFX_PLUGIN_ABI, "Data Network", "veffects",
    "A glowing router graph streaming beat-driven data packets between nodes with ping rings, hex readouts and a bandwidth equalizer."
};
VFX_EXPORT const VfxPluginInfo* vfx_plugin_info(void){ return &INFO; }

VFX_EXPORT void* vfx_plugin_create(int W, int H){
    State* s = new State();
    s->W = W; s->H = H;

    // --- place ~24 nodes with hashed jitter inside the working area ---
    const int N = 24;
    float x0 = 46.f, x1 = W - 46.f;
    float y0 = 52.f, y1 = H - 78.f;          // leave room for equalizer at bottom
    // loose 6x4 grid + per-cell jitter so the graph looks organic but spread out
    int cols = 6, rows = 4;
    for(int i=0;i<N;i++){
        int cxg = i % cols, cyg = i / cols;
        float gx = mixf(x0, x1, (cxg + 0.5f)/cols);
        float gy = mixf(y0, y1, (cyg + 0.5f)/rows);
        float jx = (hashf(i,101)-0.5f) * ((x1-x0)/cols) * 0.72f;
        float jy = (hashf(i,102)-0.5f) * ((y1-y0)/rows) * 0.72f;
        Node nd;
        nd.x = clampf(gx + jx, x0, x1);
        nd.y = clampf(gy + jy, y0, y1);
        nd.phase = hashf(i,103);
        nd.kind  = (int)(hashf(i,104)*3.f) % 3;
        s->nodes.push_back(nd);
    }

    // --- connect each node to its k nearest neighbours (dedup) -> ~40-70 links ---
    auto exists = [&](int a, int b){
        for(auto& l : s->links) if((l.a==a&&l.b==b)||(l.a==b&&l.b==a)) return true;
        return false;
    };
    int bandCounter = 0;
    for(int i=0;i<N;i++){
        // find nearest neighbours
        int order[N]; float dist[N];
        for(int j=0;j<N;j++){
            order[j]=j;
            float dx=s->nodes[j].x-s->nodes[i].x, dy=s->nodes[j].y-s->nodes[i].y;
            dist[j]= (j==i)? 1e18f : dx*dx+dy*dy;
        }
        // partial selection sort for the closest few
        int k = 2 + (int)(hashf(i,201)*2.99f);   // 2..4 neighbours
        for(int a=0;a<k;a++){
            int m=a; for(int b2=a+1;b2<N;b2++) if(dist[order[b2]]<dist[order[m]]) m=b2;
            int tmp=order[a]; order[a]=order[m]; order[m]=tmp;
            int j = order[a];
            if(j==i) continue;
            if(exists(i,j)) continue;
            Link l;
            l.a=i; l.b=j;
            l.len = sqrtf(dist[j]);
            l.band = bandCounter++;
            l.speed = 0.10f + hashf((uint32_t)(i*97+j), 301)*0.22f;   // fraction/sec base
            l.phase = hashf((uint32_t)(i*131+j), 302);
            s->links.push_back(l);
        }
    }
    return s;
}
VFX_EXPORT void vfx_plugin_destroy(void* st){ delete (State*)st; }

VFX_EXPORT void vfx_plugin_render(void* st, const VfxCanvas* cv, const VfxParams* p){
    State* s = (State*)st;
    float* fb = cv->fb; int W = cv->width, H = cv->height;
    float A = p->alpha;
    float bass=p->bass, mid=p->mid, tre=p->treble, rms=p->rms;
    float beat=p->beat, onset=p->onset, flux=p->flux, centroid=p->centroid;
    double t = p->time;
    int NB = p->bandCount;

    // --- palette: terminal green -> cyan/blue shifted by spectral centroid ---
    float hue = 0.34f + clampf(centroid,0.f,1.f)*0.24f;   // ~0.34 green .. 0.58 blue
    float lr,lg,lb, ar,ag,ab, wr,wg,wb;
    cv->hsv(hue, 0.85f, 1.0f, &lr,&lg,&lb);               // link/base color
    cv->hsv(hue-0.05f, 0.55f, 1.0f, &ar,&ag,&ab);         // packet/arrival (brighter, whiter)
    cv->hsv(hue+0.10f, 0.90f, 1.0f, &wr,&wg,&wb);         // accent / ping (bluer)

    // tempo phase for beat-synced packet waves
    double bpm = (p->bpm > 20.0 && p->bpm < 300.0) ? p->bpm : 120.0;
    double beatPhase = t * bpm / 60.0;                    // 1 unit per beat

    // --- background: faint vignette-ish dark field ---
    for(int y=0;y<H;y+=2){
        float gy = (float)y/H;
        float m = 0.006f + 0.012f*(0.5f+0.5f*rms)*(0.3f+gy);
        for(int x=0;x<W;x+=2)
            putAdd(fb,W,H,x,y, lr*0.10f, lg*0.14f, lb*0.18f, m*A);
    }

    // per-node accumulated activation (packet arrivals) computed while drawing packets
    std::vector<float> act(s->nodes.size(), 0.f);

    // --- links + packets ---
    int NLINK = (int)s->links.size();
    for(int li=0; li<NLINK; li++){
        Link& l = s->links[li];
        Node& A0 = s->nodes[l.a];
        Node& B0 = s->nodes[l.b];

        // per-link throughput: bass overall + this link's spectrum band
        float bandE = (NB>0) ? p->bands[l.band % NB] : 0.4f;
        float thru = 0.18f + 0.65f*bass + 0.55f*bandE;
        thru = clampf(thru, 0.f, 1.6f);

        // draw the fiber link (dim base + throughput brightness)
        float lk = (0.05f + 0.18f*thru) * A;
        lineAdd(fb,W,H, A0.x,A0.y, B0.x,B0.y, lr*0.7f, lg, lb, lk);

        // number of packets travelling on this link
        int nPk = 2 + (int)(thru*3.0f);            // 2..~6 per link
        if(nPk > 7) nPk = 7;
        for(int q=0; q<nPk; q++){
            float ph  = hashf((uint32_t)(li*13+q), 401);
            float spd = l.speed * (0.7f + 1.1f*hashf((uint32_t)(li*7+q),402)) * (1.f + 0.5f*bass);
            int   dir = (hashf((uint32_t)(li*5+q),403) > 0.5f) ? 1 : -1;

            // half the packets ride the tempo (beat waves), half free-run
            float frac;
            bool waveRider = (q & 1) == 0;
            if(waveRider)
                frac = fracf((float)beatPhase * (0.35f + spd) * dir + ph);
            else
                frac = fracf((float)(t*spd) * dir + ph);

            float px = mixf(A0.x, B0.x, frac);
            float py = mixf(A0.y, B0.y, frac);

            float pk = (0.35f + 0.65f*thru) * (0.7f + 0.9f*beat);
            // launch flash: waveRiders brighten right after a beat (onset burst)
            if(waveRider) pk *= (1.f + 1.6f*onset);
            float rad = 2.2f + 1.8f*beat + 2.0f*thru;
            cv->add_glow(cv, px,py, ar,ag,ab, pk*0.9f*A, rad);
            // bright core dash
            putAdd(fb,W,H,(int)px,(int)py, ar,ag,ab, pk*0.9f*A);

            // arrival contribution: near an endpoint, light that node up
            float near0 = clampf(1.f - frac*8.f, 0.f, 1.f);          // close to A
            float near1 = clampf(1.f - (1.f-frac)*8.f, 0.f, 1.f);    // close to B
            act[l.a] += near0 * pk;
            act[l.b] += near1 * pk;
        }
    }

    // --- treble sparkle: transient packets flickering on random links ---
    if(tre > 0.05f){
        int sparks = (int)(tre * 40.f);
        for(int i=0;i<sparks;i++){
            uint32_t hs = hashu((uint32_t)(p->frameNo*2654435761u + i*40503u));
            int li = hs % (NLINK>0?NLINK:1);
            Link& l = s->links[li];
            Node& A0 = s->nodes[l.a]; Node& B0 = s->nodes[l.b];
            float f = hashf(hs, 7);
            float px = mixf(A0.x,B0.x,f), py = mixf(A0.y,B0.y,f);
            cv->add_glow(cv, px,py, wr,wg,wb, (0.5f+0.8f*tre)*A, 2.0f);
        }
    }

    // --- nodes: glow scaled by activation, plus ping rings when busy ---
    for(size_t i=0;i<s->nodes.size();i++){
        Node& nd = s->nodes[i];
        float a = clampf(act[i]*0.5f, 0.f, 2.0f);
        float baseB = 0.55f + 0.35f*rms;
        float glowB = baseB + 1.6f*a + 0.5f*beat;
        float rad = 6.f + (nd.kind*2.f) + 3.f*a + 2.f*beat;

        // node body: bright core + colored halo
        cv->add_glow(cv, nd.x, nd.y, wr,wg,wb, 0.35f*glowB*A, rad*1.4f);
        cv->add_glow(cv, nd.x, nd.y, ar,ag,ab, 0.75f*glowB*A, rad*0.55f);
        putAdd(fb,W,H,(int)nd.x,(int)nd.y, 1.f,1.f,1.f, (0.4f+a)*A);

        // router kind gets a small square outline
        if(nd.kind==2){
            float rr=5.f;
            lineAdd(fb,W,H, nd.x-rr,nd.y-rr, nd.x+rr,nd.y-rr, wr,wg,wb, 0.4f*glowB*A);
            lineAdd(fb,W,H, nd.x+rr,nd.y-rr, nd.x+rr,nd.y+rr, wr,wg,wb, 0.4f*glowB*A);
            lineAdd(fb,W,H, nd.x+rr,nd.y+rr, nd.x-rr,nd.y+rr, wr,wg,wb, 0.4f*glowB*A);
            lineAdd(fb,W,H, nd.x-rr,nd.y+rr, nd.x-rr,nd.y-rr, wr,wg,wb, 0.4f*glowB*A);
        }

        // ping rings: continuously emanate from active nodes, tempo-driven
        float ringDrive = a + 0.3f*bass;
        if(ringDrive > 0.25f){
            int rings = 3;
            for(int rI=0; rI<rings; rI++){
                float rp = fracf((float)(t*0.6f) + nd.phase + rI/(float)rings);
                float R = rp * (26.f + 20.f*ringDrive);
                float fade = (1.f - rp);
                float k = 0.30f * ringDrive * fade * A;
                if(k <= 0.001f) continue;
                int seg = 26;
                for(int q=0;q<seg;q++){
                    float an0 = (q/(float)seg)*6.2831853f;
                    float an1 = ((q+1)/(float)seg)*6.2831853f;
                    lineAdd(fb,W,H,
                        nd.x+cosf(an0)*R, nd.y+sinf(an0)*R,
                        nd.x+cosf(an1)*R, nd.y+sinf(an1)*R,
                        wr,wg,wb, k);
                }
            }
        }

        // hex readout beside busy nodes (scrolling digits)
        if(a > 0.15f){
            int ndig = 4;
            int hx = (int)nd.x + 9;
            int hy = (int)nd.y - 2;
            for(int d=0; d<ndig; d++){
                uint32_t hs = hashu((uint32_t)(i*777u) ^ (uint32_t)(t*6.0) ^ (uint32_t)(d*31u));
                int digit = hs & 15;
                drawHexDigit(fb,W,H, hx + d*4, hy, digit,
                             ar,ag,ab, (0.25f+0.5f*a)*A);
            }
        }
    }

    // --- top scrolling binary strip (network trace) ---
    {
        int bits = W/6;
        int scroll = (int)(t*18.0);
        for(int i=0;i<bits;i++){
            uint32_t hs = hashu((uint32_t)((i+scroll)*2246822519u) ^ (uint32_t)(t*2.0));
            int bit = hs & 1;
            int bx = i*6;
            int by = 6;
            float k = (bit? 0.5f : 0.16f) * (0.5f+0.6f*flux) * A;
            drawHexDigit(fb,W,H, bx, by, bit, lr,lg,lb, k);
        }
    }

    // --- bottom bandwidth equalizer ---
    if(NB > 0){
        int pad = 16;
        int bw = (W - 2*pad) / NB;
        int baseY = H - 10;
        int maxH = 46;
        for(int i=0;i<NB;i++){
            float e = p->bands[i];
            float bh = clampf(e*1.15f, 0.f, 1.f) * maxH;
            int bx = pad + i*bw;
            // per-bar hue leans bluer for higher bands
            float bhue = hue + 0.14f*(i/(float)NB);
            float br,bg,bb; cv->hsv(bhue, 0.85f, 1.0f, &br,&bg,&bb);
            for(int y=0;y<(int)bh;y++){
                float v = y/(bh+1.f);
                float k = (0.14f + 0.55f*v) * A;
                for(int x=0;x<bw-1;x++)
                    putAdd(fb,W,H, bx+x, baseY-y, br,bg,bb, k);
            }
            // cap highlight
            for(int x=0;x<bw-1;x++)
                putAdd(fb,W,H, bx+x, baseY-(int)bh, 1.f,1.f,1.f, 0.35f*A);
        }
        // baseline rule
        for(int x=pad;x<W-pad;x++)
            putAdd(fb,W,H, x, baseY+1, lr,lg,lb, 0.12f*A);
    }
}
