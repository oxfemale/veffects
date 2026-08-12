// photo_particles.cpp -- image-driven scene (2D).
// Turns a loaded image (Open image / drag a jpg-png-bmp) into a field of glowing
// particles: each sampled pixel becomes a dot at its image position, colored by
// the pixel. Music scatters and reforms the picture -- beats blow the particles
// apart, quiet passages let them settle back into the photo. With no image loaded
// it shows an animated placeholder.

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
static inline void addGlowFB(float* fb, int W, int H, float fx, float fy,
                             float r, float g, float b, float k, float rad){
    int R = (int)ceilf(rad); int xi=(int)fx, yi=(int)fy;
    float r2 = rad*rad;
    for(int dy=-R;dy<=R;dy++) for(int dx=-R;dx<=R;dx++){
        float q=(dx*dx+dy*dy)/r2; if(q>1) continue;
        int x=xi+dx, y=yi+dy;
        if((unsigned)x>=(unsigned)W||(unsigned)y>=(unsigned)H) continue;
        float w=(1-q)*(1-q)*k; size_t i=((size_t)y*W+x)*3;
        fb[i]+=r*w; fb[i+1]+=g*w; fb[i+2]+=b*w;
    }
}

struct State { int W, H; int GX = 150, GY = 110; };

static VfxPluginInfo INFO = {
    VFX_PLUGIN_ABI, "Photo Particles", "veffects",
    "Turns a loaded image into a field of music-reactive particles that scatter on beats and reform the picture."
};
VFX_EXPORT const VfxPluginInfo* vfx_plugin_info(void){ return &INFO; }
VFX_EXPORT void* vfx_plugin_create(int W, int H){ State* s=new State(); s->W=W; s->H=H; return s; }
VFX_EXPORT void  vfx_plugin_destroy(void* st){ delete (State*)st; }

VFX_EXPORT void vfx_plugin_render(void* st, const VfxCanvas* cv, const VfxParams* p){
    State* s=(State*)st;
    float* fb=cv->fb; int W=cv->width, H=cv->height;
    float A=p->alpha, bass=p->bass, tre=p->treble, rms=p->rms, beat=p->beat, onset=p->onset;
    double t=p->time;

    // ---- no image: animated placeholder ----
    if(!p->image || p->imageW<=0 || p->imageH<=0){
        for(int y=0;y<H;y+=2) for(int x=0;x<W;x+=2){
            float u=(float)x/W, v=(float)y/H;
            float n=0.5f+0.5f*sinf(u*8+ (float)t)*cosf(v*8-(float)t*0.7f);
            float rr,gg,bb; cv->hsv(0.55f+0.2f*n+0.1f*(float)sin(t*0.2), 0.6f, 0.5f*n, &rr,&gg,&bb);
            size_t i=((size_t)y*W+x)*3; fb[i]+=rr*0.15f*A; fb[i+1]+=gg*0.15f*A; fb[i+2]+=bb*0.15f*A;
        }
        // prompt text as a bright bar
        for(int x=W/2-120;x<W/2+120;x++){ int y=H/2;
            fb[((size_t)y*W+x)*3+0]+=0.2f*A; fb[((size_t)y*W+x)*3+1]+=0.8f*A; fb[((size_t)y*W+x)*3+2]+=0.9f*A; }
        return;
    }

    const unsigned char* img=p->image; int iw=p->imageW, ih=p->imageH, ic=p->imageChannels;
    // aspect-fit the image into the frame
    float scale=fminf((float)W/iw,(float)H/ih);
    float dW=iw*scale, dH=ih*scale, oX=(W-dW)*0.5f, oY=(H-dH)*0.5f;

    // scatter amount: quiet -> reformed photo, loud/beat -> exploded
    float scatter = 0.6f*beat + 0.25f*rms + 0.15f;
    float cx=W*0.5f, cy=H*0.5f;

    for(int gy=0; gy<s->GY; gy++){
        for(int gx=0; gx<s->GX; gx++){
            float u=(gx+0.5f)/s->GX, vv=(gy+0.5f)/s->GY;
            int ix=(int)(u*iw); if(ix>=iw)ix=iw-1;
            int iy=(int)(vv*ih); if(iy>=ih)iy=ih-1;
            const unsigned char* px=&img[((size_t)iy*iw+ix)*ic];
            float r=px[0]/255.f, g=px[1]/255.f, b=px[2]/255.f;

            float bx=oX+u*dW, by=oY+vv*dH;                 // home position (the photo)
            // per-particle scatter direction + gentle time wander
            uint32_t h=hashu(gx*73856093u ^ gy*19349663u);
            float ang=hashf(h,1)*6.2831853f;
            float dist=hashf(h,2);
            float outx=cosf(ang), outy=sinf(ang);
            float wob=sinf((float)t*(1.5f+2.f*hashf(h,3))+hashf(h,4)*6.28f);
            float disp=scatter*(30.f+120.f*dist)*(0.7f+0.6f*wob);
            // pull toward center a touch as it scatters, so it "breathes"
            float px2=bx + outx*disp + (cx-bx)*0.15f*scatter;
            float py2=by + outy*disp + (cy-by)*0.15f*scatter;

            float bright=(0.5f+0.9f*rms)*(0.8f+0.5f*tre);
            float sparkle = (hashf(h,(uint32_t)(t*6)) < tre*0.15f) ? 1.8f : 1.f;
            float rad=1.3f + 1.2f*(1.f-scatter*0.5f);
            addGlowFB(fb,W,H, px2,py2, r,g,b, bright*sparkle*0.6f*A, rad);
        }
    }
    // a soft beat flash of the whole picture edge
    (void)onset; (void)bass;
}
