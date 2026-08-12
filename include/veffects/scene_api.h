#pragma once
#include "score.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct VE_SceneContext {
    int width;
    int height;
    float time_sec;
    const VE_FrameScore *score;
} VE_SceneContext;

typedef void (*VE_SceneInitFn)(int width, int height);
typedef void (*VE_SceneDrawFn)(const VE_SceneContext *ctx);
typedef void (*VE_SceneShutdownFn)(void);

#define VE_SCENE_INIT_SYM     "ve_scene_init"
#define VE_SCENE_DRAW_SYM     "ve_scene_draw"
#define VE_SCENE_SHUTDOWN_SYM "ve_scene_shutdown"

#ifdef __cplusplus
}
#endif
