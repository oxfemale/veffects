#pragma once

#include <cstdio>
#include <string>
#include <vector>

class RenderMp4 {
public:
    ~RenderMp4();

    bool open(const std::string& output_path, int width, int height);
    bool capture_frame();
    void close();

private:
    FILE* pipe_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    std::vector<unsigned char> pixels_;
};
