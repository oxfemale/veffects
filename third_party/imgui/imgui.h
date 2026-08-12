#pragma once

#include <cstdarg>

struct ImDrawData {};

#define IMGUI_CHECKVERSION() ((void)0)

namespace ImGui {
struct IO {
    float DeltaTime = 1.0f / 60.0f;
};

void CreateContext();
void DestroyContext();
IO& GetIO();
bool Begin(const char* name, bool* p_open = nullptr, int flags = 0);
void End();
void Text(const char* fmt, ...);
bool Button(const char* label);
bool Checkbox(const char* label, bool* v);
void NewFrame();
void Render();
ImDrawData* GetDrawData();
}  // namespace ImGui
