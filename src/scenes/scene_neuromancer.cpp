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
    const float tilt = (ctx->score->spectral_centroid - 0.5f) * 0.7f;
    vertices.reserve(300);
    for (int i = -8; i <= 8; ++i) {
        const float fx = static_cast<float>(i) / 8.0f;
        vertices.push_back({fx + tilt * 0.2f, -1.0f, 0.35f, 1.0f, 0.9f});
        vertices.push_back({fx - tilt * 0.2f,  1.0f, 0.35f, 1.0f, 0.9f});
    }
    for (int i = 0; i < 18; ++i) {
        const float z = static_cast<float>(i) / 17.0f;
        const float width = 1.1f - z * 0.9f;
        const float y = -0.95f + z * 1.85f;
        vertices.push_back({-width + tilt * z, y, 0.2f, 0.7f, 1.0f});
        vertices.push_back({ width + tilt * z, y, 0.2f, 0.7f, 1.0f});
    }
    if (ctx->score->onset > 0.5f) {
        for (int i = 0; i < 8; ++i) {
            const float angle = ctx->time_sec * 7.0f + static_cast<float>(i) * 0.7f;
            const float x = std::cos(angle) * 0.2f;
            const float y = std::sin(angle) * 0.2f;
            vertices.push_back({0.0f, 0.0f, 1.0f, 1.0f, 1.0f});
            vertices.push_back({x, y, 0.6f, 1.0f, 1.0f});
        }
    }

    glUseProgram(g_program);
    glBindVertexArray(g_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)), vertices.data(), GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(sizeof(float) * 2));
    glLineWidth(1.5f + ctx->score->high * 2.0f);
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
