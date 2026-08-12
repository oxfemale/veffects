#pragma once

#include <stddef.h>
#include <stdint.h>

typedef uint64_t drmp3_uint64;

typedef struct drmp3_config {
    uint32_t channels;
    uint32_t sampleRate;
} drmp3_config;

typedef struct drmp3 {
    uint32_t sampleRate;
    uint32_t channels;
    void *userdata;
} drmp3;

static inline int drmp3_init_file(drmp3 *mp3, const char *filename, const void *allocationCallbacks) {
    (void)filename;
    (void)allocationCallbacks;
    if (mp3 == NULL) {
        return 0;
    }
    mp3->sampleRate = 44100;
    mp3->channels = 2;
    mp3->userdata = NULL;
    return 1;
}

static inline drmp3_uint64 drmp3_read_pcm_frames_f32(drmp3 *mp3, drmp3_uint64 framesToRead, float *bufferOut) {
    (void)mp3;
    (void)framesToRead;
    (void)bufferOut;
    return 0;
}

static inline void drmp3_uninit(drmp3 *mp3) {
    (void)mp3;
}
