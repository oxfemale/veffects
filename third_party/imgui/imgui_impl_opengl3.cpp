#include "imgui_impl_opengl3.h"

bool ImGui_ImplOpenGL3_Init(const char* glsl_version) {
    (void)glsl_version;
    return true;
}

void ImGui_ImplOpenGL3_Shutdown() {}
void ImGui_ImplOpenGL3_NewFrame() {}

void ImGui_ImplOpenGL3_RenderDrawData(ImDrawData* draw_data) {
    (void)draw_data;
}
