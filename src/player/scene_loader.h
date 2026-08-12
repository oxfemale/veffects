#pragma once

#include <string>

#include "veffects/scene_api.h"

#if defined(_WIN32)
#include <windows.h>
using VE_ModuleHandle = HMODULE;
#else
#include <dlfcn.h>
using VE_ModuleHandle = void*;
#endif

struct ScenePlugin {
    VE_ModuleHandle handle = nullptr;
    VE_SceneInitFn init = nullptr;
    VE_SceneDrawFn draw = nullptr;
    VE_SceneShutdownFn shutdown = nullptr;
    std::string name;
    std::string path;

    bool load(const std::string& plugin_name, const std::string& plugin_path) {
        unload();
        name = plugin_name;
        path = plugin_path;
    #if defined(_WIN32)
        handle = LoadLibraryA(plugin_path.c_str());
        if (!handle) {
            return false;
        }
        init = reinterpret_cast<VE_SceneInitFn>(GetProcAddress(handle, VE_SCENE_INIT_SYM));
        draw = reinterpret_cast<VE_SceneDrawFn>(GetProcAddress(handle, VE_SCENE_DRAW_SYM));
        shutdown = reinterpret_cast<VE_SceneShutdownFn>(GetProcAddress(handle, VE_SCENE_SHUTDOWN_SYM));
    #else
        handle = dlopen(plugin_path.c_str(), RTLD_NOW);
        if (!handle) {
            return false;
        }
        init = reinterpret_cast<VE_SceneInitFn>(dlsym(handle, VE_SCENE_INIT_SYM));
        draw = reinterpret_cast<VE_SceneDrawFn>(dlsym(handle, VE_SCENE_DRAW_SYM));
        shutdown = reinterpret_cast<VE_SceneShutdownFn>(dlsym(handle, VE_SCENE_SHUTDOWN_SYM));
    #endif
        return init != nullptr && draw != nullptr && shutdown != nullptr;
    }

    void unload() {
        if (shutdown) {
            shutdown();
        }
        shutdown = nullptr;
        draw = nullptr;
        init = nullptr;
        if (handle) {
        #if defined(_WIN32)
            FreeLibrary(handle);
        #else
            dlclose(handle);
        #endif
            handle = nullptr;
        }
    }

    ~ScenePlugin() { unload(); }
};
