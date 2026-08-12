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
    const int segments = 256;
    const float amp = 0.2f + ctx->score->rms * 0.55f;
    const float ratio = 2.0f + ctx->score->tempo_hz * 6.0f;
    const float noise = ctx->score->high * 0.08f;
    vertices.reserve(segments * 2);
    for (int i = 0; i < segments; ++i) {
        const float t0 = static_cast<float>(i) / static_cast<float>(segments - 1);
        const float t1 = static_cast<float>(i + 1) / static_cast<float>(segments - 1);
        const float a0 = t0 * 2.0f * kPi;
        const float a1 = t1 * 2.0f * kPi;
        const float x0 = std::sin(a0 * ratio + ctx->time_sec) * amp + std::sin(a0 * 11.0f) * noise;
        const float y0 = std::cos(a0 * 3.0f - ctx->time_sec * 0.5f) * amp + std::cos(a0 * 13.0f) * noise;
        const float x1 = std::sin(a1 * ratio + ctx->time_sec) * amp + std::sin(a1 * 11.0f) * noise;
        const float y1 = std::cos(a1 * 3.0f - ctx->time_sec * 0.5f) * amp + std::cos(a1 * 13.0f) * noise;
        vertices.push_back({x0, y0, 1.0f, 0.8f, 0.2f});
        vertices.push_back({x1, y1, 1.0f, 0.3f + ctx->score->high * 0.7f, 0.3f});
    }

    glUseProgram(g_program);
    glBindVertexArray(g_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)), vertices.data(), GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(sizeof(float) * 2));
    glLineWidth(1.0f + ctx->score->rms * 5.0f);
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
