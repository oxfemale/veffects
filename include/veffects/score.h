#pragma once
#include <stdint.h>

/* Per-frame audio feature vector written by the analyzer, read by the player.
   60 frames/s × 160 bytes = 9 600 bytes/s ≈ 9.6 KB/s */
typedef struct VE_FrameScore {
    float rms;               /* overall loudness 0-1 */
    float bass;              /* 20-250 Hz energy 0-1 */
    float mid;               /* 250-2000 Hz energy 0-1 */
    float high;              /* 2000-8000 Hz energy 0-1 */
    float onset;             /* 1 on beat onset, else 0 */
    float spectral_centroid; /* normalised 0-1 */
    float band[16];          /* 16-band spectrum 0-1 */
    uint32_t frame_index;    /* monotonic counter */
    float tempo_hz;          /* estimated BPM/60 */
    float reserved[13];      /* pad to 160 bytes */
} VE_FrameScore;

#define VE_SCORE_MAGIC   0x56455343u
#define VE_SCORE_VERSION 1u

typedef struct VE_ScoreHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t frame_count;
    uint32_t sample_rate;
    uint32_t hop_samples;
    uint32_t pad[3];
} VE_ScoreHeader;
