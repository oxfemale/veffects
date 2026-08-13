// veffects_analyze.h -- in-process audio decode + analysis producing a VfxData score.
//
// Supported inputs: mp3 (minimp3), wav (dr_wav), flac (dr_flac), midi (tml + a tiny
// built-in synth). Shared by the CLI analyzer (veffects_gen) and the player.
//
// The including translation unit must define MINIMP3_IMPLEMENTATION exactly once
// before including this header:
//
//   #define MINIMP3_IMPLEMENTATION
//   #include "veffects_analyze.h"
//
// vfxAnalyzeFile() decodes the file to PCM, runs a windowed FFT, extracts audio
// features, and fills a VfxData. Optionally returns the decoded PCM for playback.

#pragma once
#define _USE_MATH_DEFINES        // make MSVC's <math.h>/<cmath> expose M_PI
#include "minimp3.h"
#include "minimp3_ex.h"

#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"
#define DR_FLAC_IMPLEMENTATION
#include "dr_flac.h"
#define TML_IMPLEMENTATION
#include "tml.h"

#include "veffects_format.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <vector>
#include <string>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace vfxa {

static const int   VFXA_FPS      = 60;
static const int   VFXA_FFT_SIZE = 2048;
static const int   VFXA_BANDS    = 32;
static const float VFXA_FMIN     = 40.f;
static const float VFXA_FMAX     = 16000.f;

inline void vfxa_fft(std::vector<float>& re, std::vector<float>& im) {
    const int n = (int)re.size();
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
    }
    for (int len = 2; len <= n; len <<= 1) {
        float ang = -2.f * (float)M_PI / len;
        float wr = cosf(ang), wi = sinf(ang);
        for (int i = 0; i < n; i += len) {
            float cr = 1.f, ci = 0.f;
            for (int k = 0; k < len / 2; k++) {
                float ur = re[i+k],          ui = im[i+k];
                float vr = re[i+k+len/2]*cr - im[i+k+len/2]*ci;
                float vi = re[i+k+len/2]*ci + im[i+k+len/2]*cr;
                re[i+k] = ur + vr;  im[i+k] = ui + vi;
                re[i+k+len/2] = ur - vr;  im[i+k+len/2] = ui - vi;
                float ncr = cr*wr - ci*wi;  ci = cr*wi + ci*wr;  cr = ncr;
            }
        }
    }
}

inline float vfxa_percentile(std::vector<float> v, float p) {
    if (v.empty()) return 1.f;
    size_t k = (size_t)(p * (v.size() - 1));
    std::nth_element(v.begin(), v.begin() + k, v.end());
    float x = v[k];
    return x > 1e-9f ? x : 1e-9f;
}

} // namespace vfxa

// Decoded PCM for playback (interleaved 16-bit).
struct VfxAudioPCM {
    std::vector<short> interleaved;
    int hz = 0, ch = 0;
};

// ---------------- decoders ----------------

inline bool vfxDecodeMp3(const std::string& path, VfxAudioPCM& pcm, std::string* err) {
    mp3dec_t mp3d; mp3dec_file_info_t info;
    if (mp3dec_load(&mp3d, path.c_str(), &info, NULL, NULL)) {
        if (err) *err = "cannot decode mp3 " + path; return false;
    }
    pcm.hz = info.hz; pcm.ch = info.channels;
    pcm.interleaved.assign(info.buffer, info.buffer + info.samples);
    free(info.buffer);
    return true;
}

inline bool vfxDecodeWav(const std::string& path, VfxAudioPCM& pcm, std::string* err) {
    unsigned int ch = 0, sr = 0; drwav_uint64 frames = 0;
    drwav_int16* d = drwav_open_file_and_read_pcm_frames_s16(path.c_str(), &ch, &sr, &frames, NULL);
    if (!d) { if (err) *err = "cannot decode wav " + path; return false; }
    pcm.hz = (int)sr; pcm.ch = (int)ch;
    pcm.interleaved.assign(d, d + (size_t)frames * ch);
    drwav_free(d, NULL);
    return true;
}

inline bool vfxDecodeFlac(const std::string& path, VfxAudioPCM& pcm, std::string* err) {
    unsigned int ch = 0, sr = 0; drflac_uint64 frames = 0;
    drflac_int16* d = drflac_open_file_and_read_pcm_frames_s16(path.c_str(), &ch, &sr, &frames, NULL);
    if (!d) { if (err) *err = "cannot decode flac " + path; return false; }
    pcm.hz = (int)sr; pcm.ch = (int)ch;
    pcm.interleaved.assign(d, d + (size_t)frames * ch);
    drflac_free(d, NULL);
    return true;
}

// MIDI: parse events with tml, then render with a tiny built-in synth (simple
// oscillators + envelopes, channel 9 = noise percussion). No soundfont needed;
// this is a chiptune-ish rendition, enough to play and to drive the visuals.
inline bool vfxDecodeMidi(const std::string& path, VfxAudioPCM& pcm, std::string* err) {
    tml_message* midi = tml_load_filename(path.c_str());
    if (!midi) { if (err) *err = "cannot load midi " + path; return false; }
    const int SR = 44100;
    double lastMs = 0;
    for (tml_message* m = midi; m; m = m->next) if ((double)m->time > lastMs) lastMs = (double)m->time;
    double durMs = lastMs + 1500.0;
    if (durMs > 20.0 * 60.0 * 1000.0) durMs = 20.0 * 60.0 * 1000.0;
    long total = (long)(durMs / 1000.0 * SR);
    if (total < SR) total = SR;

    std::vector<float> buf((size_t)total, 0.f);
    const int MAXCH = 16, MAXKEY = 128;
    std::vector<int>   startS((size_t)MAXCH * MAXKEY, -1);
    std::vector<float> startV((size_t)MAXCH * MAXKEY, 0.f);

    auto renderNote = [&](int st, int en, int key, float vel, int ch) {
        if (st < 0) return;
        if (en <= st) en = st + SR / 20;
        float f = 440.f * powf(2.f, (key - 69) / 12.f);
        int rel = SR / 8, att = SR / 300, len = en - st;
        bool drum = (ch == 9);
        for (int i = 0; i < len + rel; i++) {
            long idx = (long)st + i; if (idx < 0) continue; if (idx >= total) break;
            float env;
            if (drum) { int d = SR / 12; if (i >= d) break; env = 1.f - (float)i / d; }
            else if (i < att) env = (float)i / att;
            else if (i < len) env = 0.75f;
            else env = 0.75f * (1.f - (float)(i - len) / rel);
            float s;
            if (drum) {
                unsigned h = (unsigned)(idx * 1103515245u + 12345u);
                s = ((h >> 9) & 0xffff) / 32767.f - 1.f;
            } else {
                float ph = f * ((float)i / SR);
                float saw = 2.f * (ph - floorf(ph)) - 1.f;
                s = 0.7f * sinf(6.2831853f * ph) + 0.3f * saw;
            }
            buf[(size_t)idx] += s * env * vel * 0.22f;
        }
    };

    for (tml_message* m = midi; m; m = m->next) {
        int ch = m->channel & 15, key = m->key & 127;
        int s = (int)((double)m->time / 1000.0 * SR);
        if (m->type == TML_NOTE_ON && m->velocity > 0) {
            startS[ch * MAXKEY + key] = s; startV[ch * MAXKEY + key] = m->velocity / 127.f;
        } else if (m->type == TML_NOTE_OFF || (m->type == TML_NOTE_ON && m->velocity == 0)) {
            int st = startS[ch * MAXKEY + key];
            if (st >= 0) { renderNote(st, s, key, startV[ch * MAXKEY + key], ch); startS[ch * MAXKEY + key] = -1; }
        }
    }
    for (int c = 0; c < MAXCH; c++) for (int k = 0; k < MAXKEY; k++) {
        int st = startS[c * MAXKEY + k];
        if (st >= 0) renderNote(st, (int)total, k, startV[c * MAXKEY + k], c);
    }
    tml_free(midi);

    pcm.hz = SR; pcm.ch = 1; pcm.interleaved.resize((size_t)total);
    for (long i = 0; i < total; i++) {
        float v = buf[(size_t)i]; v = v < -1 ? -1 : (v > 1 ? 1 : v);
        pcm.interleaved[(size_t)i] = (short)(v * 32767);
    }
    return true;
}

inline bool vfxHasExt(const std::string& s, const char* ext) {
    size_t n = strlen(ext);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; i++)
        if (tolower((unsigned char)s[s.size()-n+i]) != (unsigned char)ext[i]) return false;
    return true;
}

// Decode any supported audio file into PCM (dispatch by extension).
inline bool vfxDecodeFile(const std::string& path, VfxAudioPCM& pcm, std::string* err) {
    if (vfxHasExt(path, ".wav"))  return vfxDecodeWav(path, pcm, err);
    if (vfxHasExt(path, ".flac")) return vfxDecodeFlac(path, pcm, err);
    if (vfxHasExt(path, ".mid") || vfxHasExt(path, ".midi")) return vfxDecodeMidi(path, pcm, err);
    return vfxDecodeMp3(path, pcm, err);   // default: mp3
}

// ---------------- analysis ----------------

inline bool vfxAnalyzePCM(const VfxAudioPCM& pcm, VfxData& out) {
    using namespace vfxa;
    const int sr = pcm.hz, ch = pcm.ch ? pcm.ch : 1;
    const size_t nSamples = pcm.interleaved.size() / (size_t)ch;
    if (nSamples == 0 || sr == 0) return false;

    std::vector<float> mono(nSamples);
    for (size_t i = 0; i < nSamples; i++) {
        int acc = 0;
        for (int c = 0; c < ch; c++) acc += pcm.interleaved[i * ch + c];
        mono[i] = (float)acc / (ch * 32768.f);
    }

    const double hop = (double)sr / VFXA_FPS;
    const uint32_t frames = (uint32_t)(nSamples / hop);
    const int FFT_SIZE = VFXA_FFT_SIZE, BANDS = VFXA_BANDS;

    std::vector<float> hann(FFT_SIZE);
    for (int i = 0; i < FFT_SIZE; i++)
        hann[i] = 0.5f * (1.f - cosf(2.f * (float)M_PI * i / (FFT_SIZE - 1)));

    std::vector<int> bandEdge(BANDS + 1);
    for (int b = 0; b <= BANDS; b++) {
        float f = VFXA_FMIN * powf(VFXA_FMAX / VFXA_FMIN, (float)b / BANDS);
        int bin = (int)(f * FFT_SIZE / sr);
        bandEdge[b] = std::min(std::max(bin, 1), FFT_SIZE / 2 - 1);
    }
    for (int b = 1; b <= BANDS; b++)
        if (bandEdge[b] <= bandEdge[b-1]) bandEdge[b] = bandEdge[b-1] + 1;

    auto hz2bin = [&](float f){ return std::min(std::max((int)(f * FFT_SIZE / sr), 1), FFT_SIZE/2 - 1); };
    const int bBass0 = hz2bin(40),   bBass1 = hz2bin(160);
    const int bMid0  = hz2bin(160),  bMid1  = hz2bin(2000);
    const int bTre0  = hz2bin(2000), bTre1  = hz2bin(16000);

    std::vector<float> re(FFT_SIZE), im(FFT_SIZE);
    std::vector<float> mag(FFT_SIZE / 2), prevMag(FFT_SIZE / 2, 0.f);
    std::vector<float> sRms(frames), sBass(frames), sMid(frames), sTre(frames),
                       sCen(frames), sFlux(frames), sOnset(frames), sLoud(frames);
    std::vector<float> bandsRaw((size_t)frames * BANDS);

    for (uint32_t fi = 0; fi < frames; fi++) {
        size_t start = (size_t)(fi * hop);
        float rms = 0, peak = 0;
        size_t hopN = (size_t)hop;
        for (size_t i = 0; i < hopN && start + i < nSamples; i++) {
            float v = mono[start + i];
            rms += v * v;
            peak = std::max(peak, fabsf(v));
        }
        sRms[fi] = sqrtf(rms / std::max<size_t>(hopN, 1));
        sLoud[fi] = peak;

        long wstart = (long)start - FFT_SIZE / 2;
        for (int i = 0; i < FFT_SIZE; i++) {
            long idx = wstart + i;
            float v = (idx >= 0 && idx < (long)nSamples) ? mono[idx] : 0.f;
            re[i] = v * hann[i];
            im[i] = 0.f;
        }
        vfxa_fft(re, im);
        for (int i = 0; i < FFT_SIZE / 2; i++)
            mag[i] = sqrtf(re[i]*re[i] + im[i]*im[i]);

        auto sumRange = [&](int a, int b){
            float s = 0; for (int i = a; i < b; i++) s += mag[i]*mag[i]; return s;
        };
        sBass[fi] = sqrtf(sumRange(bBass0, bBass1));
        sMid[fi]  = sqrtf(sumRange(bMid0,  bMid1));
        sTre[fi]  = sqrtf(sumRange(bTre0,  bTre1));

        float num = 0, den = 0;
        for (int i = 1; i < FFT_SIZE / 2; i++) { num += i * mag[i]; den += mag[i]; }
        sCen[fi] = den > 1e-9f ? (num / den) * ((float)sr / FFT_SIZE) : 0.f;

        float flux = 0;
        for (int i = 1; i < FFT_SIZE / 2; i++) {
            float dm = mag[i] - prevMag[i];
            if (dm > 0) {
                float freqW = 1.f / (1.f + (float)i / bBass1);
                flux += dm * (0.4f + 1.6f * freqW);
            }
        }
        sFlux[fi] = flux;
        std::swap(mag, prevMag);

        for (int b = 0; b < BANDS; b++) {
            float s = 0;
            for (int i = bandEdge[b]; i < bandEdge[b+1]; i++) s += prevMag[i]*prevMag[i];
            bandsRaw[(size_t)fi * BANDS + b] = sqrtf(s / (bandEdge[b+1] - bandEdge[b]));
        }
    }

    const int Wl = VFXA_FPS / 4;
    for (uint32_t i = 0; i < frames; i++) {
        float mean = 0; int cnt = 0;
        for (int j = -Wl; j <= Wl; j++) {
            long k = (long)i + j;
            if (k >= 0 && k < (long)frames) { mean += sFlux[k]; cnt++; }
        }
        mean /= std::max(cnt, 1);
        float v = sFlux[i] - 1.3f * mean;
        sOnset[i] = v > 0 ? v : 0;
    }
    std::vector<float> onsetPeaks(frames, 0.f);
    for (uint32_t i = 1; i + 1 < frames; i++)
        if (sOnset[i] > sOnset[i-1] && sOnset[i] >= sOnset[i+1])
            onsetPeaks[i] = sOnset[i];

    float bpm = 0;
    {
        int lagMin = (int)(60.0 / 200.0 * VFXA_FPS);
        int lagMax = (int)(60.0 / 60.0  * VFXA_FPS);
        float best = 0; int bestLag = 0;
        for (int lag = lagMin; lag <= lagMax && lag < (int)frames; lag++) {
            double s = 0;
            for (uint32_t i = 0; i + lag < frames; i++) s += (double)sOnset[i] * sOnset[i + lag];
            if (s > best) { best = (float)s; bestLag = lag; }
        }
        if (bestLag > 0) bpm = 60.f * VFXA_FPS / bestLag;
    }

    auto normalize = [&](std::vector<float>& v) {
        float p = vfxa_percentile(v, 0.98f);
        for (float& x : v) x = std::min(x / p, 1.f);
    };
    normalize(sRms); normalize(sBass); normalize(sMid); normalize(sTre);
    normalize(sFlux); normalize(sLoud);
    { float p = vfxa_percentile(onsetPeaks, 0.995f);
      for (float& x : onsetPeaks) x = std::min(x / p, 1.f); }
    for (float& x : sCen) x = std::min(x / 8000.f, 1.f);
    { float p = vfxa_percentile(bandsRaw, 0.98f);
      for (uint32_t fi = 0; fi < frames; fi++)
        for (int b = 0; b < BANDS; b++) {
            float tilt = 1.f + 1.5f * (float)b / BANDS;
            float& x = bandsRaw[(size_t)fi * BANDS + b];
            x = std::min(x * tilt / p, 1.f);
            x = powf(x, 0.6f);
        } }

    memcpy(out.header.magic, VFX_MAGIC, 4);
    out.header.version    = VFX_VERSION;
    out.header.fps        = VFXA_FPS;
    out.header.frameCount = frames;
    out.header.bandCount  = BANDS;
    out.header.sampleRate = sr;
    out.header.duration   = (float)nSamples / sr;
    out.header.bpm        = bpm;
    memset(out.header.reserved, 0, sizeof(out.header.reserved));

    out.scalars.resize((size_t)frames * VFX_NSCALARS);
    for (uint32_t i = 0; i < frames; i++) {
        float* s = &out.scalars[(size_t)i * VFX_NSCALARS];
        s[VFX_RMS] = sRms[i];   s[VFX_BASS] = sBass[i];
        s[VFX_MID] = sMid[i];   s[VFX_TREBLE] = sTre[i];
        s[VFX_CENTROID] = sCen[i]; s[VFX_FLUX] = sFlux[i];
        s[VFX_BEAT] = onsetPeaks[i]; s[VFX_LOUD] = sLoud[i];
    }
    out.bands = std::move(bandsRaw);
    return true;
}

// Decode + analyze a file. If pcm != nullptr, also returns the decoded PCM.
inline bool vfxAnalyzeFile(const std::string& path, VfxData& out,
                           VfxAudioPCM* pcm = nullptr, std::string* err = nullptr) {
    VfxAudioPCM local;
    VfxAudioPCM& p = pcm ? *pcm : local;
    if (!vfxDecodeFile(path, p, err)) return false;
    if (!vfxAnalyzePCM(p, out)) { if (err) *err = "empty/unsupported audio: " + path; return false; }
    return true;
}

// Backward-compatible alias.
inline bool vfxAnalyzeMp3(const std::string& path, VfxData& out,
                          VfxAudioPCM* pcm = nullptr, std::string* err = nullptr) {
    return vfxAnalyzeFile(path, out, pcm, err);
}
