// microbes.cpp -- "Microbes" scene plugin.
// A view down a microscope into a droplet: translucent teal/green cells and
// microbes -- rod-shaped bacilli with wiggling flagella, round cocci in
// chains, morphing amoebas, and tiny darting specks -- some slowly dividing,
// swimming on smooth Lissajous paths inside a bright circular microscope field.
//
// All animation is derived from the absolute time p->time (analytic), so any
// frame renders correctly without simulation history (offline render + seek).

#include "veffects_plugin.h"
#include <cmath>
#include <cstdint>
#include <vector>

static inline float clampf(float x, float a, float b){ return x<a?a:(x>b?b:x); }
static inline float smoothstep(float e0, float e1, float x){
    float t = clampf((x-e0)/(e1-e0), 0.f, 1.f); return t*t*(3.f-2.f*t);
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
    for(int i=0;i<=n;i++){ float t=(float)i/n;
        putAdd(fb,W,H,(int)(x0+dx*t),(int)(y0+dy*t),r,g,b,k); }
}

enum MType { COCCUS=0, BACILLUS=1, AMOEBA=2, SPECK=3 };

struct Microbe {
    int   type;
    uint32_t seed;
    float px, py;          // Lissajous centre params (amplitude fraction of field)
    float fx, fy, phx, phy;// path frequencies + phases
    float base;            // base radius (px)
    float depth;           // 0 = far / out of focus, 1 = near / sharp
    float hue;             // body hue offset [-0.06..0.06]
    float wigF, wigP;      // flagella / membrane wiggle freq + phase
    float divRate, divPh;  // division cycle rate + phase (0 = never divides)
    int   chain;           // cocci count in a cluster/chain (1..5)
    float orient;          // body orientation drift
};

struct State {
    int W, H;
    float cx, cy, R;       // microscope field centre + radius
    std::vector<Microbe> mic;
};

// soft translucent cell: halo + body + coloured nucleus + optional membrane rim.
static void drawCell(const VfxCanvas* cv, float* fb, int W, int H,
                     float x, float y, float rad,
                     float br,float bg,float bb,      // body colour
                     float nr,float ng,float nb,      // nucleus colour
                     float hr,float hg,float hb,      // rim highlight colour
                     float k, float focus, float t, uint32_t seed){
    if(rad < 0.5f) rad = 0.5f;
    // out-of-focus cells: bigger, softer, dimmer body; sharp cells: tighter core + rim
    float soft = 1.f + (1.f-focus)*1.2f;
    cv->add_glow(cv, x, y, br,bg,bb, k*0.22f*(0.6f+0.4f*focus), rad*1.9f*soft);
    cv->add_glow(cv, x, y, br,bg,bb, k*0.42f, rad*1.05f*soft);
    // nucleus, slightly offset, gently drifting
    float noff = rad*0.22f;
    float nx = x + cosf(t*0.7f+seed*0.017f)*noff;
    float ny = y + sinf(t*0.6f+seed*0.011f)*noff;
    cv->add_glow(cv, nx, ny, nr,ng,nb, k*0.80f*(0.55f+0.45f*focus), rad*0.44f);
    // tighter bright nucleus core so the magenta/violet reads through the body
    cv->add_glow(cv, nx, ny, nr,ng,nb, k*0.55f*(0.5f+0.5f*focus), rad*0.20f);
    // membrane rim (only meaningful when reasonably in focus)
    if(focus > 0.35f){
        int seg = 14;
        float rk = k*0.30f*smoothstep(0.35f,0.8f,focus);
        for(int i=0;i<seg;i++){
            float a0 = (float)i/seg*6.2831853f;
            float a1 = (float)(i+1)/seg*6.2831853f;
            float wob0 = 1.f + 0.06f*sinf(a0*3.f + t*1.3f + seed*0.03f);
            float wob1 = 1.f + 0.06f*sinf(a1*3.f + t*1.3f + seed*0.03f);
            lineAdd(fb,W,H, x+cosf(a0)*rad*wob0, y+sinf(a0)*rad*wob0,
                            x+cosf(a1)*rad*wob1, y+sinf(a1)*rad*wob1,
                    hr,hg,hb, rk);
        }
    }
}

static VfxPluginInfo INFO = {
    VFX_PLUGIN_ABI, "Microbes", "veffects",
    "A microscope view into a droplet of translucent teal cells, wiggling bacilli, dividing cocci and darting specks."
};
VFX_EXPORT const VfxPluginInfo* vfx_plugin_info(void){ return &INFO; }

VFX_EXPORT void* vfx_plugin_create(int W, int H){
    State* s = new State();
    s->W = W; s->H = H;
    s->cx = W*0.5f; s->cy = H*0.5f;
    s->R  = 0.52f * (float)((W<H)?W:H);   // microscope field radius
    int N = 96;
    s->mic.resize(N);
    for(int i=0;i<N;i++){
        Microbe& m = s->mic[i];
        uint32_t sd = hashu((uint32_t)i*2654435761U + 7u);
        m.seed = sd;
        float r0 = hashf(sd,1);
        // type distribution
        if(r0 < 0.30f)      m.type = COCCUS;
        else if(r0 < 0.55f) m.type = BACILLUS;
        else if(r0 < 0.72f) m.type = AMOEBA;
        else                m.type = SPECK;
        m.px  = 0.20f + hashf(sd,2)*0.72f;   // orbit radius fraction of field
        m.py  = 0.20f + hashf(sd,3)*0.72f;
        m.fx  = 0.05f + hashf(sd,4)*0.22f;
        m.fy  = 0.05f + hashf(sd,5)*0.22f;
        m.phx = hashf(sd,6)*6.2831853f;
        m.phy = hashf(sd,7)*6.2831853f;
        m.depth = hashf(sd,8);
        m.hue = (hashf(sd,9)-0.5f)*0.12f;
        m.wigF = 4.f + hashf(sd,10)*7.f;
        m.wigP = hashf(sd,11)*6.2831853f;
        m.divRate = (hashf(sd,12) < 0.5f) ? (0.03f + hashf(sd,13)*0.06f) : 0.f;
        m.divPh   = hashf(sd,14);
        m.chain   = 1 + (int)(hashf(sd,15)*4.99f);
        m.orient  = hashf(sd,16)*6.2831853f;

        switch(m.type){
            case COCCUS:   m.base = 7.f  + hashf(sd,20)*6.f;  break;
            case BACILLUS: m.base = 6.f  + hashf(sd,20)*4.f;  break;
            case AMOEBA:   m.base = 14.f + hashf(sd,20)*12.f; break;
            default:       m.base = 1.6f + hashf(sd,20)*1.8f; break; // SPECK
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
    float onset=p->onset, beat=p->beat, centroid=clampf(p->centroid,0.f,1.f);
    double t = p->time;
    float cx=s->cx, cy=s->cy, R=s->R;

    // beat throb: on-onset pulse + slow breathing
    float throb = 0.85f + 0.30f*beat + 0.55f*onset;
    float swim  = 0.6f + 1.7f*bass;          // bass -> faster swimming / swarming
    // centroid shifts body palette green(0) <-> violet, applied as extra hue push
    float chue  = (centroid-0.5f)*0.18f;

    // --- microscope field: faint teal wash inside the circle + bright rim ---
    // additive-only, so darkness outside comes for free (buffer starts at 0)
    int y0 = (int)(cy-R-2), y1 = (int)(cy+R+2);
    if(y0<0)y0=0; if(y1>H)y1=H;
    for(int y=y0;y<y1;y+=2){
        float dy = (y-cy);
        for(int x=0;x<W;x+=2){
            float dx = (x-cx);
            float d = sqrtf(dx*dx+dy*dy);
            if(d > R) continue;
            float rr = d/R;
            // gentle radial darkening toward the rim, brighter core
            float fld = (0.010f + 0.020f*(1.f-rr*rr))*(0.7f+0.6f*rms);
            putAdd(fb,W,H,x,y, 0.02f,0.10f,0.11f, fld*A);
            putAdd(fb,W,H,x+1,y, 0.02f,0.10f,0.11f, fld*A);
        }
    }
    // bright circular vignette rim (the microscope aperture)
    {
        int seg = 220;
        float rimK = (0.9f + 0.6f*rms)*A;
        for(int i=0;i<seg;i++){
            float a0=(float)i/seg*6.2831853f, a1=(float)(i+1)/seg*6.2831853f;
            lineAdd(fb,W,H, cx+cosf(a0)*R, cy+sinf(a0)*R,
                            cx+cosf(a1)*R, cy+sinf(a1)*R, 0.55f,0.85f,0.80f, rimK);
            lineAdd(fb,W,H, cx+cosf(a0)*(R-1.5f), cy+sinf(a0)*(R-1.5f),
                            cx+cosf(a1)*(R-1.5f), cy+sinf(a1)*(R-1.5f), 0.35f,0.60f,0.58f, rimK*0.6f);
        }
        // soft inner haze just inside the aperture
        for(int i=0;i<48;i++){
            float a=(float)i/48*6.2831853f;
            cv->add_glow(cv, cx+cosf(a)*(R-6.f), cy+sinf(a)*(R-6.f),
                         0.25f,0.55f,0.55f, 0.08f*A, 22.f);
        }
    }

    // number of active specks driven by bands / overall energy
    float energy = clampf(0.35f + rms*0.8f + bass*0.5f, 0.f, 1.f);

    for(size_t idx=0; idx<s->mic.size(); ++idx){
        Microbe& m = s->mic[idx];
        uint32_t sd = m.seed;

        // Lissajous swim path inside the field (analytic from time)
        float ph = (float)t*swim;
        float ox = cosf(ph*m.fx*6.2831853f + m.phx);
        float oy = sinf(ph*m.fy*6.2831853f + m.phy);
        // slow secondary drift for organic feel
        ox += 0.18f*sinf((float)t*0.13f + m.phy);
        oy += 0.18f*cosf((float)t*0.11f + m.phx);
        float x = cx + ox * R * m.px * 0.9f;
        float y = cy + oy * R * m.py * 0.9f;

        // keep inside the aperture
        float dcx=x-cx, dcy=y-cy, dd=sqrtf(dcx*dcx+dcy*dcy);
        if(dd > R-4.f){ float sc=(R-4.f)/dd; x=cx+dcx*sc; y=cy+dcy*sc; }

        float focus = smoothstep(0.0f,1.0f, m.depth);          // near = sharp
        // depth-of-field: throbbing size + focus scaling
        float rad = m.base*(0.7f+0.6f*focus) * throb * (0.9f+0.25f*sinf((float)t*0.8f+sd*0.02f));

        // body palette: translucent teal/green/cyan
        float bh = 0.47f + m.hue + chue;    // ~teal-green base hue
        float br,bg,bb; cv->hsv(bh, 0.75f, 1.f, &br,&bg,&bb);
        // nucleus: magenta / violet, brighter with rms (membrane glow)
        float nh = 0.86f + (centroid-0.5f)*0.10f;
        float nr,ng,nb; cv->hsv(nh, 0.85f, 1.f, &nr,&ng,&nb);
        float nk = 1.0f + 0.9f*rms;
        // rim highlight: yellow-green
        float hr,hg,hb; cv->hsv(0.22f, 0.9f, 1.f, &hr,&hg,&hb);

        float k = (0.9f + 0.5f*rms) * A;

        // orientation: velocity direction (derivative of path) -> body axis
        float vx = -sinf(ph*m.fx*6.2831853f + m.phx);
        float vy =  cosf(ph*m.fy*6.2831853f + m.phy);
        float vl = sqrtf(vx*vx+vy*vy)+1e-3f; vx/=vl; vy/=vl;
        float ang = atan2f(vy,vx) + 0.4f*sinf((float)t*0.5f+m.orient);
        float ca=cosf(ang), sa=sinf(ang);

        if(m.type == SPECK){
            // tiny darting speck: quick jitter, appears/vanishes with band energy
            uint32_t bandSel = (p->bandCount>0) ? (uint32_t)(sd % (uint32_t)p->bandCount) : 0u;
            float be = (p->bandCount>0) ? p->bands[bandSel] : rms;
            float vis = smoothstep(0.15f, 0.5f, be*0.6f + energy*0.5f);
            if(vis <= 0.02f) continue;
            float jitter = 3.f*bass;
            float jx = x + jitter*sinf((float)t*11.f + sd*0.05f);
            float jy = y + jitter*cosf((float)t*13.f + sd*0.07f);
            cv->add_glow(cv, jx, jy, br,bg,bb, k*0.6f*vis, rad*2.2f);
            cv->add_glow(cv, jx, jy, 0.8f,1.0f,0.9f, k*0.5f*vis, rad);
            continue;
        }

        if(m.type == COCCUS){
            // round cocci in a chain / cluster, some dividing on the beat
            int nchain = m.chain;
            for(int c=0;c<nchain;c++){
                float t2 = (nchain>1)?((float)c/(nchain-1)-0.5f):0.f;
                float gap = rad*1.7f;
                float gx = x + ca*t2*gap*nchain*0.5f;
                float gy = y + sa*t2*gap*nchain*0.5f;
                // division: pinch into two near the end of the cycle
                float split = 0.f, sdir = 0.f;
                if(m.divRate > 0.f && c==0){
                    float u = fmodf((float)t*m.divRate + m.divPh + 0.2f*beat, 1.f);
                    split = smoothstep(0.62f, 1.f, u) * rad*1.3f;
                    sdir  = 1.f;
                }
                if(split > 0.4f){
                    drawCell(cv,fb,W,H, gx-sa*split, gy+ca*split, rad*0.92f,
                             br,bg,bb, nr*nk,ng*nk,nb*nk, hr,hg,hb, k, focus, (float)t, sd+c*13);
                    drawCell(cv,fb,W,H, gx+sa*split, gy-ca*split, rad*0.92f,
                             br,bg,bb, nr*nk,ng*nk,nb*nk, hr,hg,hb, k, focus, (float)t, sd+c*29);
                } else {
                    drawCell(cv,fb,W,H, gx, gy, rad,
                             br,bg,bb, nr*nk,ng*nk,nb*nk, hr,hg,hb, k, focus, (float)t, sd+c*7);
                }
            }
            continue;
        }

        if(m.type == BACILLUS){
            // rod / capsule body: a run of overlapping glows along the axis,
            // plus a wiggling flagellum tail; may divide (pinch in the middle).
            float bodyLen = rad*3.2f;
            float divU = (m.divRate>0.f) ? fmodf((float)t*m.divRate+m.divPh,1.f) : 0.f;
            float pinch = (m.divRate>0.f) ? smoothstep(0.6f,1.f,divU) : 0.f; // waist thinning
            int nseg = 7;
            for(int c=0;c<=nseg;c++){
                float u = (float)c/nseg - 0.5f;             // -0.5..0.5
                float bx = x + ca*u*bodyLen;
                float by = y + sa*u*bodyLen;
                // capsule radius profile (rounded ends), waist thins on division
                float prof = sqrtf(clampf(1.f-4.f*u*u,0.f,1.f));
                float waist = 1.f - pinch*expf(-(u*u)*40.f)*0.85f;
                float rr = rad*(0.55f+0.45f*prof)*waist;
                cv->add_glow(cv, bx,by, br,bg,bb, k*0.30f, rr*1.7f);
                cv->add_glow(cv, bx,by, br,bg,bb, k*0.40f, rr*1.0f);
            }
            // nucleus streak near centre (violet)
            cv->add_glow(cv, x, y, nr*nk,ng*nk,nb*nk, k*0.6f*(0.55f+0.45f*focus), rad*0.7f);
            // membrane rim highlight along the capsule sides
            if(focus>0.4f){
                float rk = k*0.30f*smoothstep(0.4f,0.85f,focus);
                float pxn = -sa, pyn = ca; // perpendicular
                float endx=x+ca*bodyLen*0.5f, endy=y+sa*bodyLen*0.5f;
                float sx0=x-ca*bodyLen*0.5f, sy0=y-sa*bodyLen*0.5f;
                lineAdd(fb,W,H, sx0+pxn*rad*0.55f,sy0+pyn*rad*0.55f,
                                endx+pxn*rad*0.55f,endy+pyn*rad*0.55f, hr,hg,hb, rk);
                lineAdd(fb,W,H, sx0-pxn*rad*0.55f,sy0-pyn*rad*0.55f,
                                endx-pxn*rad*0.55f,endy-pyn*rad*0.55f, hr,hg,hb, rk);
            }
            // flagellum: wiggling tail from the rear end (treble drives amplitude)
            float rex = x - ca*bodyLen*0.55f;
            float rey = y - sa*bodyLen*0.55f;
            int tn = 12;
            float amp = rad*(0.5f + 2.6f*tre);
            float tlen = bodyLen*1.6f;
            float px=-sa, py=ca;
            float ppx=rex, ppy=rey;
            for(int c=1;c<=tn;c++){
                float u=(float)c/tn;
                float w = sinf(u*6.0f*m.wigF*0.25f + (float)t*(6.f+8.f*tre) + m.wigP)*amp*u;
                float bxp = rex - ca*tlen*u + px*w;
                float byp = rey - sa*tlen*u + py*w;
                lineAdd(fb,W,H, ppx,ppy, bxp,byp, br*0.7f,bg*0.9f,bb*0.8f, k*0.28f*(1.f-0.5f*u));
                ppx=bxp; ppy=byp;
            }
            continue;
        }

        // AMOEBA: blobby morphing membrane from several drifting lobes
        {
            int lobes = 6;
            float nk2 = k*(0.55f+0.45f*focus);
            // overall soft body
            cv->add_glow(cv, x, y, br,bg,bb, k*0.16f, rad*1.9f);
            for(int c=0;c<lobes;c++){
                float a = (float)c/lobes*6.2831853f;
                float morph = 0.55f + 0.45f*sinf((float)t*(0.6f+0.15f*c) + a*2.f + sd*0.01f);
                float lx = x + cosf(a)*rad*morph;
                float ly = y + sinf(a)*rad*morph;
                cv->add_glow(cv, lx, ly, br,bg,bb, k*0.24f, rad*0.9f);
            }
            // wandering violet nucleus
            float nx = x + cosf((float)t*0.5f+sd*0.02f)*rad*0.3f;
            float ny = y + sinf((float)t*0.43f+sd*0.03f)*rad*0.3f;
            cv->add_glow(cv, nx, ny, nr*nk,ng*nk,nb*nk, nk2*0.8f, rad*0.5f);
            // morphing membrane outline (yellow-green rim)
            if(focus>0.3f){
                int seg=22; float rk=k*0.26f*smoothstep(0.3f,0.8f,focus);
                float prevx=0,prevy=0;
                for(int i=0;i<=seg;i++){
                    float a=(float)i/seg*6.2831853f;
                    float rr = rad*(0.85f + 0.30f*sinf(a*3.f + (float)t*1.1f + sd*0.02f)
                                          + 0.15f*sinf(a*5.f - (float)t*0.7f));
                    float mx=x+cosf(a)*rr, my=y+sinf(a)*rr;
                    if(i>0) lineAdd(fb,W,H, prevx,prevy, mx,my, hr,hg,hb, rk);
                    prevx=mx; prevy=my;
                }
            }
        }
    }

    // faint drifting out-of-focus motes for depth (few, cheap)
    for(int i=0;i<10;i++){
        uint32_t sd=hashu((uint32_t)i*40503u+3u);
        float a = hashf(sd,1)*6.2831853f + (float)t*0.05f*(0.5f+hashf(sd,2));
        float rr = R*(0.2f+0.7f*hashf(sd,3));
        float mx = cx + cosf(a)*rr + 20.f*sinf((float)t*0.07f+sd*0.01f);
        float my = cy + sinf(a)*rr + 20.f*cosf((float)t*0.06f+sd*0.02f);
        cv->add_glow(cv, mx,my, 0.15f,0.45f,0.45f, 0.10f*A*(0.6f+0.6f*rms), 30.f);
    }
}
