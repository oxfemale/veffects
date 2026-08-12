// spaceport.cpp -- "Spaceport" scene plugin.
// A deep-space station over a glowing planet: starfield backdrop, a lit-window
// equalizer on the station hull, docking arms with beacon lights, spaceships
// riding traffic lanes with engine trails, and distant traffic streaks.
//
// All animation is analytic from p->time, so any single frame renders correctly
// without simulation history (offline render and seeking both work).

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
// filled axis-aligned rectangle (additive)
static inline void rectAdd(float* fb, int W, int H, int x0,int y0,int x1,int y1,
                           float r,float g,float b,float k){
    if(x0>x1){int t=x0;x0=x1;x1=t;} if(y0>y1){int t=y0;y0=y1;y1=t;}
    for(int y=y0;y<=y1;y++) for(int x=x0;x<=x1;x++) putAdd(fb,W,H,x,y,r,g,b,k);
}

struct Star { float x, y; float base; float tw; };
struct State {
    int W, H;
    std::vector<Star> stars;
};

VFX_EXPORT void* vfx_plugin_create(int W, int H){
    State* s = new State();
    s->W = W; s->H = H;
    int NS = 320;
    s->stars.resize(NS);
    for(int i=0;i<NS;i++){
        Star& st = s->stars[i];
        st.x = hashf(i,1)*W;
        st.y = hashf(i,2)*H;
        st.base = 0.15f + hashf(i,3)*0.85f;
        st.tw = hashf(i,4)*6.2831853f;
    }
    return s;
}
VFX_EXPORT void vfx_plugin_destroy(void* st){ delete (State*)st; }

static VfxPluginInfo INFO = {
    VFX_PLUGIN_ABI, "Spaceport", "veffects",
    "A lit space station over a glowing planet with docking beacons and spaceships riding traffic lanes on engine trails."
};
VFX_EXPORT const VfxPluginInfo* vfx_plugin_info(void){ return &INFO; }

VFX_EXPORT void vfx_plugin_render(void* stp, const VfxCanvas* cv, const VfxParams* p){
    State* s = (State*)stp;
    float* fb = cv->fb; int W = cv->width, H = cv->height;
    float A = p->alpha;
    float bass=p->bass, mid=p->mid, tre=p->treble, rms=p->rms, beat=p->beat;
    float onset=p->onset, cen=clampf(p->centroid,0.f,1.f);
    double t = p->time;

    // centroid shifts the accent palette amber <-> cyan
    // warm amber = (1.0, 0.62, 0.22); cool cyan = (0.35, 0.9, 1.0)
    float aR = 1.00f, aG = 0.62f, aB = 0.22f;   // amber
    float cR = 0.35f, cG = 0.90f, cB = 1.00f;   // cyan
    float mixw = cen;                             // brighter timbre -> cooler
    float acR = aR + (cR-aR)*mixw;
    float acG = aG + (cG-aG)*mixw;
    float acB = aB + (cB-aB)*mixw;

    // 1) deep space vertical gradient (blue/violet), brighter with rms
    for(int y=0;y<H;y+=2){
        float gy = (float)y/H;
        float m = 0.006f + 0.020f*(1.f-gy)*(1.f-gy)*(0.5f+0.6f*rms);
        for(int x=0;x<W;x+=2) putAdd(fb,W,H,x,y, 0.05f,0.06f,0.16f, m*A);
    }

    // 2) starfield backdrop (twinkle with treble)
    for(size_t i=0;i<s->stars.size();i++){
        Star& st = s->stars[i];
        float tw = 0.55f + 0.45f*sinf((float)t*(1.3f+0.5f*(i&7)) + st.tw);
        float k = st.base*(0.35f + 0.65f*tw)*(0.6f+0.8f*tre);
        putAdd(fb,W,H,(int)st.x,(int)st.y, 0.8f,0.85f,1.0f, k*A);
        if(st.base>0.75f) cv->add_glow(cv, st.x, st.y, 0.7f,0.8f,1.0f, 0.10f*k*A, 2.2f);
    }

    // 3) large glowing planet on the lower-left (nebula glow with rms)
    float pcx = W*0.14f, pcy = H*0.86f, pR = H*0.55f;
    float glowPulse = 0.75f + 0.45f*rms + 0.15f*sinf((float)t*0.5f);
    // planet body: filled disc with a soft limb and a lit terminator toward upper-right
    {
        int y0 = (int)(pcy-pR), y1 = (int)(pcy+pR);
        for(int y=y0;y<=y1;y+=1){
            if(y<0||y>=H) continue;
            float dyp = (y-pcy)/pR;
            float span = 1.f - dyp*dyp; if(span<=0) continue;
            float hx = pR*sqrtf(span);
            int xa=(int)(pcx-hx), xb=(int)(pcx+hx);
            for(int x=xa;x<=xb;x+=1){
                if(x<0||x>=W) continue;
                float dxp=(x-pcx)/pR;
                float rr = dxp*dxp+dyp*dyp;
                // day-lit toward upper-right (light dir)
                float lit = clampf(0.28f + 0.55f*(dxp*0.7f - dyp*0.7f), 0.f, 1.f);
                float limb = 1.f - rr; // brighter center
                // banded gas-giant texture
                float band = 0.7f + 0.3f*sinf(dyp*10.f + dxp*2.f);
                float base = 0.020f + 0.10f*lit*lit*limb*band*glowPulse;
                // tint drifts violet(shadow)->warm(day)
                float rc = 0.35f + 0.55f*lit;
                float gc = 0.20f + 0.35f*lit;
                float bc = 0.45f + 0.25f*lit;
                putAdd(fb,W,H,x,y, rc,gc,bc, base*A);
            }
        }
        // atmosphere rim glow
        for(int a=0;a<200;a++){
            float an = a/200.f*6.2831853f;
            float gx = pcx+cosf(an)*pR*0.99f, gy = pcy+sinf(an)*pR*0.99f;
            float lit = clampf(0.3f+0.6f*(cosf(an)*0.7f - sinf(an)*0.7f),0.f,1.f);
            cv->add_glow(cv, gx,gy, 0.45f,0.55f,0.95f, 0.05f*(0.4f+lit)*glowPulse*A, 9.f);
        }
    }

    // 4) distant traffic: faint moving light streaks (with treble), across mid-sky
    int NT = 14;
    for(int i=0;i<NT;i++){
        float lane = 0.18f + 0.5f*hashf(i,21);
        float ly = H*lane;
        float sp = 40.f + hashf(i,22)*80.f;
        float dir = (hashf(i,23)>0.5f)?1.f:-1.f;
        float span = W + 80.f;
        float xx = fmodf(hashf(i,24)*span + (float)t*sp, span) - 40.f;
        if(dir<0) xx = span-40.f - xx;
        float k = (0.25f+0.6f*tre)*(0.4f+0.6f*hashf(i,25));
        float tr,tg,tb;
        if(hashf(i,26)>0.5f){ tr=acR; tg=acG; tb=acB; } else { tr=0.9f; tg=0.95f; tb=1.0f; }
        lineAdd(fb,W,H, xx,ly, xx-dir*6.f,ly+0.5f, tr,tg,tb, k*A);
        cv->add_glow(cv, xx,ly, tr,tg,tb, 0.20f*k*A, 2.0f);
    }

    // ---- STATION geometry (right-center) ----
    float stx = W*0.66f, sty = H*0.44f;
    float sway = 3.0f*sinf((float)t*0.25f);
    stx += sway; sty += 1.5f*cosf((float)t*0.2f);
    float hullBright = 0.14f + 0.10f*bass;

    // 4b) slow rotating outer ring around the station
    {
        float rot = (float)t*0.30f;
        float RR = 118.f;
        float ry = 0.34f; // ellipse squash (perspective)
        int seg=120;
        float cr=cosf(rot), sr=sinf(rot);
        for(int i=0;i<seg;i++){
            float a0 = i/(float)seg*6.2831853f;
            float a1 = (i+1)/(float)seg*6.2831853f;
            float x0 = cosf(a0)*RR, y0a = sinf(a0)*RR*ry;
            float x1 = cosf(a1)*RR, y1a = sinf(a1)*RR*ry;
            // rotate ring plane about vertical for a bit of tilt animation
            float px0 = stx + x0*cr, py0 = sty + y0a - x0*sr*0.15f;
            float px1 = stx + x1*cr, py1 = sty + y1a - x1*sr*0.15f;
            float depth = 0.5f+0.5f*sinf(a0+rot);
            float k=(0.10f+0.14f*depth)*(0.7f+0.5f*mid);
            lineAdd(fb,W,H, px0,py0, px1,py1, 0.45f,0.55f,0.85f, k*A);
        }
        // ring nav beacons blinking with treble
        for(int b=0;b<6;b++){
            float a = b/6.f*6.2831853f + rot;
            float bx = stx + cosf(a)*RR*cr;
            float by = sty + sinf(a)*RR*ry - cosf(a)*RR*sr*0.15f;
            float blink = 0.5f+0.5f*sinf((float)t*6.0f + b*1.7f);
            float k=(0.3f + 1.6f*blink*(0.4f+0.9f*tre));
            cv->add_glow(cv, bx,by, acR,acG,acB, k*A, 3.2f);
        }
    }

    // 5) central hub (octagon-ish body) with lit-window equalizer
    {
        float hw = 70.f, hh = 46.f;
        // hull plate
        rectAdd(fb,W,H,(int)(stx-hw),(int)(sty-hh),(int)(stx+hw),(int)(sty+hh),
                0.10f,0.12f,0.20f, hullBright*A);
        // beveled top/bottom darker plates
        rectAdd(fb,W,H,(int)(stx-hw),(int)(sty-hh),(int)(stx+hw),(int)(sty-hh+8),
                0.16f,0.18f,0.26f, hullBright*A);
        rectAdd(fb,W,H,(int)(stx-hw),(int)(sty+hh-8),(int)(stx+hw),(int)(sty+hh),
                0.06f,0.07f,0.12f, hullBright*A);
        // hull outline
        lineAdd(fb,W,H, stx-hw,sty-hh, stx+hw,sty-hh, 0.4f,0.5f,0.7f, 0.5f*A);
        lineAdd(fb,W,H, stx-hw,sty+hh, stx+hw,sty+hh, 0.4f,0.5f,0.7f, 0.5f*A);
        lineAdd(fb,W,H, stx-hw,sty-hh, stx-hw,sty+hh, 0.4f,0.5f,0.7f, 0.5f*A);
        lineAdd(fb,W,H, stx+hw,sty-hh, stx+hw,sty+hh, 0.4f,0.5f,0.7f, 0.5f*A);

        // WINDOW EQUALIZER: grid of windows, columns react to p->bands,
        // overall brightness pulses with bass.
        int NB = p->bandCount;
        int cols = 16, rows = 5;
        float gx0 = stx-hw+8, gy0 = sty-hh+11;
        float cwd = (2*hw-16)/cols, cht = (2*hh-20)/rows;
        for(int cxi=0;cxi<cols;cxi++){
            float bandE;
            if(NB>0){ int bi = cxi*NB/cols; bandE = p->bands[bi]; }
            else bandE = 0.4f+0.3f*sinf((float)t*3.f+cxi);
            // equalizer height: number of lit rows from bottom
            float litRows = bandE*rows;
            for(int ryi=0;ryi<rows;ryi++){
                int fromBottom = rows-1-ryi;
                float on;
                if((float)fromBottom < litRows) on = 1.f;
                else on = 0.12f; // dim ambient windows
                // per-window flicker so it reads as many rooms
                float fl = 0.7f+0.3f*sinf((float)t*2.0f + cxi*1.3f + ryi*0.7f
                                          + hashf(cxi, ryi)*6.28f);
                float wb = (0.12f + 1.4f*on*(0.5f+0.7f*bass))*fl;
                int wx0 = (int)(gx0+cxi*cwd)+1, wy0=(int)(gy0+ryi*cht)+1;
                int wx1 = (int)(gx0+(cxi+1)*cwd)-1, wy1=(int)(gy0+(ryi+1)*cht)-1;
                // window color: warm amber interior, tinted by centroid a touch
                float wr = 1.00f*0.75f + acR*0.25f;
                float wg = 0.72f*0.75f + acG*0.25f;
                float wbl= 0.30f*0.75f + acB*0.25f;
                rectAdd(fb,W,H, wx0,wy0,wx1,wy1, wr,wg,wbl, wb*A);
            }
        }
        // central sensor dome glow with bass
        cv->add_glow(cv, stx, sty-hh-4, acR,acG,acB, (0.5f+2.0f*bass)*A, 8.f);
    }

    // 6) docking arms / bays extending from the hub, each tipped with a beacon
    struct Arm { float ang; float len; };
    Arm arms[5] = {
        {-2.60f, 96.f}, {2.55f, 96.f}, {3.05f, 78.f}, {-0.55f, 70.f}, {0.55f, 70.f}
    };
    float dockPts[5][2];
    float HW=70.f, HH=46.f; // hull half-extents (must match section 5)
    for(int i=0;i<5;i++){
        float a = arms[i].ang;
        float ca=cosf(a), sa=sinf(a)*0.7f;
        // start the arm on the hull boundary along direction (ca,sa) so trusses
        // never cross the window face
        float sxs = fabsf(ca)>1e-3f ? HW/fabsf(ca) : 1e9f;
        float sys = fabsf(sa)>1e-3f ? HH/fabsf(sa) : 1e9f;
        float sMin = sxs<sys?sxs:sys;
        float bxs = stx + ca*sMin, bys = sty + sa*sMin;
        float ex = stx + ca*(sMin+arms[i].len);
        float ey = sty + sa*(sMin+arms[i].len);
        // arm truss (double line)
        float nx = -sinf(a), ny = cosf(a)*0.7f;
        lineAdd(fb,W,H, bxs+nx*4,bys+ny*4, ex+nx*4,ey+ny*4, 0.30f,0.36f,0.5f, (0.25f+0.2f*mid)*A);
        lineAdd(fb,W,H, bxs-nx*4,bys-ny*4, ex-nx*4,ey-ny*4, 0.30f,0.36f,0.5f, (0.25f+0.2f*mid)*A);
        // cross-braces
        for(int c=1;c<5;c++){
            float tt=c/5.f;
            float mx=bxs+(ex-bxs)*tt, my=bys+(ey-bys)*tt;
            lineAdd(fb,W,H, mx+nx*4,my+ny*4, mx-nx*4,my-ny*4, 0.22f,0.27f,0.4f, 0.2f*A);
        }
        // docking bay pad at tip
        rectAdd(fb,W,H,(int)(ex-5),(int)(ey-4),(int)(ex+5),(int)(ey+4), 0.14f,0.16f,0.24f, 0.4f*A);
        // beacon light blinking (staggered) with treble
        float blink = 0.5f+0.5f*sinf((float)t*5.0f + i*1.9f);
        float k = 0.3f + 1.8f*blink*(0.4f+0.9f*tre);
        cv->add_glow(cv, ex,ey, acR,acG,acB, k*A, 4.0f);
        dockPts[i][0]=ex; dockPts[i][1]=ey;
    }

    // 7) SPACESHIPS riding traffic lanes toward/from docking bays.
    // Each ship position is analytic via fmod of time; engine trail + running
    // lights; a launch/dock flare pulses on beat/onset.
    int NSHIP = 6;
    float flare = clampf(beat*1.1f + onset*0.7f, 0.f, 1.f);
    for(int i=0;i<NSHIP;i++){
        // lane parameters
        float speed = 0.06f + 0.05f*hashf(i,50);
        float phase = hashf(i,51);
        float u = fmodf((float)t*speed + phase, 1.f);      // 0..1 along the lane
        int bay = (int)(hashf(i,52)*5.f) % 5;
        float bx = dockPts[bay][0], by = dockPts[bay][1];
        // lane goes from an off-screen edge to the chosen docking bay and back.
        // use a triangle wave so ships arrive (u:0->0.5) then depart (0.5->1).
        float tri = (u<0.5f)? (u*2.f) : (2.f-u*2.f);       // 0..1..0
        bool arriving = (u<0.5f);
        // entry point on screen edge (depends on ship)
        float side = hashf(i,53);
        float ex, ey;
        if(side<0.33f){ ex=-30.f; ey=H*(0.15f+0.6f*hashf(i,54)); }
        else if(side<0.66f){ ex=W+30.f; ey=H*(0.15f+0.6f*hashf(i,55)); }
        else { ex=W*(0.2f+0.6f*hashf(i,56)); ey=-30.f; }
        // curved lane (quadratic bezier through a control point) for a nice arc
        float mcx = (ex+bx)*0.5f + (hashf(i,57)-0.5f)*160.f;
        float mcy = (ey+by)*0.5f - 60.f - 60.f*hashf(i,58);
        float tt = tri;
        float omt = 1.f-tt;
        float shx = omt*omt*ex + 2*omt*tt*mcx + tt*tt*bx;
        float shy = omt*omt*ey + 2*omt*tt*mcy + tt*tt*by;
        // heading (derivative of bezier)
        float dxh = 2*omt*(mcx-ex) + 2*tt*(bx-mcx);
        float dyh = 2*omt*(mcy-ey) + 2*tt*(by-mcy);
        if(arriving){ /* moving toward bay */ } else { dxh=-dxh; dyh=-dyh; }
        float hl = sqrtf(dxh*dxh+dyh*dyh)+1e-3f;
        dxh/=hl; dyh/=hl;

        // ship body (small elongated hull)
        float bodyk = 0.5f;
        float px=-dyh, py=dxh; // perpendicular
        // hull
        lineAdd(fb,W,H, shx-dxh*5,shy-dyh*5, shx+dxh*5,shy+dyh*5, 0.5f,0.55f,0.7f, bodyk*A);
        lineAdd(fb,W,H, shx-dxh*3+px*2,shy-dyh*3+py*2, shx+dxh*4,shy+dyh*4, 0.4f,0.45f,0.6f, bodyk*A);
        lineAdd(fb,W,H, shx-dxh*3-px*2,shy-dyh*3-py*2, shx+dxh*4,shy+dyh*4, 0.4f,0.45f,0.6f, bodyk*A);

        // running lights (red port / green starboard) blinking
        float rl = 0.5f+0.5f*sinf((float)t*8.f+i);
        cv->add_glow(cv, shx+px*3, shy+py*3, 1.0f,0.2f,0.2f, (0.3f+0.5f*rl)*A, 1.8f);
        cv->add_glow(cv, shx-px*3, shy-py*3, 0.2f,1.0f,0.3f, (0.3f+0.5f*rl)*A, 1.8f);

        // ENGINE TRAIL behind the ship (cyan/white), flares on beat/onset
        float ex2 = shx - dxh*6, ey2 = shy - dyh*6; // engine nozzle
        float trailLen = 26.f + 40.f*flare + 20.f*rms;
        int steps = 22;
        for(int q=0;q<steps;q++){
            float tq = q/(float)steps;
            float tx = ex2 - dxh*trailLen*tq;
            float ty = ey2 - dyh*trailLen*tq;
            // widen and dim toward the tail
            float fade = (1.f-tq)*(1.f-tq);
            float jitter = (hashf(i, q + (uint32_t)(t*20))-0.5f)*2.5f*(1.f-fade);
            tx += px*jitter; ty += py*jitter;
            float k = (0.35f + 1.3f*flare)*fade;
            // color: hot white core near nozzle -> cyan/amber tail (centroid)
            float mixc = tq;
            float er = (1.0f)*(1.f-mixc) + acR*mixc;
            float eg = (1.0f)*(1.f-mixc) + acG*mixc;
            float eb = (1.0f)*(1.f-mixc) + acB*mixc;
            cv->add_glow(cv, tx,ty, er,eg,eb, k*A, 2.2f+3.0f*fade);
        }
        // bright engine core
        cv->add_glow(cv, ex2,ey2, 0.7f,0.95f,1.0f, (0.8f+2.2f*flare)*A, 3.0f);

        // docking/launch flare burst when very close to the bay on a beat
        if(tri>0.9f && flare>0.2f){
            cv->add_glow(cv, bx,by, acR,acG,acB, flare*2.5f*A, 10.f);
        }
    }

    // 8) a couple of comm antennas on top of the hub, tips blinking with treble
    for(int i=0;i<2;i++){
        float axr = stx + (i? 22.f : -22.f);
        float ayt = sty - 46.f;
        float tipY = ayt - (34.f + 6.f*sinf((float)t*0.7f+i));
        lineAdd(fb,W,H, axr,ayt, axr+(i?6:-6),tipY, 0.35f,0.4f,0.55f, 0.35f*A);
        float blink = (sinf((float)t*7.f + i*3.1f)>0.4f)?1.f:0.15f;
        cv->add_glow(cv, axr+(i?6:-6),tipY, 1.0f,0.3f,0.25f, (0.3f+1.4f*blink*(0.4f+0.8f*tre))*A, 2.4f);
    }
}
