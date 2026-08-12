#include "audio_player.h"

#include <SDL_mixer.h>

AudioPlayer::~AudioPlayer() {
    if (music_) {
        Mix_FreeMusic(music_);
        music_ = nullptr;
    }
    if (audio_open_) {
        Mix_CloseAudio();
        audio_open_ = false;
    }
}

bool AudioPlayer::ensure_open() {
    if (audio_open_) {
        return true;
    }
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 1024) != 0) {
        return false;
    }
    audio_open_ = true;
    return true;
}

bool AudioPlayer::load(const char* mp3_path) {
    if (!ensure_open()) {
        return false;
    }
    if (music_) {
        Mix_FreeMusic(music_);
        music_ = nullptr;
    }
    path_ = mp3_path ? mp3_path : "";
    music_ = Mix_LoadMUS(path_.c_str());
    return music_ != nullptr;
}

void AudioPlayer::play() {
    if (music_) {
        Mix_VolumeMusic(muted_ ? 0 : MIX_MAX_VOLUME);
        Mix_PlayMusic(music_, 0);
    }
}

void AudioPlayer::pause() {
    if (Mix_PlayingMusic()) {
        Mix_PauseMusic();
    }
}

void AudioPlayer::toggle_mute() {
    muted_ = !muted_;
    Mix_VolumeMusic(muted_ ? 0 : MIX_MAX_VOLUME);
}
