#include "imgui_impl_sdl2.h"

bool ImGui_ImplSDL2_InitForOpenGL(SDL_Window* window, void* gl_context) {
    (void)window;
    (void)gl_context;
    return true;
}

void ImGui_ImplSDL2_Shutdown() {}
void ImGui_ImplSDL2_NewFrame() {}

bool ImGui_ImplSDL2_ProcessEvent(const SDL_Event* event) {
    (void)event;
    return false;
}
