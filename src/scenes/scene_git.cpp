#include "../../third_party/glad/glad.h"
#include "../../include/veffects/scene_api.h"

#include <cmath>
#include <iostream>
#include <vector>

namespace {

struct Vertex {
    float x;
    float y;
    float r;
    float g;
    float b;
};

GLuint g_program = 0;
GLuint g_vao = 0;
GLuint g_vbo = 0;

GLuint compile_shader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    return shader;
}

GLuint create_program() {
    static const char* kVertex = R"(
        #version 330 core
        layout (location = 0) in vec2 a_pos;
        layout (location = 1) in vec3 a_color;
        out vec3 v_color;
        uniform float u_point_size;
        void main() {
            gl_Position = vec4(a_pos, 0.0, 1.0);
            gl_PointSize = u_point_size;
            v_color = a_color;
        }
    )";
    static const char* kFragment = R"(
        #version 330 core
        in vec3 v_color;
        out vec4 frag_color;
        void main() { frag_color = vec4(v_color, 1.0); }
    )";
    const GLuint vs = compile_shader(GL_VERTEX_SHADER, kVertex);
    const GLuint fs = compile_shader(GL_FRAGMENT_SHADER, kFragment);
    const GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    return program;
}

}  // namespace

extern "C" void ve_scene_init(int width, int height) {
    (void)width;
    (void)height;
    g_program = create_program();
    glGenVertexArrays(1, &g_vao);
    glGenBuffers(1, &g_vbo);
}

extern "C" void ve_scene_draw(const VE_SceneContext* ctx) {
    if (!ctx || !ctx->score) {
        return;
    }
    std::vector<Vertex> vertices;
    const int columns = 64;
    const int rows = 24;
    const float speed = 0.2f + ctx->score->bass * 1.8f;
    const float flash = ctx->score->onset > 0.5f ? 0.45f : 0.0f;
    vertices.reserve(columns * rows + 8);
    for (int x = 0; x < columns; ++x) {
        const float fx = -0.98f + 1.96f * static_cast<float>(x) / static_cast<float>(columns - 1);
        const float phase = std::fmod(static_cast<float>(x) * 0.173f + ctx->time_sec * speed, 2.0f);
        for (int y = 0; y < rows; ++y) {
            const float fy = 1.0f - std::fmod(phase + static_cast<float>(y) * 0.11f, 2.0f);
            const float brightness = 0.25f + 0.75f * (1.0f - static_cast<float>(y) / static_cast<float>(rows));
            vertices.push_back({fx, fy, 0.1f + flash, brightness + flash, 0.15f + flash});
        }
    }
    if (flash > 0.0f) {
        vertices.push_back({-1.0f, -1.0f, 0.8f, 0.9f, 0.8f});
        vertices.push_back({ 1.0f,  1.0f, 0.8f, 0.9f, 0.8f});
        vertices.push_back({-1.0f,  1.0f, 0.8f, 0.9f, 0.8f});
        vertices.push_back({ 1.0f, -1.0f, 0.8f, 0.9f, 0.8f});
    }

    glUseProgram(g_program);
    glBindVertexArray(g_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)), vertices.data(), GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(sizeof(float) * 2));
    glUniform1f(glGetUniformLocation(g_program, "u_point_size"), 2.0f + ctx->score->bass * 5.0f);
    glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(columns * rows));
    if (flash > 0.0f) {
        glDrawArrays(GL_LINES, columns * rows, 4);
    }
    glBindVertexArray(0);
    glUseProgram(0);
}

extern "C" void ve_scene_shutdown(void) {
    if (g_vbo) {
        glDeleteBuffers(1, &g_vbo);
        g_vbo = 0;
    }
    if (g_vao) {
        glDeleteVertexArrays(1, &g_vao);
        g_vao = 0;
    }
    if (g_program) {
        glDeleteProgram(g_program);
        g_program = 0;
    }
}
