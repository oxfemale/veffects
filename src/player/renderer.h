#pragma once

#include <cstdint>

#include "../../third_party/glad/glad.h"

class Renderer {
public:
    Renderer() = default;
    ~Renderer();

    bool init(int w, int h);
    void begin_frame();
    void end_frame();

private:
    bool init_program();
    void cleanup();

    int width_ = 0;
    int height_ = 0;
    GLuint program_ = 0;
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
};
