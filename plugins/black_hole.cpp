// black_hole.cpp -- "Black Hole" scene plugin.
// A central event-horizon shadow ringed by a thin bright photon ring, a swirling
// glowing accretion disk with relativistic Doppler brightening (blue/bright on the
// approaching side, red/dim on the receding side) and a hot white-orange inner
// edge, over a gravitationally-lensed starfield (background sampled at a deflected
// radius r_src = r - k/r so space bends near the hole), with infalling matter
// streaks spiraling inward.
//
// All animation is derived from the absolute time p->time, so any frame renders
// correctly without simulation history (offline render and seeking both work).

#include "veffects_plugin.h"
#include <cmath>
#include <cstdint>
#include <vector>

static inline float clampf(float x, float a, float b){ return x<a?a:(x>b?b:x); }
static inline float mixf(float a, float b, float t){ return a+(b-a)*t; }
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
static inline void putAddF(float* fb, int W, int H, float fx, float fy,
                           float r, float g, float b, float k){
    // bilinear-ish soft splat for sub-pixel star / particle placement
    int x=(int)floorf(fx), y=(int)floorf(fy);
    float ax=fx-x, ay=fy-y;
    putAdd(fb,W,H,x,  y,  r,g,b,k*(1-ax)*(1-ay));
    putAdd(fb,W,H,x+1,y,  r,g,b,k*ax*(1-ay));
    putAdd(fb,W,H,x,  y+1,r,g,b,k*(1-ax)*ay);
    putAdd(fb,W,H,x+1,y+1,r,g,b,k*ax*ay);
}

// starfield sample: deterministic stars on a coarse grid, returns colour+brightness
static inline void starField(float sx, float sy, float t, float tre,
                             float& r, float& g, float& b){
    r=g=b=0.f;
    // cell grid; check the cell and neighbours so a star near an edge still splats
    const float CELL = 26.f;
    int cx = (int)floorf(sx/CELL), cy = (int)floorf(sy/CELL);
    for(int oy=-1;oy<=1;oy++)for(int ox=-1;ox<=1;ox++){
        int gx=cx+ox, gy=cy+oy;
        uint32_t h = hashu((uint32_t)(gx*73856093) ^ (uint32_t)(gy*19349663));
        if((h & 15u) < 12u) continue;                 // ~25% of cells hold a star
        float px = (gx + (hashu(h)&0xffff)/65535.f)*CELL;
        float py = (gy + (hashu(h*3+1)&0xffff)/65535.f)*CELL;
        float dx = sx-px, dy = sy-py;
        float d2 = dx*dx+dy*dy;
        float mag = 0.35f + (h>>8 & 0xff)/255.f;       // star magnitude
        float rad = 0.7f + mag*0.7f;                    // small, point-like
        float fall = expf(-d2/(rad*rad));
        if(fall < 0.04f) continue;
        // twinkle with treble
        float tw = 0.6f + 0.4f*sinf(t*(2.f+mag*5.f) + (h&63));
        float bright = mag*mag*(0.22f + 0.5f*tw*(0.5f+1.2f*tre))*fall;
        // faint star colours: bluish-white to warm
        float cc = ((h>>3)&0xff)/255.f;
        r += mixf(0.7f,1.0f,cc)*bright;
        g += mixf(0.85f,0.92f,cc)*bright;
        b += mixf(1.0f,0.8f,cc)*bright;
    }
}

struct State { int W, H; };  // stateless, but create must return non-NULL

static VfxPluginInfo INFO = {
    VFX_PLUGIN_ABI, "Black Hole", "veffects",
    "A gravitationally-lensed starfield warps around a black event-horizon shadow ringed by a bright photon ring and a swirling Doppler-brightened accretion disk with infalling matter streaks."
};
VFX_EXPORT const VfxPluginInfo* vfx_plugin_info(void){ return &INFO; }

VFX_EXPORT void* vfx_plugin_create(int W, int H){
    State* s = new State();
    s->W = W; s->H = H;
    return s;
}
VFX_EXPORT void vfx_plugin_destroy(void* st){ delete (State*)st; }

VFX_EXPORT void vfx_plugin_render(void* st, const VfxCanvas* cv, const VfxParams* p){
    State* s = (State*)st;
    float* fb = cv->fb; int W = cv->width, H = cv->height;
    float A = p->alpha;
    float bass=p->bass, mid=p->mid, tre=p->treble, rms=p->rms;
    float beat=p->beat, onset=p->onset, cen=clampf(p->centroid,0.f,1.f);
    double t=p->time;
    (void)mid;

    float cx = W*0.5f, cy = H*0.5f;

    // ---- geometry of the hole (screen-space pixel radii) --------------------
    float Rs   = 66.f;                       // event-horizon shadow radius
    float Rph  = Rs*1.12f;                    // photon ring radius (hugs shadow)
    // gravitational deflection strength: stronger pull with rms/beat
    float lensK = (Rs*Rs)*(0.95f + 0.55f*rms + 0.25f*beat);
    // disk hot-edge hue shift with spectral centroid
    float hueShift = (cen-0.5f)*0.10f;
    // disk spin speed + brightness with bass/beat
    float spin = 0.7f + 2.2f*bass + 1.1f*beat;
    float diskGain = 0.85f + 1.3f*bass + 0.7f*beat;

    // disk inclination: we view it nearly edge-on, foreshortened vertically.
    const float INC = 0.34f;                 // vertical squash of the disk ellipse
    float Rin  = Rs*1.25f;                    // inner disk edge (just outside photon ring)
    float Rout = Rs*3.6f;                     // outer disk edge

    // =========================================================================
    // PASS 1 : gravitationally-lensed starfield + faint indigo space (per-pixel)
    // Step by 1 over the whole frame (~307k px). For each screen point at radius
    // r from the centre we sample the background at a deflected radius
    // r_src = r - lensK/r, so stars near the hole are bent inward and smeared
    // into arcs. Inside the shadow -> black.
    // =========================================================================
    for(int y=0;y<H;y++){
        float dy = y - cy;
        for(int x=0;x<W;x++){
            float dx = x - cx;
            float r = sqrtf(dx*dx+dy*dy) + 1e-3f;
            size_t idx = ((size_t)y*W + x)*3;

            // faint deep-space indigo gradient (very dark)
            float bgm = 0.010f + 0.014f*(y/(float)H);
            float br=0.020f*bgm/0.014f, bgc=0.018f, bb=0.045f;
            // cheap: just a small constant indigo tint
            fb[idx+0] += 0.006f*A;
            fb[idx+1] += 0.006f*A;
            fb[idx+2] += 0.016f*A;
            (void)br;(void)bgc;(void)bb;(void)bgm;

            if(r <= Rs*0.985f) continue;             // inside shadow: pure black

            // deflection: pull sample radius inward near the hole
            float r_src = r - lensK/r;
            if(r_src < 1.0f) r_src = 1.0f;            // clamp near-singular
            float sc = r_src/r;
            float ssx = cx + dx*sc;
            float ssy = cy + dy*sc;

            // extra tangential smear near the ring: shift sample along the arc
            float bend = clampf((Rph*2.2f - r)/ (Rph*2.2f), 0.f, 1.f);
            if(bend>0.f){
                float ang = atan2f(dy,dx);
                float tang = bend*bend*0.6f;          // arc smear amount
                ssx += -sinf(ang)*tang*18.f;
                ssy +=  cosf(ang)*tang*18.f;
            }

            float sr,sg,sb;
            starField(ssx, ssy, (float)t, tre, sr,sg,sb);
            if(sr+sg+sb > 0.f){
                // magnification ring: brighten lensed stars just outside shadow
                float mgn = 1.f + 1.6f*bend;
                fb[idx+0] += sr*mgn*A;
                fb[idx+1] += sg*mgn*A;
                fb[idx+2] += sb*mgn*A;
            }
        }
    }

    // =========================================================================
    // PASS 2 : ACCRETION DISK (analytic per-pixel over its bounding box).
    // Model an inclined annulus. For a screen point, undo the vertical squash to
    // get disk-plane radius rd and azimuth phi; brightness = radial profile *
    // swirling turbulence * Doppler factor. Doppler: approaching side (left)
    // brighter & bluer, receding side (right) dimmer & redder.
    // =========================================================================
    {
        int bx0 = (int)(cx - Rout - 6), bx1 = (int)(cx + Rout + 6);
        int by0 = (int)(cy - Rout*INC - 22), by1 = (int)(cy + Rout*INC + 22);
        bx0=clampf(bx0,0,W-1); bx1=clampf(bx1,0,W-1);
        by0=clampf(by0,0,H-1); by1=clampf(by1,0,H-1);
        int NB = p->bandCount;

        for(int y=by0;y<=by1;y++){
            float dy = y - cy;
            for(int x=bx0;x<=bx1;x++){
                float dx = x - cx;
                // disk-plane coordinates: un-squash vertical
                float pz = dy / INC;                   // depth in disk plane
                float rd = sqrtf(dx*dx + pz*pz);
                if(rd < Rin || rd > Rout) continue;
                float phi = atan2f(pz, dx);

                // radial brightness profile: hot inner edge falling outward
                float u = (rd - Rin)/(Rout - Rin);     // 0 inner .. 1 outer
                float radProf = powf(1.f-u, 1.7f);     // bright inner, fades out
                radProf += 0.35f*expf(-u*u*22.f);      // extra-hot inner lip

                // swirling gas turbulence: spiral arms rotating with time
                float arms = 0.55f + 0.45f*sinf(phi*3.f - (float)t*spin + rd*0.05f);
                float fine = 0.6f + 0.4f*sinf(phi*7.f - (float)t*spin*1.6f + rd*0.11f
                                              + sinf((float)t*0.7f)*2.f);
                float turb = arms*fine;
                // p->bands modulate the arc density
                if(NB>0){
                    int bi = (int)((phi/6.28318f + 0.5f)*NB) % NB;
                    if(bi<0) bi+=NB;
                    turb *= (0.7f + 0.9f*p->bands[bi]);
                }

                // Doppler: velocity is tangential; the left limb (-x, moving toward
                // viewer given our spin sense) is beamed brighter & blue-shifted.
                // approach factor from the x-component of orbital velocity dir.
                float vdir = -sinf(phi);               // tangential x-ish sign
                float dopp = vdir;                      // -1 receding .. +1 approaching
                float beam = powf(clampf(0.5f + 0.82f*dopp, 0.f, 1.f), 2.0f); // 0..1
                float bright = (0.22f + 2.1f*beam);    // relativistic beaming

                float k = radProf*turb*bright*diskGain;
                if(k <= 0.001f) continue;

                // colour: hot white-orange inner -> cooler orange/red outer;
                // Doppler tints blue (approach) vs deep red (recede).
                // base hue around orange, shifted by centroid.
                float hueBase = 0.055f + hueShift;     // orange
                // inner lip pushes toward white/yellow
                float whiteMix = clampf(radProf*1.1f, 0.f, 1.f);
                float rr,gg,bb;
                cv->hsv(hueBase + (1.f-u)*0.02f, mixf(1.0f,0.35f,whiteMix),
                        1.0f, &rr,&gg,&bb);
                // toward white at hot inner edge
                rr = mixf(rr,1.0f,whiteMix*0.85f);
                gg = mixf(gg,0.95f,whiteMix*0.85f);
                bb = mixf(bb,0.85f,whiteMix*0.7f);
                // Doppler colour: approach -> add blue, recede -> deepen red
                float dblue = clampf(dopp,0.f,1.f);
                float dred  = clampf(-dopp,0.f,1.f);
                rr += 0.10f*dblue; gg += 0.22f*dblue; bb += 0.75f*dblue;
                gg *= (1.f-0.45f*dred); bb *= (1.f-0.65f*dred);
                rr *= (1.f+0.25f*dred);

                // occlusion: the near half of the far-side disk passes BEHIND the
                // shadow. Points with pz>0 (front) draw over; points behind the
                // hole within shadow radius are hidden.
                if(pz > 0.f){
                    // front arc: always visible
                } else {
                    // back arc: hidden where it projects onto the shadow disk
                    float pr = sqrtf(dx*dx+dy*dy);
                    if(pr < Rs*0.99f) continue;
                }

                putAdd(fb,W,H,x,y, rr,gg,bb, k*0.24f*A);
            }
        }
    }

    // =========================================================================
    // PASS 3 : PHOTON RING -- thin bright ring hugging the shadow, flares on beat.
    // =========================================================================
    {
        float ringK = 1.1f + 3.0f*beat + 0.7f*onset;
        int STEPS = 512;
        for(int i=0;i<STEPS;i++){
            float a = (float)i/STEPS*6.28318f;
            float ca=cosf(a), sa=sinf(a);
            // slight Doppler on the ring too (left brighter/bluer)
            float dop = clampf(0.5f - 0.5f*sinf(a), 0.f, 1.f);
            float k = (0.22f + 0.30f*dop)*ringK*A;
            // hot white-blue photon ring, thin with inner/outer feather
            putAddF(fb,W,H, cx+ca*Rph,        cy+sa*Rph,        0.95f,0.97f,1.0f, k);
            putAddF(fb,W,H, cx+ca*(Rph-1.1f), cy+sa*(Rph-1.1f), 0.85f,0.9f,1.0f,  k*0.5f);
            putAddF(fb,W,H, cx+ca*(Rph+1.1f), cy+sa*(Rph+1.1f), 0.75f,0.82f,1.0f, k*0.5f);
            // sparse soft flares riding the ring (do NOT flood the shadow centre)
            if((i & 31)==0)
                cv->add_glow(cv, cx+ca*Rph, cy+sa*Rph, 0.7f,0.82f,1.0f,
                             (0.05f+0.35f*beat)*(0.4f+0.8f*dop)*A, 5.f);
        }
    }

    // =========================================================================
    // PASS 4 : INFALLING MATTER STREAKS spiraling inward. Deterministic from t.
    // Each streak follows an inward spiral; sparks/flares brighten on onset.
    // =========================================================================
    {
        int NS = 26;
        float flare = clampf(onset*1.3f + beat*0.4f, 0.f, 1.f);
        for(int i=0;i<NS;i++){
            float ph = hashf(i,3);
            float spd = 0.10f + hashf(i,4)*0.14f;
            // u: 0 (far) -> 1 (swallowed), loops
            float u = fmodf(ph + (float)t*spd*(0.6f+1.4f*bass), 1.f);
            float a0 = hashf(i,5)*6.28318f;
            float turns = 2.2f + hashf(i,6)*2.0f;
            float rad = mixf(Rout*1.5f, Rin*0.9f, u);     // spirals inward
            float ang = a0 + turns*u*6.28318f + (float)t*spin*0.5f;
            // fade in at start, brighten as it falls in, vanish at horizon
            float av = clampf(u*5.f,0.f,1.f) * clampf((1.f-u)*3.f,0.f,1.f);
            if(av<=0.f) continue;
            float ca=cosf(ang), sa=sinf(ang);
            float px = cx + ca*rad, py = cy + sa*rad*INC;   // inclined like the disk
            // hot spark colour: orange-white, bluer as it accelerates inward
            float heat = u;
            float rr = mixf(1.0f,0.7f,heat*0.3f);
            float gg = mixf(0.5f,0.75f,heat);
            float bb = mixf(0.15f,1.0f,heat*heat);
            float k = (0.25f + 1.6f*heat + 2.2f*flare*hashf(i,7))*av*A;
            // draw a short motion-trail along the spiral (a few inner samples)
            for(int q=0;q<5;q++){
                float uu = clampf(u + q*0.010f, 0.f, 1.f);
                float rr2 = mixf(Rout*1.5f, Rin*0.9f, uu);
                float an2 = a0 + turns*uu*6.28318f + (float)t*spin*0.5f;
                float tx = cx + cosf(an2)*rr2, ty = cy + sinf(an2)*rr2*INC;
                putAddF(fb,W,H, tx,ty, rr,gg,bb, k*(1.f-q*0.18f)*0.5f);
            }
            putAddF(fb,W,H, px,py, rr,gg,bb, k);
            if(flare>0.3f && hashf(i,8) < flare)
                cv->add_glow(cv, px,py, rr,gg,bb, k*0.4f, 3.f+4.f*flare);
        }
    }
}
