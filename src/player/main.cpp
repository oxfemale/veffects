#include <SDL.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../../third_party/glad/glad.h"
#include "../../third_party/imgui/imgui.h"
#include "../../third_party/imgui/imgui_impl_opengl3.h"
#include "../../third_party/imgui/imgui_impl_sdl2.h"
#include "audio_player.h"
#include "imgui_panel.h"
#include "render_mp4.h"
#include "renderer.h"
#include "scene_loader.h"
#include "score_reader.h"

namespace {

struct Options {
    std::string score_path;
    std::string audio_path;
    std::string scene_name = "git";
    std::string render_mp4_path;
};

std::string plugin_extension() {
#if defined(_WIN32)
    return ".dll";
#elif defined(__APPLE__)
    return ".so";
#else
    return ".so";
#endif
}

Options parse_args(int argc, char** argv) {
    if (argc < 3) {
        throw std::runtime_error("usage: veffects-play <score.vsc> <audio.mp3> [--scene <name>] [--render-mp4 <out.mp4>]");
    }
    Options options;
    options.score_path = argv[1];
    options.audio_path = argv[2];
    for (int i = 3; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--scene" && i + 1 < argc) {
            options.scene_name = argv[++i];
        } else if (arg == "--render-mp4" && i + 1 < argc) {
            options.render_mp4_path = argv[++i];
        } else {
            throw std::runtime_error("unknown argument: " + arg);
        }
    }
    return options;
}

std::vector<std::string> scene_names() {
    return {"git", "tachikoma", "bladerunner", "neuromancer", "nirvana"};
}

std::string resolve_scene_path(const std::filesystem::path& exe_path, const std::string& scene_name) {
    const std::string file_name = "scene_" + scene_name + plugin_extension();
    const std::vector<std::filesystem::path> candidates = {
        exe_path.parent_path() / "scenes" / file_name,
        exe_path.parent_path() / file_name,
        std::filesystem::current_path() / "build" / "bin" / "scenes" / file_name,
        std::filesystem::current_path() / file_name,
    };
    for (const auto& candidate : candidates) {
        if (std::filesystem::exists(candidate)) {
            return candidate.string();
        }
    }
    throw std::runtime_error("could not locate plugin for scene '" + scene_name + "'");
}

void render_live(SDL_Window* window,
                 Renderer& renderer,
                 ScenePlugin& scene,
                 AudioPlayer& audio,
                 const ScoreReader& score,
                 const std::filesystem::path& exe_path,
                 const std::vector<std::string>& scenes,
                 std::size_t selected_scene) {
    ImGuiPanel panel;
    audio.play();
    auto start = std::chrono::steady_clock::now();
    bool running = true;

    while (running) {
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) {
                running = false;
            }
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        const auto now = std::chrono::steady_clock::now();
        const float elapsed = std::chrono::duration<float>(now - start).count();
        const std::size_t frame_index = score.empty()
            ? 0
            : std::min<std::size_t>(score.size() - 1, static_cast<std::size_t>(elapsed * 60.0f));
        const VE_FrameScore* frame = score.frame_at(frame_index);

        renderer.begin_frame();
        if (frame && scene.draw) {
            VE_SceneContext ctx{1280, 720, elapsed, frame};
            scene.draw(&ctx);
        }

        panel.render(frame, audio, scenes, selected_scene);
        if (selected_scene < scenes.size() && scenes[selected_scene] != scene.name) {
            const std::string path = resolve_scene_path(exe_path, scenes[selected_scene]);
            if (!scene.load(scenes[selected_scene], path)) {
                throw std::runtime_error("failed to switch scene plugin: " + path);
            }
            if (scene.init) {
                scene.init(1280, 720);
            }
        }

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        renderer.end_frame();
        SDL_GL_SwapWindow(window);
        SDL_Delay(16);
    }
}

void render_offline(Renderer& renderer,
                    ScenePlugin& scene,
                    const ScoreReader& score,
                    const std::string& output_path) {
    RenderMp4 writer;
    if (!writer.open(output_path, 1280, 720)) {
        throw std::runtime_error("failed to launch ffmpeg for offline render");
    }
    for (std::size_t i = 0; i < score.size(); ++i) {
        const VE_FrameScore* frame = score.frame_at(i);
        renderer.begin_frame();
        if (frame && scene.draw) {
            VE_SceneContext ctx{1280, 720, static_cast<float>(i) / 60.0f, frame};
            scene.draw(&ctx);
        }
        renderer.end_frame();
        if (!writer.capture_frame()) {
            throw std::runtime_error("failed to stream frame to ffmpeg");
        }
    }
    writer.close();
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Options options = parse_args(argc, argv);
        ScoreReader score;
        if (!score.load(options.score_path.c_str())) {
            throw std::runtime_error("failed to load score file");
        }

        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
            throw std::runtime_error(SDL_GetError());
        }

        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

        const Uint32 window_flags = SDL_WINDOW_OPENGL | (options.render_mp4_path.empty() ? SDL_WINDOW_SHOWN : SDL_WINDOW_HIDDEN);
        SDL_Window* window = SDL_CreateWindow("veffects", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, window_flags);
        if (!window) {
            throw std::runtime_error(SDL_GetError());
        }
        SDL_GLContext gl_context = SDL_GL_CreateContext(window);
        if (!gl_context) {
            SDL_DestroyWindow(window);
            throw std::runtime_error(SDL_GetError());
        }
        SDL_GL_SetSwapInterval(1);
        if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress))) {
            SDL_GL_DeleteContext(gl_context);
            SDL_DestroyWindow(window);
            throw std::runtime_error("failed to load OpenGL symbols");
        }

        {
            Renderer renderer;
            if (!renderer.init(1280, 720)) {
                SDL_GL_DeleteContext(gl_context);
                SDL_DestroyWindow(window);
                throw std::runtime_error("failed to initialize renderer");
            }

            const auto scenes = scene_names();
            auto found = std::find(scenes.begin(), scenes.end(), options.scene_name);
            std::size_t selected_scene = found == scenes.end() ? 0 : static_cast<std::size_t>(std::distance(scenes.begin(), found));
            const std::filesystem::path exe_path = std::filesystem::path(argv[0]).lexically_normal();

            ScenePlugin scene;
            const std::string plugin_path = resolve_scene_path(exe_path, scenes[selected_scene]);
            if (!scene.load(scenes[selected_scene], plugin_path)) {
                throw std::runtime_error("failed to load scene plugin: " + plugin_path);
            }
            scene.init(1280, 720);

            AudioPlayer audio;
            if (!options.render_mp4_path.empty()) {
                render_offline(renderer, scene, score, options.render_mp4_path);
            } else {
                if (!audio.load(options.audio_path.c_str())) {
                    std::cerr << "warning: audio playback disabled\n";
                }
                IMGUI_CHECKVERSION();
                ImGui::CreateContext();
                ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
                ImGui_ImplOpenGL3_Init("#version 330");
                render_live(window, renderer, scene, audio, score, exe_path, scenes, selected_scene);
                ImGui_ImplOpenGL3_Shutdown();
                ImGui_ImplSDL2_Shutdown();
                ImGui::DestroyContext();
            }

            scene.unload();
        }

        SDL_GL_DeleteContext(gl_context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "veffects-play: " << ex.what() << '\n';
        return 1;
    }
}
