#pragma once

#include <cmath>
#include <cstring>

static inline float ve_rms(const float* samples, int n) {
    if (samples == NULL || n <= 0) {
        return 0.0f;
    }
    float sum = 0.0f;
    for (int i = 0; i < n; ++i) {
        sum += samples[i] * samples[i];
    }
    return std::sqrt(sum / (float)n);
}

static inline float ve_onset(float prev_rms, float curr_rms, float threshold) {
    return (curr_rms - prev_rms) > threshold ? 1.0f : 0.0f;
}

static inline float ve_spectral_centroid(const float* mag, int bins, float sample_rate) {
    if (mag == NULL || bins <= 0 || sample_rate <= 0.0f) {
        return 0.0f;
    }
    float weighted = 0.0f;
    float total = 0.0f;
    for (int i = 0; i < bins; ++i) {
        const float freq = ((float)i / (float)bins) * (sample_rate * 0.5f);
        weighted += freq * mag[i];
        total += mag[i];
    }
    if (total <= 1e-6f) {
        return 0.0f;
    }
    const float centroid_hz = weighted / total;
    const float nyquist = sample_rate * 0.5f;
    return centroid_hz / (nyquist > 0.0f ? nyquist : 1.0f);
}

static inline void ve_compute_bands(const float* mag, int bins, float* bands, int num_bands) {
    if (bands == NULL || num_bands <= 0) {
        return;
    }
    std::memset(bands, 0, sizeof(float) * (size_t)num_bands);
    if (mag == NULL || bins <= 0) {
        return;
    }
    for (int band = 0; band < num_bands; ++band) {
        const int start = (band * bins) / num_bands;
        const int end = ((band + 1) * bins) / num_bands;
        float sum = 0.0f;
        int count = 0;
        for (int i = start; i < end; ++i) {
            sum += mag[i];
            ++count;
        }
        bands[band] = count > 0 ? sum / (float)count : 0.0f;
    }

    float max_value = 0.0f;
    for (int band = 0; band < num_bands; ++band) {
        if (bands[band] > max_value) {
            max_value = bands[band];
        }
    }
    if (max_value > 1e-6f) {
        for (int band = 0; band < num_bands; ++band) {
            bands[band] /= max_value;
        }
    }
}
