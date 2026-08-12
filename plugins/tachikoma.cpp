// tachikoma.cpp -- "Tachikoma" scene plugin.
// An adorable blue robotic spider-tank (think-tank) from Ghost in the Shell:
// a bulbous rounded body with the iconic multi-lens eye cluster that glows and
// blinks, several articulated legs that step to the beat, and light cyan HUD /
// targeting brackets with small spectrum readouts.
//
// All animation derives from the absolute time p->time (analytic, no cross-frame
// state), so offline render and seeking both work. Palette is cheerful cyan /
// teal / sky-blue on a dark background -- deliberately distinct from the green
// Ghost-in-the-Shell scene.

#include "veffects_plugin.h"
#include <cmath>
#include <cstdint>
#include <vector>

static inline float clampf(float x, float a, float b){ return x<a?a:(x>b?b:x); }
static inline float smoothstepf(float a, float b, float x){
    float t = clampf((x-a)/(b-a), 0.f, 1.f); return t*t*(3.f-2.f*t);
}
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
// thick line: draw a couple of parallel offsets for weight w (in px).
static inline void lineThick(float* fb, int W, int H, float x0,float y0,float x1,float y1,
                             float r,float g,float b,float k,float w){
    float dx=x1-x0, dy=y1-y0; float len=sqrtf(dx*dx+dy*dy); if(len<1e-3f)len=1e-3f;
    float nx=-dy/len, ny=dx/len;
    int half=(int)(w*0.5f); if(half<0)half=0;
    for(int o=-half;o<=half;o++){
        float att = 1.f - 0.35f*(fabsf((float)o)/(half+1));
        lineAdd(fb,W,H, x0+nx*o,y0+ny*o, x1+nx*o,y1+ny*o, r,g,b,k*att);
    }
}

struct State { int W, H; };

static VfxPluginInfo INFO = {
    VFX_PLUGIN_ABI, "Tachikoma", "veffects",
    "A cheerful cyan robotic spider-tank with a glowing multi-lens eye cluster and beat-stepping legs over a HUD."
};
VFX_EXPORT const VfxPluginInfo* vfx_plugin_info(void){ return &INFO; }

VFX_EXPORT void* vfx_plugin_create(int W, int H){
    State* s = new State(); s->W=W; s->H=H; return s;
}
VFX_EXPORT void vfx_plugin_destroy(void* st){ delete (State*)st; }

// Filled soft blob using add_glow rings (cheap, no per-pixel full-frame loop).
static void softBlob(const VfxCanvas* cv, float cx, float cy, float rad,
                     float r, float g, float b, float k){
    cv->add_glow(cv, cx, cy, r,g,b, k, rad);
}

// A round camera lens: dark rim ring, bright iris core, tiny specular highlight.
static void drawLens(const VfxCanvas* cv, float* fb, int W, int H,
                     float cx, float cy, float rad, float open,
                     float r, float g, float b, float glow){
    // outer metallic rim
    int seg = 20;
    for(int i=0;i<seg;i++){
        float a0 = (float)i/seg*6.2831853f, a1=(float)(i+1)/seg*6.2831853f;
        lineThick(fb,W,H, cx+cosf(a0)*rad, cy+sinf(a0)*rad,
                          cx+cosf(a1)*rad, cy+sinf(a1)*rad,
                          0.35f,0.75f,0.95f, 0.5f, 2.f);
    }
    // iris glow (scaled by eyelid "open")
    float ir = rad*0.72f*open;
    if(ir > 0.6f){
        cv->add_glow(cv, cx, cy, r,g,b, glow*0.8f*open, ir*1.9f);
        cv->add_glow(cv, cx, cy, r*1.2f,g*1.2f,b*1.3f, glow*1.1f*open, ir*0.9f);
        // bright pupil
        cv->add_glow(cv, cx, cy, 0.5f,0.85f,1.0f, glow*0.9f*open, ir*0.38f);
        // specular highlight, upper-left
        cv->add_glow(cv, cx-ir*0.32f, cy-ir*0.34f, 1.0f,1.0f,1.0f, glow*0.9f*open, ir*0.28f);
    }
}

VFX_EXPORT void vfx_plugin_render(void* st, const VfxCanvas* cv, const VfxParams* p){
    State* s = (State*)st;
    float* fb = cv->fb; int W = cv->width, H = cv->height;
    float A = p->alpha;
    float bass=p->bass, mid=p->mid, tre=p->treble, rms=p->rms, beat=p->beat;
    float onset=p->onset; double t=p->time;
    (void)s; (void)mid;

    // ---- background: dark navy vignette with subtle teal grid glow ----
    for(int y=0;y<H;y+=2){
        float gy=(float)y/H;
        for(int x=0;x<W;x+=2){
            float gx=(float)x/W;
            float d = (gx-0.5f)*(gx-0.5f)+(gy-0.55f)*(gy-0.55f);
            float m = 0.010f*(1.f-clampf(d*1.8f,0.f,1.f)) + 0.003f;
            putAdd(fb,W,H,x,y, 0.02f,0.10f,0.20f, m*A);
        }
    }
    // faint moving grid lines (floor perspective feel)
    for(int gx=0; gx<=W; gx+=48){
        float xo = gx + 6.f*sinf((float)t*0.3f + gx*0.05f);
        lineAdd(fb,W,H, xo, H*0.62f, xo, H, 0.05f,0.35f,0.5f, 0.05f*A);
    }
    for(int gy=(int)(H*0.62f); gy<H; gy+=22){
        lineAdd(fb,W,H, 0, gy, W, gy, 0.05f,0.3f,0.45f, 0.04f*A);
    }

    // ============================================================
    // HERO TACHIKOMA
    // ============================================================
    float cx = W*0.5f;
    // body bobs with bass + gentle idle sway
    float bob = 10.f*sinf((float)t*1.6f)*(0.4f+0.8f*bass) + 6.f*sinf((float)t*0.7f);
    float cy = H*0.46f + bob;
    float sway = 8.f*sinf((float)t*0.5f);
    cx += sway;

    float unit = 74.f;                 // body scale (px)
    float bodyR = unit*(1.f + 0.06f*bass);

    // ---- legs: 3 per side, articulated, stepping to the beat ----
    // Each leg has a hip on the lower body, a knee, and a foot on the ground.
    float groundY = cy + bodyR*1.15f + 40.f;
    // step phase: legs alternate in a tripod-ish gait, cadence follows time+beat
    float cadence = (float)t*3.2f + beat*1.5f;
    for(int side=0; side<2; side++){
        float sgn = side? 1.f : -1.f;
        for(int L=0; L<3; L++){
            float hipx = cx + sgn*(bodyR*0.55f);
            float hipy = cy + bodyR*(0.15f + 0.28f*L) - bodyR*0.25f;
            // horizontal spread of the foot
            float spread = bodyR*(0.85f + 0.55f*L);
            float footBaseX = cx + sgn*spread;
            // stepping: lift + forward swing, phase-staggered per leg/side
            float ph = cadence + L*2.094f + side*3.14159f;
            float lift = clampf(sinf(ph),0.f,1.f);
            lift = lift*lift;
            float step = sinf(ph);
            float footx = footBaseX + step*10.f*(0.5f+beat);
            float footy = groundY - lift*(16.f + 22.f*beat) - L*4.f;
            // knee: push outward and up, flexes with the step
            float mx = (hipx+footx)*0.5f;
            float kneeOut = sgn*(18.f + 10.f*lift);
            float kneex = mx + kneeOut;
            float kneey = (hipy+footy)*0.5f - (18.f + 14.f*lift);
            // upper + lower leg segments (tapered, glowing joints)
            float legK = (0.5f + 0.5f*rms);
            lineThick(fb,W,H, hipx,hipy, kneex,kneey, 0.25f,0.65f,0.9f, 0.55f*legK*A, 6.f);
            lineThick(fb,W,H, kneex,kneey, footx,footy, 0.22f,0.6f,0.85f, 0.5f*legK*A, 4.f);
            // joint glows
            cv->add_glow(cv, kneex,kneey, 0.4f,0.8f,1.0f, 0.4f*A, 5.f);
            // foot pad + contact spark when planted
            cv->add_glow(cv, footx,footy, 0.5f,0.85f,1.0f, 0.5f*A, 4.f);
            float planted = 1.f - lift;
            if(planted > 0.7f){
                cv->add_glow(cv, footx, footy+3.f, 0.3f,0.7f,0.95f, 0.35f*planted*A, 7.f);
            }
        }
    }

    // ---- body: bulbous rounded shell built from stacked soft blobs ----
    // base teal shell
    softBlob(cv, cx, cy, bodyR*1.35f, 0.06f,0.28f,0.40f, 0.45f*A);
    softBlob(cv, cx, cy, bodyR*1.05f, 0.09f,0.38f,0.52f, 0.5f*A);
    // top dome highlight (sky blue), slightly up
    softBlob(cv, cx, cy - bodyR*0.35f, bodyR*0.7f, 0.16f,0.50f,0.70f, 0.5f*A);
    // rim light along the top
    softBlob(cv, cx - bodyR*0.35f, cy - bodyR*0.55f, bodyR*0.35f, 0.35f,0.7f,0.9f, 0.5f*A);
    // shell outline arcs for a "hard surface" read
    {
        int seg=26; float rr=bodyR*1.12f;
        for(int i=0;i<seg;i++){
            float a0=(float)i/seg*6.2831853f, a1=(float)(i+1)/seg*6.2831853f;
            // slightly squashed ellipse (wider than tall) = bulbous body
            float ex0=cx+cosf(a0)*rr, ey0=cy+sinf(a0)*rr*0.92f;
            float ex1=cx+cosf(a1)*rr, ey1=cy+sinf(a1)*rr*0.92f;
            lineAdd(fb,W,H, ex0,ey0, ex1,ey1, 0.3f,0.7f,0.9f, 0.28f*A);
        }
    }
    // two little antennae/sensor stalks on top wiggling with treble
    for(int a=0;a<2;a++){
        float sgn=a?1.f:-1.f;
        float bx=cx+sgn*bodyR*0.3f, by=cy-bodyR*1.05f;
        float tx=bx+sgn*(6.f+8.f*tre)+4.f*sinf((float)t*4.f+a);
        float ty=by-26.f-10.f*tre;
        lineThick(fb,W,H, bx,by, tx,ty, 0.3f,0.7f,0.9f, 0.4f*A, 2.f);
        cv->add_glow(cv, tx,ty, 0.6f,0.9f,1.0f, (0.4f+0.6f*tre)*A, 4.f);
    }

    // ---- ICONIC multi-lens EYE CLUSTER ----
    // blink: brief global closure a couple times, derived from time.
    float blinkPhase = fmodf((float)t*0.5f, 4.0f);   // a blink window every ~8s of phase
    float blink = 1.f;
    if(blinkPhase < 0.22f){
        // 0->closed->open over the window
        float u = blinkPhase/0.22f;
        blink = fabsf(u-0.5f)*2.f;          // 1 at edges, 0 mid
        blink = smoothstepf(0.0f,1.0f,blink);
    }
    // pulsing glow with rms + onset
    float eyeGlow = (0.55f + 0.9f*rms + 0.7f*onset) * blink;

    // eye cluster sits on the front/upper face of the body
    float ecx = cx, ecy = cy - bodyR*0.12f;
    float faceR = bodyR*0.62f;
    // dark faceplate behind the lenses
    softBlob(cv, ecx, ecy, faceR*1.15f, 0.02f,0.06f,0.10f, 0.8f*A);
    // big central eye
    drawLens(cv, fb,W,H, ecx, ecy, faceR*0.62f, blink, 0.2f,0.8f,1.0f, eyeGlow*A);
    // satellite lenses arranged around the central one
    int NL = 5;
    for(int i=0;i<NL;i++){
        float ang = (float)i/NL*6.2831853f + 0.4f + (float)t*0.15f;
        float rr = faceR*0.92f;
        float lx = ecx + cosf(ang)*rr;
        float ly = ecy + sinf(ang)*rr*0.92f;
        float lr = faceR*0.26f;
        // each satellite pulses to a different spectrum band for a "sensor array" feel
        float bandv = 0.f;
        if(p->bandCount>0){ int bi=(i* p->bandCount)/NL; bandv = p->bands[bi]; }
        float lg = (0.4f + 0.9f*bandv + 0.5f*rms)*blink;
        // hue-shift the satellites within cyan->sky-blue for variety
        float rr2=0.2f, gg2=0.75f, bb2=1.0f;
        drawLens(cv, fb,W,H, lx, ly, lr, blink, rr2,gg2,bb2, lg*A);
    }
    // aggregate cluster bloom
    cv->add_glow(cv, ecx, ecy, 0.22f,0.65f,1.0f, 0.28f*eyeGlow*A, faceR*1.6f);

    // ============================================================
    // BACKGROUND MINI-TACHIKOMAS (a couple of smaller ones)
    // ============================================================
    for(int m=0;m<2;m++){
        float mm = m?1.f:-1.f;
        float mcx = cx + mm*W*0.32f - sway*0.5f;
        float mbob = 6.f*sinf((float)t*1.4f + m*1.7f)*(0.4f+0.7f*bass);
        float mcy = H*0.60f + mbob;
        float mr = 30.f;
        // simple legs
        for(int side=0;side<2;side++){
            float sgn=side?1.f:-1.f;
            for(int L=0;L<2;L++){
                float ph=(float)t*3.0f + L*2.2f + side*3.14159f + m*1.1f;
                float lift=clampf(sinf(ph),0.f,1.f); lift*=lift;
                float hipx=mcx+sgn*mr*0.5f, hipy=mcy+mr*0.2f;
                float footx=mcx+sgn*mr*(1.1f+0.4f*L);
                float footy=mcy+mr*1.4f - lift*10.f;
                float kx=(hipx+footx)*0.5f+sgn*8.f, ky=(hipy+footy)*0.5f-10.f;
                lineThick(fb,W,H, hipx,hipy,kx,ky, 0.2f,0.55f,0.8f, 0.4f*A, 3.f);
                lineThick(fb,W,H, kx,ky,footx,footy, 0.18f,0.5f,0.75f, 0.35f*A, 2.f);
                cv->add_glow(cv, footx,footy, 0.4f,0.75f,0.95f, 0.3f*A, 3.f);
            }
        }
        // body
        softBlob(cv, mcx, mcy, mr*1.3f, 0.06f,0.28f,0.4f, 0.5f*A);
        softBlob(cv, mcx, mcy-mr*0.3f, mr*0.7f, 0.14f,0.45f,0.62f, 0.55f*A);
        // small eye cluster
        float meg=(0.4f+0.7f*rms)*blink;
        drawLens(cv, fb,W,H, mcx, mcy-mr*0.1f, mr*0.5f, blink, 0.2f,0.75f,1.0f, meg*A);
        for(int i=0;i<3;i++){
            float ang=(float)i/3*6.2831853f+(float)t*0.2f;
            float lx=mcx+cosf(ang)*mr*0.75f, ly=mcy-mr*0.1f+sinf(ang)*mr*0.7f;
            drawLens(cv, fb,W,H, lx,ly, mr*0.2f, blink, 0.2f,0.7f,1.0f, meg*0.8f*A);
        }
    }

    // ============================================================
    // HUD: targeting brackets, corner frames, spectrum readouts, beat ping
    // ============================================================
    // targeting brackets around the hero
    {
        float bR = bodyR*1.9f;
        float bx0=cx-bR, by0=cy-bR, bx1=cx+bR, by1=cy+bR;
        float len=22.f;
        float k=0.5f*A;
        float r=0.2f,g=0.8f,b=1.0f;
        // corners
        lineAdd(fb,W,H, bx0,by0, bx0+len,by0, r,g,b,k); lineAdd(fb,W,H, bx0,by0, bx0,by0+len, r,g,b,k);
        lineAdd(fb,W,H, bx1,by0, bx1-len,by0, r,g,b,k); lineAdd(fb,W,H, bx1,by0, bx1,by0+len, r,g,b,k);
        lineAdd(fb,W,H, bx0,by1, bx0+len,by1, r,g,b,k); lineAdd(fb,W,H, bx0,by1, bx0,by1-len, r,g,b,k);
        lineAdd(fb,W,H, bx1,by1, bx1-len,by1, r,g,b,k); lineAdd(fb,W,H, bx1,by1, bx1,by1-len, r,g,b,k);
        // rotating tick ring
        float R0 = bR*1.15f;
        for(int i=0;i<24;i++){
            float an=(float)i/24*6.2831853f + (float)t*0.4f;
            float t0=(i%6==0)?10.f:4.f;
            lineAdd(fb,W,H, cx+cosf(an)*R0, cy+sinf(an)*R0,
                            cx+cosf(an)*(R0+t0), cy+sinf(an)*(R0+t0), r,g,b, 0.3f*A);
        }
    }
    // corner HUD frames
    auto corner=[&](float x,float y,float dx,float dy){
        lineAdd(fb,W,H, x,y, x+dx,y, 0.2f,0.8f,1.0f, 0.5f*A);
        lineAdd(fb,W,H, x,y, x,y+dy, 0.2f,0.8f,1.0f, 0.5f*A);
    };
    corner(12,12,30,0);   corner(12,12,0,30);
    corner(W-12,12,-30,0); corner(W-12,12,0,30);
    corner(12,H-12,30,0);  corner(12,H-12,0,-30);
    corner(W-12,H-12,-30,0); corner(W-12,H-12,0,-30);

    // small data glyphs (little cyan tick marks / bars) top-left
    for(int i=0;i<6;i++){
        float gx=22.f+i*10.f;
        float on = hashf(i, (uint32_t)(t*3.0)) > 0.4f ? 1.f : 0.4f;
        lineAdd(fb,W,H, gx, 30, gx, 30+ (4+6*hashf(i,99)), 0.2f,0.8f,1.0f, 0.4f*on*A);
    }

    // spectrum readout along the bottom, cyan bars
    int NB=p->bandCount;
    if(NB>0){
        int used = NB>32?32:NB;
        int bw=(W-40)/used;
        for(int i=0;i<used;i++){
            float e=p->bands[(i*NB)/used];
            float bh=e*e*60.f;
            int bx=20+i*bw, by=H-18;
            for(int y=0;y<(int)bh;y++){
                float f=(float)y/(bh+1.f);
                putAdd(fb,W,H, bx, by-y, 0.15f,0.7f,1.0f, (0.15f+0.6f*f)*A);
                if(bw>2) putAdd(fb,W,H, bx+1, by-y, 0.15f,0.7f,1.0f, (0.10f+0.4f*f)*A);
            }
            // cap glow
            cv->add_glow(cv, bx+0.5f, by-bh, 0.3f,0.8f,1.0f, 0.15f*A, 3.f);
        }
    }

    // beat ping: expanding cyan ring, triggered by beat/onset, phase from time.
    float pingT = fmodf((float)t, 0.9f);   // ping cycle
    float ping = clampf(beat*1.2f + onset, 0.f, 1.5f);
    if(ping > 0.25f){
        float pr = pingT/0.9f;
        float rad = 40.f + pr*260.f;
        float fade = (1.f - pr) * clampf(ping,0.f,1.f);
        int seg=40;
        for(int i=0;i<seg;i++){
            float a0=(float)i/seg*6.2831853f, a1=(float)(i+1)/seg*6.2831853f;
            lineAdd(fb,W,H, cx+cosf(a0)*rad, cy+sinf(a0)*rad,
                            cx+cosf(a1)*rad, cy+sinf(a1)*rad, 0.3f,0.8f,1.0f, 0.35f*fade*A);
        }
    }
}
