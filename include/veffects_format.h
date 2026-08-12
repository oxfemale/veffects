// veffects_format.h
// The .veffects file format -- a compact "mathematical score" of visual effects
// extracted from an audio track. The player reads it and draws frames procedurally.
//
// Binary, little-endian.
// A VfxHeader followed by frameCount data frames:
//   each frame = VFX_NSCALARS scalar floats + bandCount spectrum-band floats.
//
// All values are normalized to [0..1] (except bpm/duration in the header).

#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>

static const char     VFX_MAGIC[4] = {'V','F','X','1'};
static const uint32_t VFX_VERSION  = 1;

#pragma pack(push, 1)
struct VfxHeader {
    char     magic[4];      // "VFX1"
    uint32_t version;       // 1
    uint32_t fps;           // data frame rate (usually 60)
    uint32_t frameCount;    // number of data frames
    uint32_t bandCount;     // spectrum bands per frame (usually 32)
    uint32_t sampleRate;    // source audio sample rate
    float    duration;      // track length, seconds
    float    bpm;           // tempo estimate (0 if unknown)
    uint32_t reserved[6];   // reserved for future use
};
#pragma pack(pop)

// Scalar parameters of one frame (indices into the scalars array).
enum VfxScalar {
    VFX_RMS      = 0,  // overall loudness (smoothed)
    VFX_BASS     = 1,  // low-band energy   (~40-160 Hz)
    VFX_MID      = 2,  // mid-band energy   (~160-2000 Hz)
    VFX_TREBLE   = 3,  // high-band energy  (~2-16 kHz)
    VFX_CENTROID = 4,  // spectral centroid ("brightness" of timbre)
    VFX_FLUX     = 5,  // spectral flux     (rate of spectral change)
    VFX_BEAT     = 6,  // onset/beat strength for this frame (peaks = beats)
    VFX_LOUD     = 7,  // peak loudness (fast, unsmoothed)
    VFX_NSCALARS = 8
};

struct VfxData {
    VfxHeader header;
    std::vector<float> scalars;   // frameCount * VFX_NSCALARS
    std::vector<float> bands;     // frameCount * bandCount

    float scalarAt(uint32_t frame, VfxScalar s) const {
        if (frame >= header.frameCount) frame = header.frameCount ? header.frameCount - 1 : 0;
        return scalars[(size_t)frame * VFX_NSCALARS + s];
    }
    float bandAt(uint32_t frame, uint32_t b) const {
        if (frame >= header.frameCount) frame = header.frameCount ? header.frameCount - 1 : 0;
        return bands[(size_t)frame * header.bandCount + b];
    }
    // Linear interpolation of a scalar at an arbitrary time t (seconds).
    float scalarLerp(double t, VfxScalar s) const {
        double f = t * header.fps;
        if (f < 0) f = 0;
        uint32_t i0 = (uint32_t)f;
        uint32_t i1 = i0 + 1;
        float a = (float)(f - i0);
        return scalarAt(i0, s) * (1.f - a) + scalarAt(i1, s) * a;
    }
    float bandLerp(double t, uint32_t b) const {
        double f = t * header.fps;
        if (f < 0) f = 0;
        uint32_t i0 = (uint32_t)f;
        uint32_t i1 = i0 + 1;
        float a = (float)(f - i0);
        return bandAt(i0, b) * (1.f - a) + bandAt(i1, b) * a;
    }
};

inline bool vfxSave(const std::string& path, const VfxData& d) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = fwrite(&d.header, sizeof(VfxHeader), 1, f) == 1;
    for (uint32_t i = 0; ok && i < d.header.frameCount; i++) {
        ok = fwrite(&d.scalars[(size_t)i * VFX_NSCALARS], sizeof(float), VFX_NSCALARS, f) == VFX_NSCALARS
          && fwrite(&d.bands[(size_t)i * d.header.bandCount], sizeof(float), d.header.bandCount, f) == d.header.bandCount;
    }
    fclose(f);
    return ok;
}

inline bool vfxLoad(const std::string& path, VfxData& d) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    if (fread(&d.header, sizeof(VfxHeader), 1, f) != 1) { fclose(f); return false; }
    if (memcmp(d.header.magic, VFX_MAGIC, 4) != 0 || d.header.version != VFX_VERSION) { fclose(f); return false; }
    uint32_t n = d.header.frameCount, nb = d.header.bandCount;
    d.scalars.resize((size_t)n * VFX_NSCALARS);
    d.bands.resize((size_t)n * nb);
    bool ok = true;
    for (uint32_t i = 0; ok && i < n; i++) {
        ok = fread(&d.scalars[(size_t)i * VFX_NSCALARS], sizeof(float), VFX_NSCALARS, f) == VFX_NSCALARS
          && fread(&d.bands[(size_t)i * nb], sizeof(float), nb, f) == nb;
    }
    fclose(f);
    return ok;
}
