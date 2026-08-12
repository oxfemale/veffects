#include "imgui.h"

namespace ImGui {
static IO g_io;
static ImDrawData g_draw_data;

void CreateContext() {}
void DestroyContext() {}
IO& GetIO() { return g_io; }
bool Begin(const char* name, bool* p_open, int flags) {
    (void)name;
    (void)p_open;
    (void)flags;
    return true;
}
void End() {}
void Text(const char* fmt, ...) {
    (void)fmt;
}
bool Button(const char* label) {
    (void)label;
    return false;
}
bool Checkbox(const char* label, bool* v) {
    (void)label;
    (void)v;
    return false;
}
void NewFrame() {}
void Render() {}
ImDrawData* GetDrawData() { return &g_draw_data; }
}  // namespace ImGui
