#pragma once

#include <cmath>

static inline void ve_fft(float* real, float* imag, int N) {
    const float ve_pi = 3.14159265358979323846f;
    int j = 0;
    for (int i = 1; i < N; ++i) {
        int bit = N >> 1;
        while (j & bit) {
            j ^= bit;
            bit >>= 1;
        }
        j ^= bit;
        if (i < j) {
            float tr = real[i];
            real[i] = real[j];
            real[j] = tr;
            float ti = imag[i];
            imag[i] = imag[j];
            imag[j] = ti;
        }
    }

    for (int len = 2; len <= N; len <<= 1) {
        const float angle = -2.0f * ve_pi / (float)len;
        const float wlen_r = std::cos(angle);
        const float wlen_i = std::sin(angle);
        for (int i = 0; i < N; i += len) {
            float wr = 1.0f;
            float wi = 0.0f;
            for (int j2 = 0; j2 < len / 2; ++j2) {
                const int u = i + j2;
                const int v = i + j2 + len / 2;
                const float vr = real[v] * wr - imag[v] * wi;
                const float vi = real[v] * wi + imag[v] * wr;
                const float ur = real[u];
                const float ui = imag[u];
                real[u] = ur + vr;
                imag[u] = ui + vi;
                real[v] = ur - vr;
                imag[v] = ui - vi;
                const float next_wr = wr * wlen_r - wi * wlen_i;
                wi = wr * wlen_i + wi * wlen_r;
                wr = next_wr;
            }
        }
    }
}
