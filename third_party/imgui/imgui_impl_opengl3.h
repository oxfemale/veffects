#pragma once

struct ImDrawData;

bool ImGui_ImplOpenGL3_Init(const char* glsl_version);
void ImGui_ImplOpenGL3_Shutdown();
void ImGui_ImplOpenGL3_NewFrame();
void ImGui_ImplOpenGL3_RenderDrawData(ImDrawData* draw_data);
