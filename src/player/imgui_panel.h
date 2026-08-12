#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "audio_player.h"
#include "veffects/score.h"

class ImGuiPanel {
public:
    void render(const VE_FrameScore* frame,
                AudioPlayer& audio,
                const std::vector<std::string>& scene_names,
                std::size_t& selected_scene) const;
};
