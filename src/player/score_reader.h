#pragma once

#include <cstdio>
#include <string>
#include <vector>

#include "veffects/score.h"

class ScoreReader {
public:
    bool load(const char* path) {
        frames_.clear();
        header_ = {};
        FILE* file = std::fopen(path, "rb");
        if (!file) {
            return false;
        }
        const bool ok = std::fread(&header_, sizeof(header_), 1, file) == 1 &&
                        header_.magic == VE_SCORE_MAGIC &&
                        header_.version == VE_SCORE_VERSION;
        if (!ok) {
            std::fclose(file);
            frames_.clear();
            return false;
        }
        frames_.resize(header_.frame_count);
        if (!frames_.empty()) {
            const std::size_t read_count = std::fread(frames_.data(), sizeof(VE_FrameScore), frames_.size(), file);
            if (read_count != frames_.size()) {
                std::fclose(file);
                frames_.clear();
                return false;
            }
        }
        std::fclose(file);
        return true;
    }

    const VE_ScoreHeader& header() const { return header_; }
    const std::vector<VE_FrameScore>& frames() const { return frames_; }
    const VE_FrameScore* frame_at(std::size_t index) const {
        return index < frames_.size() ? &frames_[index] : nullptr;
    }
    std::size_t size() const { return frames_.size(); }
    bool empty() const { return frames_.empty(); }

private:
    VE_ScoreHeader header_{};
    std::vector<VE_FrameScore> frames_;
};
