#include "../../third_party/glad/glad.h"
#include "../../include/veffects/scene_api.h"

#include <cmath>
#include <vector>

namespace {

struct Vertex { float x, y, r, g, b; };
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
        void main() {
            gl_Position = vec4(a_pos, 0.0, 1.0);
            v_color = a_color;
        }
    )";
    static const char* kFragment = R"(
        #version 330 core
        in vec3 v_color;
        out vec4 frag_color;
        void main() { frag_color = vec4(v_color, 1.0); }
    )";
    GLuint vs = compile_shader(GL_VERTEX_SHADER, kVertex);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, kFragment);
    GLuint program = glCreateProgram();
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
    const float pulse = 0.5f + ctx->score->bass * 0.5f;
    const float vanish_y = -0.1f + ctx->score->band[3] * 0.2f;
    vertices.reserve(256);
    for (int i = 0; i < 18; ++i) {
        const float t = static_cast<float>(i) / 17.0f;
        const float z = std::fmod(ctx->time_sec * (0.4f + ctx->score->bass) + t, 1.0f);
        const float width = 1.0f - z * 0.8f;
        const float y = -1.0f + z * (1.1f + pulse * 0.7f);
        vertices.push_back({-width, y, 1.0f, 0.1f, 0.7f});
        vertices.push_back({ width, y, 1.0f, 0.1f, 0.7f});
    }
    for (int i = 0; i < 16; ++i) {
        const float x = -1.0f + 2.0f * static_cast<float>(i) / 15.0f;
        vertices.push_back({x, -1.0f, 0.4f, 0.9f, 1.0f});
        vertices.push_back({x * 0.12f, vanish_y, 0.4f, 0.9f, 1.0f});
    }
    for (int i = 0; i < 15; ++i) {
        const float x0 = -1.0f + 2.0f * static_cast<float>(i) / 14.0f;
        const float x1 = -1.0f + 2.0f * static_cast<float>(i + 1) / 14.0f;
        const float y0 = 0.15f + (ctx->score->band[i % 16] - 0.5f) * 0.25f;
        const float y1 = 0.15f + (ctx->score->band[(i + 1) % 16] - 0.5f) * 0.25f;
        vertices.push_back({x0, y0, 1.0f, 0.45f, 0.3f});
        vertices.push_back({x1, y1, 1.0f, 0.45f, 0.3f});
    }

    glUseProgram(g_program);
    glBindVertexArray(g_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)), vertices.data(), GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(sizeof(float) * 2));
    glLineWidth(2.0f + ctx->score->bass * 4.0f);
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(vertices.size()));
    glBindVertexArray(0);
    glUseProgram(0);
}

extern "C" void ve_scene_shutdown(void) {
    if (g_vbo) glDeleteBuffers(1, &g_vbo);
    if (g_vao) glDeleteVertexArrays(1, &g_vao);
    if (g_program) glDeleteProgram(g_program);
    g_vbo = g_vao = g_program = 0;
}
