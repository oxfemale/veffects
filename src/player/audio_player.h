#pragma once

#include <string>

struct _Mix_Music;
using Mix_Music = _Mix_Music;

class AudioPlayer {
public:
    ~AudioPlayer();

    bool load(const char* mp3_path);
    void play();
    void pause();
    void toggle_mute();

    bool muted() const { return muted_; }

private:
    bool ensure_open();

    Mix_Music* music_ = nullptr;
    bool audio_open_ = false;
    bool muted_ = false;
    std::string path_;
};
