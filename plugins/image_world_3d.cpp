// image_world_3d.cpp -- image-driven scene (3D isometric, "Sims"-like).
// Reads a loaded image as a heightmap: pixel brightness -> block height, pixel
// color -> block color. Renders an isometric world of glowing voxel columns with
// a slowly orbiting camera. Music raises the terrain and sends ripples across it.
// With no image loaded it shows an animated placeholder grid.

#include "veffects_plugin.h"
#include <cmath>
#include <cstdint>
#include <vector>
#include <algorithm>

static inline float clampf(float x, float a, float b){ return x<a?a:(x>b?b:x); }

struct Tile { float rx, rz, depth; float r, g, b, h; };
struct State {
    int W, H, GX = 64, GY = 48;
    std::vector<int> order;
    std::vector<Tile> tiles;
    State(){ order.resize(GX*GY); tiles.resize(GX*GY); }
};

static inline void putAdd(float* fb,int W,int H,int x,int y,float r,float g,float b,float k){
    if((unsigned)x>=(unsigned)W||(unsigned)y>=(unsigned)H) return;
    size_t i=((size_t)y*W+x)*3; fb[i]+=r*k; fb[i+1]+=g*k; fb[i+2]+=b*k;
}

static VfxPluginInfo INFO = {
    VFX_PLUGIN_ABI, "Image World 3D", "veffects",
    "Builds an isometric voxel world from a loaded image (brightness=height, color=tint) with an orbiting camera."
};
VFX_EXPORT const VfxPluginInfo* vfx_plugin_info(void){ return &INFO; }
VFX_EXPORT void* vfx_plugin_create(int W, int H){ State* s=new State(); s->W=W; s->H=H; return s; }
VFX_EXPORT void  vfx_plugin_destroy(void* st){ delete (State*)st; }

VFX_EXPORT void vfx_plugin_render(void* st, const VfxCanvas* cv, const VfxParams* p){
    State* s=(State*)st;
    float* fb=cv->fb; int W=cv->width, H=cv->height;
    float A=p->alpha, bass=p->bass, rms=p->rms, beat=p->beat, mid=p->mid;
    double t=p->time;
    int GX=s->GX, GY=s->GY;

    // faint sky/ground background
    for(int y=0;y<H;y+=2){
        float gy=(float)y/H;
        float rr = 0.02f+0.05f*(1-gy), gg=0.03f+0.04f*(1-gy), bb=0.07f+0.10f*(1-gy);
        for(int x=0;x<W;x+=2) putAdd(fb,W,H,x,y, rr,gg,bb, 0.5f*A);
    }

    bool haveImg = p->image && p->imageW>0 && p->imageH>0;
    const unsigned char* img = haveImg ? p->image : nullptr;
    int iw=p->imageW, ih=p->imageH, ic=p->imageChannels;

    float yaw = (float)t*0.15f;
    float cyaw=cosf(yaw), syaw=sinf(yaw);
    float tw=5.0f, th=2.6f;                    // iso positioning scale (px)
    float drawR=3.2f, drawTh=2.0f;             // tile draw half-size (< spacing => less overlap)
    float baseY = H*0.46f;
    float heightPx = 40.f * (1.f + 0.6f*bass); // vertical exaggeration, pumps with bass

    // build tiles
    for(int j=0;j<GY;j++){
        for(int i=0;i<GX;i++){
            int idx=j*GX+i;
            Tile& T=s->tiles[idx];
            float u=(i+0.5f)/GX, v=(j+0.5f)/GY;
            float r,g,b,h;
            if(haveImg){
                int ix=(int)(u*iw); if(ix>=iw)ix=iw-1;
                int iy=(int)(v*ih); if(iy>=ih)iy=ih-1;
                const unsigned char* px=&img[((size_t)iy*iw+ix)*ic];
                r=px[0]/255.f; g=px[1]/255.f; b=px[2]/255.f;
                h=0.299f*r+0.587f*g+0.114f*b;
            } else {
                // placeholder: rolling sine terrain, teal palette
                h=0.5f+0.5f*sinf(u*10+ (float)t)*cosf(v*10-(float)t*0.6f);
                r=0.1f+0.3f*h; g=0.5f+0.5f*h; b=0.6f+0.3f*h;
            }
            float cxw=(i-GX*0.5f), czw=(j-GY*0.5f);
            float rx=cxw*cyaw - czw*syaw;
            float rz=cxw*syaw + czw*cyaw;
            // ripple wave across the map on beats
            float wave = sinf((cxw+czw)*0.35f - (float)t*2.2f)*0.18f*beat;
            T.rx=rx; T.rz=rz; T.depth=rx+rz;
            T.r=r; T.g=g; T.b=b; T.h=clampf(h+wave,0.f,1.4f);
            s->order[idx]=idx;
        }
    }
    // far -> near
    std::sort(s->order.begin(), s->order.end(),
              [&](int a,int b){ return s->tiles[a].depth < s->tiles[b].depth; });

    for(int oi=0; oi<(int)s->order.size(); oi++){
        Tile& T=s->tiles[s->order[oi]];
        float sideH = T.h*heightPx;
        float sx = W*0.5f + (T.rx - T.rz)*tw;
        float sy = baseY + (T.rx + T.rz)*th - sideH;
        if(sx< -10||sx>W+10||sy<-10||sy>H+80) continue;
        float lit = 0.22f + 0.5f*T.h;                  // brighter = higher
        float glow = (0.45f + 0.4f*rms)*A;
        // side (extruded column), darker
        float sr=T.r*0.45f, sg=T.g*0.45f, sb=T.b*0.55f;
        for(int dx=-(int)drawR; dx<=(int)drawR; dx++){
            float edge = drawTh*(1.f - fabsf((float)dx)/drawR);
            int y0=(int)(sy+edge);
            for(int yy=0; yy<(int)sideH; yy++)
                putAdd(fb,W,H, (int)sx+dx, y0+yy, sr,sg,sb, 0.22f*glow);
        }
        // top diamond, lit
        for(int dy=-(int)drawTh; dy<=(int)drawTh; dy++){
            float span=drawR*(1.f - fabsf((float)dy)/drawTh);
            for(int dx=-(int)span; dx<=(int)span; dx++)
                putAdd(fb,W,H, (int)sx+dx, (int)sy+dy, T.r,T.g,T.b, lit*glow);
        }
        // rim highlight on tall tiles hit by mid
        if(T.h>0.75f)
            putAdd(fb,W,H,(int)sx,(int)(sy-drawTh), 1.f,1.f,1.f, 0.3f*mid*A);
    }
}
