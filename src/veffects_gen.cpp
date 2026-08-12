// veffects_gen -- CLI analyzer: mp3 -> .veffects
//
// Thin wrapper over vfxAnalyzeMp3() (include/veffects_analyze.h), which the player
// also uses to analyze tracks in-process.
//
// Usage:  veffects_gen input.mp3 [output.veffects]

#define MINIMP3_IMPLEMENTATION
#include "veffects_analyze.h"

#include <cstdio>
#include <string>

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: veffects_gen input.mp3 [output.veffects]\n");
        return 1;
    }
    std::string inPath = argv[1];
    std::string outPath = argc > 2 ? argv[2] : [&]{
        std::string s = inPath;
        size_t dot = s.find_last_of('.');
        if (dot != std::string::npos) s = s.substr(0, dot);
        return s + ".veffects";
    }();

    VfxData d;
    std::string err;
    fprintf(stderr, "analyzing: %s\n", inPath.c_str());
    if (!vfxAnalyzeMp3(inPath, d, nullptr, &err)) {
        fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    if (!vfxSave(outPath, d)) {
        fprintf(stderr, "error: cannot write %s\n", outPath.c_str());
        return 1;
    }
    size_t bytes = sizeof(VfxHeader) +
        (size_t)d.header.frameCount * (VFX_NSCALARS + d.header.bandCount) * sizeof(float);
    fprintf(stderr, "written: %s (%.1f MB, %.1f s, %u frames, bpm %.1f)\n",
            outPath.c_str(), bytes / 1e6, d.header.duration, d.header.frameCount, d.header.bpm);
    return 0;
}
