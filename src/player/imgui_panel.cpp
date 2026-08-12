#include "imgui_panel.h"

#include <cstdio>

#include "../../third_party/imgui/imgui.h"

void ImGuiPanel::render(const VE_FrameScore* frame,
                        AudioPlayer& audio,
                        const std::vector<std::string>& scene_names,
                        std::size_t& selected_scene) const {
    if (!ImGui::Begin("veffects")) {
        ImGui::End();
        return;
    }

    if (frame) {
        ImGui::Text("Frame: %u", frame->frame_index);
        ImGui::Text("Tempo estimate: %.2f BPM", frame->tempo_hz * 60.0f);
    } else {
        ImGui::Text("No frame loaded");
    }

    if (ImGui::Button(audio.muted() ? "Unmute audio" : "Mute audio")) {
        audio.toggle_mute();
    }

    ImGui::Text("Scenes:");
    for (std::size_t i = 0; i < scene_names.size(); ++i) {
        const std::string label = (i == selected_scene ? "* " : "  ") + scene_names[i];
        if (ImGui::Button(label.c_str())) {
            selected_scene = i;
        }
    }
    ImGui::End();
}
