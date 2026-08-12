#include "render_mp4.h"

#include <sstream>

#include "../../third_party/glad/glad.h"

#if defined(_WIN32)
#include <stdio.h>
#define VE_POPEN _popen
#define VE_PCLOSE _pclose
#else
#define VE_POPEN popen
#define VE_PCLOSE pclose
#endif

RenderMp4::~RenderMp4() {
    close();
}

bool RenderMp4::open(const std::string& output_path, int width, int height) {
    close();
    width_ = width;
    height_ = height;
    pixels_.assign(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) * 4u, 0u);
    std::ostringstream command;
    command << "ffmpeg -y -f rawvideo -pix_fmt rgba -s "
            << width_ << 'x' << height_
            << " -r 60 -i - -vf vflip -c:v libx264 \""
            << output_path << "\"";
    pipe_ = VE_POPEN(command.str().c_str(), "wb");
    return pipe_ != nullptr;
}

bool RenderMp4::capture_frame() {
    if (!pipe_) {
        return false;
    }
    glReadPixels(0, 0, width_, height_, GL_RGBA, GL_UNSIGNED_BYTE, pixels_.data());
    return std::fwrite(pixels_.data(), 1, pixels_.size(), pipe_) == pixels_.size();
}

void RenderMp4::close() {
    if (pipe_) {
        VE_PCLOSE(pipe_);
        pipe_ = nullptr;
    }
}
