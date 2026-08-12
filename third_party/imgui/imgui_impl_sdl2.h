#pragma once

struct SDL_Window;
struct SDL_Event;

bool ImGui_ImplSDL2_InitForOpenGL(SDL_Window* window, void* gl_context);
void ImGui_ImplSDL2_Shutdown();
void ImGui_ImplSDL2_NewFrame();
bool ImGui_ImplSDL2_ProcessEvent(const SDL_Event* event);
