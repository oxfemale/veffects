#include "../../third_party/glad/glad.h"
#include "../../include/veffects/scene_api.h"

#include <cmath>
#include <vector>

namespace {

struct Vertex { float x, y, r, g, b; };
constexpr float kPi = 3.14159265358979323846f;
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
    const int arms = 12;
    const float rotation = ctx->time_sec * (0.7f + ctx->score->tempo_hz * 6.0f);
    const float base_length = 0.25f + ctx->score->mid * 0.45f;
    const float jitter = ctx->score->high * 0.05f;
    vertices.reserve(arms * 6);
    for (int i = 0; i < arms; ++i) {
        const float angle = rotation + static_cast<float>(i) * (2.0f * kPi / arms);
        const float length = base_length + std::sin(ctx->time_sec * 3.0f + angle * 2.0f) * jitter;
        const float x = std::cos(angle) * length;
        const float y = std::sin(angle) * length;
        vertices.push_back({0.0f, 0.0f, 0.3f, 0.7f, 1.0f});
        vertices.push_back({x, y, 0.8f, 0.95f, 1.0f});

        const float branch_angle = angle + 0.35f * std::sin(ctx->time_sec + static_cast<float>(i));
        const float bx = x + std::cos(branch_angle) * 0.12f;
        const float by = y + std::sin(branch_angle) * 0.12f;
        vertices.push_back({x, y, 0.4f, 0.8f, 1.0f});
        vertices.push_back({bx, by, 0.8f, 0.95f, 1.0f});
    }

    glUseProgram(g_program);
    glBindVertexArray(g_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)), vertices.data(), GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(sizeof(float) * 2));
    glLineWidth(2.5f + ctx->score->mid * 3.0f);
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
