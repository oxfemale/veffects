// ghost_in_the_shell.cpp -- "Ghost in the Shell" scene plugin.
// Green digital rain (katakana), a rotating wireframe cyber-skull inside a HUD
// reticle, a terminal overlay, CRT scanlines and beat-driven glitch.
//
// All animation is derived from the absolute time p->time, so any frame renders
// correctly without simulation history (offline render and seeking both work).

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

struct Vec3 { float x,y,z; };
struct RainCol { float phase; float speed; int len; uint32_t seed; float flick; };
struct State {
    int W, H;
    int cw = 12, ch = 16;
    int cols = 0;
    std::vector<RainCol> rain;
    std::vector<Vec3> verts;
    std::vector<std::pair<int,int>> edges;
    std::vector<float> rowtmp;
};

// procedural "katakana"-like glyph: a few thick strokes in a cw x ch cell
static void drawGlyph(float* fb, int W, int H, int px, int py, int cw, int ch,
                      uint32_t seed, float r, float g, float b, float k){
    int x0 = px+1, y0 = py+1, iw = cw-2, ih = ch-2;
    int nh = 2 + (int)(hashf(seed,1)*2.99f);
    for(int s=0;s<nh;s++){
        int ry = y0 + (int)(hashf(seed, 10+s)*(ih-2));
        int xa = x0 + (int)(hashf(seed, 20+s)*iw*0.35f);
        int xb = x0 + iw - (int)(hashf(seed, 30+s)*iw*0.35f);
        for(int x=xa;x<=xb;x++){ putAdd(fb,W,H,x,ry,r,g,b,k); putAdd(fb,W,H,x,ry+1,r,g,b,k*0.7f); }
    }
    int nv = 1 + (int)(hashf(seed,2)*2.99f);
    for(int s=0;s<nv;s++){
        int rx = x0 + (int)(hashf(seed, 40+s)*(iw-2));
        int ya = y0 + (int)(hashf(seed, 50+s)*ih*0.3f);
        int yb = y0 + ih - (int)(hashf(seed, 60+s)*ih*0.3f);
        for(int y=ya;y<=yb;y++){ putAdd(fb,W,H,rx,y,r,g,b,k); putAdd(fb,W,H,rx+1,y,r,g,b,k*0.7f); }
    }
    if(hashf(seed,3) > 0.65f){
        int len = 2 + (int)(hashf(seed,4)*3);
        for(int i=0;i<len;i++) putAdd(fb,W,H,x0+i, y0+ih-1-i, r,g,b,k);
    }
}

static VfxPluginInfo INFO = {
    VFX_PLUGIN_ABI, "Ghost in the Shell", "veffects",
    "Green katakana digital rain, a wireframe cyber-skull in a HUD reticle, scanlines and beat glitch."
};
VFX_EXPORT const VfxPluginInfo* vfx_plugin_info(void){ return &INFO; }

VFX_EXPORT void* vfx_plugin_create(int W, int H){
    State* s = new State();
    s->W = W; s->H = H; s->cols = W / s->cw;
    s->rain.resize(s->cols);
    for(int c=0;c<s->cols;c++){
        RainCol& r = s->rain[c];
        r.speed = 45.f + hashf(c,71)*130.f;
        r.len   = 8 + (int)(hashf(c,72)*18);
        r.seed  = hashu(c*2654435761U + 12345U);
        r.phase = hashf(c,73);
        r.flick = hashf(c,74)*100.f;
    }
    // "skull": ellipsoid tapered toward the chin
    const int LAT = 11, LON = 20;
    auto vidx = [&](int i,int j){ return i*LON + (j%LON); };
    for(int i=0;i<LAT;i++){
        float t = (float)i/(LAT-1);
        float lat = (t - 0.5f) * (float)M_PI;
        float low = clampf((0.5f - t)*2.f, 0.f, 1.f);
        float top = clampf((t - 0.72f)/0.28f, 0.f, 1.f);
        float prof = 1.f - 0.45f*low*low - 0.25f*top;
        float rx = 0.72f*prof, ry = 1.0f, rz = 0.80f*prof;
        for(int j=0;j<LON;j++){
            float lon = (float)j/LON * 2.f*(float)M_PI;
            s->verts.push_back({ rx*cosf(lat)*cosf(lon), ry*sinf(lat), rz*cosf(lat)*sinf(lon) });
        }
    }
    for(int i=0;i<LAT;i++)
        for(int j=0;j<LON;j++){
            s->edges.push_back({ vidx(i,j), vidx(i,j+1) });
            if(i+1<LAT) s->edges.push_back({ vidx(i,j), vidx(i+1,j) });
        }
    s->rowtmp.resize((size_t)W*3);
    return s;
}
VFX_EXPORT void vfx_plugin_destroy(void* st){ delete (State*)st; }

VFX_EXPORT void vfx_plugin_render(void* st, const VfxCanvas* cv, const VfxParams* p){
    State* s = (State*)st;
    float* fb = cv->fb; int W = cv->width, H = cv->height;
    float A = p->alpha;
    float bass=p->bass, mid=p->mid, tre=p->treble, rms=p->rms, beat=p->beat;
    float onset=p->onset; double t=p->time;
    float glitch = clampf(beat*1.25f + onset*0.5f, 0.f, 1.f);

    // faint dark-green background gradient
    for(int y=0;y<H;y+=2){
        float gy = (float)y/H;
        float m = 0.004f + 0.010f*gy*gy*(0.5f+0.5f*rms);
        for(int x=0;x<W;x+=2) putAdd(fb,W,H,x,y, 0.0f,0.5f,0.18f, m*A);
    }

    // 1) digital rain
    for(int c=0;c<s->cols;c++){
        RainCol& r = s->rain[c];
        float tailPx = r.len * s->ch;
        float travel = H + tailPx;
        float headY  = fmodf(r.phase*travel + (float)t*r.speed, travel) - tailPx;
        int px = c * s->cw;
        int headCell = (int)floorf(headY / s->ch);
        for(int q=0;q<r.len;q++){
            int cellY = headCell - q;
            int py = cellY * s->ch;
            if(py < -s->ch || py > H) continue;
            float fade = 1.f - (float)q / r.len; fade *= fade;
            uint32_t gs = hashu(r.seed ^ (uint32_t)(cellY*2246822519U) ^ (uint32_t)((t*7.0 + r.flick)));
            float rr,gg,bb,k;
            if(q==0){ rr=0.85f; gg=1.0f; bb=0.85f; k=(1.7f + 0.9f*tre);
                      cv->add_glow(cv, px+s->cw*0.5f, py+s->ch*0.5f, 0.4f,1.0f,0.5f, 0.5f*A, 5.f); }
            else    { rr=0.10f; gg=1.0f; bb=0.30f; k=(0.40f + 0.85f*fade)*(0.85f+0.5f*tre); }
            drawGlyph(fb,W,H, px,py, s->cw,s->ch, gs, rr,gg,bb, k*A);
        }
    }

    // 2) wireframe cyber-skull
    float cx = W*0.5f, cy = H*0.46f;
    float yaw = (float)t*0.55f;
    float pitch = 0.12f*sinf((float)t*0.35f) + 0.30f*mid;
    float scale = 1.f + 0.09f*beat;
    float cyS=cosf(yaw), syS=sinf(yaw), cpS=cosf(pitch), spS=sinf(pitch);
    float f=360.f, camZ=3.2f;
    auto project = [&](Vec3 v, float& sx, float& sy, float& depth){
        v.x*=scale; v.y*=scale; v.z*=scale;
        float x1=v.x*cyS+v.z*syS, z1=-v.x*syS+v.z*cyS;
        float y1=v.y*cpS-z1*spS,  z2= v.y*spS+z1*cpS;
        float den=camZ-z2; if(den<0.2f)den=0.2f;
        sx=cx+f*x1/den; sy=cy-f*y1/den; depth=z2;
    };
    for(auto& e : s->edges){
        float ax,ay,ad,bx,by,bd;
        project(s->verts[e.first], ax,ay,ad);
        project(s->verts[e.second], bx,by,bd);
        float md=(ad+bd)*0.5f;
        float front=clampf(0.5f+md*0.9f,0.05f,1.f);
        float k=(0.05f+0.16f*front)*(0.8f+0.5f*rms);
        lineAdd(fb,W,H, ax,ay,bx,by, 0.15f*front,0.9f,0.75f, k*A);
    }
    for(int e=0;e<2;e++){
        Vec3 ev={ (e?0.28f:-0.28f),0.06f,0.72f };
        float sx,sy,dep; project(ev,sx,sy,dep);
        if(dep>0.05f){
            float k=(0.5f+2.5f*beat)*A;
            cv->add_glow(cv, sx,sy, 0.6f,1.0f,0.7f, k, 3.5f);
            for(int a=0;a<4;a++){
                float an=a*1.5708f+(float)t*1.5f; float r0=5,r1=8;
                lineAdd(fb,W,H, sx+cosf(an)*r0,sy+sinf(an)*r0, sx+cosf(an)*r1,sy+sinf(an)*r1, 0.2f,1.f,0.6f, 0.5f*A);
            }
        }
    }

    // 3) HUD overlay
    float hcx=cx, hcy=cy, R0=120.f + 8.f*sinf((float)t*0.6f) + 30.f*rms;
    for(int b=0;b<4;b++){
        float base=b*1.5708f+(float)t*0.4f;
        for(int seg=0;seg<2;seg++){
            float a0=base+seg*0.18f, a1=a0+0.5f; int steps=10;
            for(int i2=0;i2<steps;i2++){
                float u0=a0+(a1-a0)*i2/steps, u1=a0+(a1-a0)*(i2+1)/steps;
                lineAdd(fb,W,H, hcx+cosf(u0)*R0,hcy+sinf(u0)*R0, hcx+cosf(u1)*R0,hcy+sinf(u1)*R0, 0.1f,1.f,0.5f, 0.35f*A);
            }
        }
        float an=base;
        lineAdd(fb,W,H, hcx+cosf(an)*(R0-8),hcy+sinf(an)*(R0-8), hcx+cosf(an)*(R0+8),hcy+sinf(an)*(R0+8), 0.2f,1.f,0.6f, 0.4f*A);
    }
    lineAdd(fb,W,H, hcx-14,hcy, hcx-4,hcy, 0.2f,1.f,0.6f, 0.5f*A);
    lineAdd(fb,W,H, hcx+4,hcy, hcx+14,hcy, 0.2f,1.f,0.6f, 0.5f*A);
    lineAdd(fb,W,H, hcx,hcy-14, hcx,hcy-4, 0.2f,1.f,0.6f, 0.5f*A);
    lineAdd(fb,W,H, hcx,hcy+4, hcx,hcy+14, 0.2f,1.f,0.6f, 0.5f*A);
    auto corner=[&](float x,float y,float dx,float dy){
        lineAdd(fb,W,H, x,y, x+dx,y, 0.1f,1.f,0.45f, 0.5f*A);
        lineAdd(fb,W,H, x,y, x,y+dy, 0.1f,1.f,0.45f, 0.5f*A);
    };
    corner(10,10,34,0); corner(W-10,10,-34,0); corner(10,H-10,34,0); corner(W-10,H-10,-34,0);
    lineAdd(fb,W,H, 10,10, 10,32, 0.1f,1.f,0.45f, 0.5f*A);
    lineAdd(fb,W,H, W-10,10, W-10,32, 0.1f,1.f,0.45f, 0.5f*A);
    lineAdd(fb,W,H, 10,H-10, 10,H-32, 0.1f,1.f,0.45f, 0.5f*A);
    lineAdd(fb,W,H, W-10,H-10, W-10,H-32, 0.1f,1.f,0.45f, 0.5f*A);

    int NB=p->bandCount;
    if(NB>0){
        int bw=(W-40)/NB;
        for(int i=0;i<NB;i++){
            float e=p->bands[i]; float bh=e*e*70.f;
            int bx=20+i*bw, by=H-24;
            for(int y=0;y<(int)bh;y++)
                for(int x=0;x<bw-1;x++)
                    putAdd(fb,W,H, bx+x, by-y, 0.1f,1.f,0.4f, (0.10f+0.5f*(y/(bh+1)))*A);
        }
    }
    int topN=W/s->cw;
    for(int i=0;i<topN;i++){
        uint32_t gs=hashu((uint32_t)i*911u ^ (uint32_t)(t*6.0));
        if(hashf(i,(uint32_t)(t*4)) > 0.55f)
            drawGlyph(fb,W,H, i*s->cw, 16, s->cw, s->ch, gs, 0.1f,1.f,0.45f, 0.28f*A);
    }
    float scanY=fmodf((float)t*90.f, (float)H);
    for(int x=0;x<W;x++){ putAdd(fb,W,H,x,(int)scanY, 0.3f,1.f,0.6f, 0.25f*A);
                          putAdd(fb,W,H,x,(int)scanY+1, 0.2f,1.f,0.5f, 0.12f*A); }

    // destructive post-effects only when the scene is fully visible
    if(A > 0.98f){
        for(int y=0;y<H;y+=3){
            float* row=&fb[(size_t)y*W*3];
            for(int x=0;x<W*3;x++) row[x]*=0.55f;               // CRT scanlines
        }
        if(glitch > 0.15f){                                     // horizontal slice glitch
            int slices=1+(int)(glitch*4);
            for(int q=0;q<slices;q++){
                uint32_t hs=hashu((uint32_t)(p->frameNo*131 + q*977));
                int y0=(int)(hashf(hs,1)*H);
                int hgt=4+(int)(hashf(hs,2)*22);
                int dx=(int)((hashf(hs,3)-0.5f)*60.f*glitch);
                for(int y=y0; y<y0+hgt && y<H; y++){
                    float* row=&fb[(size_t)y*W*3];
                    memcpy(s->rowtmp.data(), row, (size_t)W*3*sizeof(float));
                    for(int x=0;x<W;x++){
                        int sx=x-dx; if(sx<0)sx+=W; if(sx>=W)sx-=W;
                        row[x*3+0]=s->rowtmp[sx*3+0];
                        row[x*3+1]=s->rowtmp[sx*3+1];
                        row[x*3+2]=s->rowtmp[sx*3+2];
                    }
                }
            }
        }
    }
}
