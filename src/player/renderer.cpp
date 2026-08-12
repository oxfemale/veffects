#include "renderer.h"

        #include <array>
        #include <iostream>

        namespace {

        GLuint compile_shader(GLenum type, const char* source) {
            GLuint shader = glCreateShader(type);
            glShaderSource(shader, 1, &source, nullptr);
            glCompileShader(shader);
            GLint ok = 0;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
            if (!ok) {
                char log[512] = {};
                glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
                std::cerr << "shader compile failed: " << log << '\n';
            }
            return shader;
        }

        }  // namespace

        Renderer::~Renderer() {
            cleanup();
        }

        bool Renderer::init_program() {
            static const char* kVertex = R"(
                #version 330 core
                layout (location = 0) in vec2 a_pos;
                void main() {
                    gl_Position = vec4(a_pos, 0.0, 1.0);
                }
            )";
            static const char* kFragment = R"(
                #version 330 core
                out vec4 frag_color;
                void main() {
                    frag_color = vec4(0.02, 0.02, 0.04, 1.0);
                }
            )";
            const GLuint vs = compile_shader(GL_VERTEX_SHADER, kVertex);
            const GLuint fs = compile_shader(GL_FRAGMENT_SHADER, kFragment);
            program_ = glCreateProgram();
            glAttachShader(program_, vs);
            glAttachShader(program_, fs);
            glLinkProgram(program_);
            glDeleteShader(vs);
            glDeleteShader(fs);
            GLint ok = 0;
            glGetProgramiv(program_, GL_LINK_STATUS, &ok);
            if (!ok) {
                char log[512] = {};
                glGetProgramInfoLog(program_, sizeof(log), nullptr, log);
                std::cerr << "program link failed: " << log << '\n';
                return false;
            }
            return true;
        }

        bool Renderer::init(int w, int h) {
            width_ = w;
            height_ = h;
            if (!init_program()) {
                return false;
            }

            const std::array<float, 12> quad = {
                -1.0f, -1.0f,
                 1.0f, -1.0f,
                 1.0f,  1.0f,
                -1.0f, -1.0f,
                 1.0f,  1.0f,
                -1.0f,  1.0f,
            };

            glGenVertexArrays(1, &vao_);
            glGenBuffers(1, &vbo_);
            glBindVertexArray(vao_);
            glBindBuffer(GL_ARRAY_BUFFER, vbo_);
            glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(quad.size() * sizeof(float)), quad.data(), GL_STATIC_DRAW);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            glBindVertexArray(0);
            return true;
        }

        void Renderer::begin_frame() {
            glViewport(0, 0, width_, height_);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glUseProgram(program_);
            glBindVertexArray(vao_);
            glDrawArrays(GL_TRIANGLES, 0, 6);
            glBindVertexArray(0);
            glUseProgram(0);
        }

        void Renderer::end_frame() {}

        void Renderer::cleanup() {
            if (vbo_) {
                glDeleteBuffers(1, &vbo_);
                vbo_ = 0;
            }
            if (vao_) {
                glDeleteVertexArrays(1, &vao_);
                vao_ = 0;
            }
            if (program_) {
                glDeleteProgram(program_);
                program_ = 0;
            }
        }
