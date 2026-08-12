set(SDL2_HINT_PATHS
  "$ENV{SDL2_DIR}"
  "$ENV{SDL2_ROOT}"
  "$ENV{ChocolateyInstall}/lib/sdl2"
  "$ENV{ChocolateyInstall}/lib/sdl2-mixer"
)

find_package(SDL2 QUIET CONFIG)
find_package(SDL2_mixer QUIET CONFIG)

if(TARGET SDL2::SDL2)
  set(VE_SDL2_TARGET SDL2::SDL2)
else()
  find_path(SDL2_INCLUDE_DIR SDL.h PATH_SUFFIXES SDL2 include PATHS ${SDL2_HINT_PATHS})
  find_library(SDL2_LIBRARY NAMES SDL2 PATHS ${SDL2_HINT_PATHS} PATH_SUFFIXES lib lib/x64)
  if(NOT SDL2_INCLUDE_DIR OR NOT SDL2_LIBRARY)
    message(FATAL_ERROR "SDL2 not found. Install SDL2 development packages or configure with -DVE_BUILD_PLAYER=OFF.")
  endif()
  add_library(veffects_sdl2 UNKNOWN IMPORTED)
  set_target_properties(veffects_sdl2 PROPERTIES IMPORTED_LOCATION "${SDL2_LIBRARY}")
  target_include_directories(veffects_sdl2 INTERFACE "${SDL2_INCLUDE_DIR}")
  set(VE_SDL2_TARGET veffects_sdl2)
endif()

if(TARGET SDL2_mixer::SDL2_mixer)
  set(VE_SDL2_MIXER_TARGET SDL2_mixer::SDL2_mixer)
else()
  find_path(SDL2_MIXER_INCLUDE_DIR SDL_mixer.h PATH_SUFFIXES SDL2 include PATHS ${SDL2_HINT_PATHS})
  find_library(SDL2_MIXER_LIBRARY NAMES SDL2_mixer SDL2_mixer-static PATHS ${SDL2_HINT_PATHS} PATH_SUFFIXES lib lib/x64)
  if(NOT SDL2_MIXER_INCLUDE_DIR OR NOT SDL2_MIXER_LIBRARY)
    message(FATAL_ERROR "SDL2_mixer not found. Install SDL2_mixer development packages or configure with -DVE_BUILD_PLAYER=OFF.")
  endif()
  add_library(veffects_sdl2_mixer UNKNOWN IMPORTED)
  set_target_properties(veffects_sdl2_mixer PROPERTIES IMPORTED_LOCATION "${SDL2_MIXER_LIBRARY}")
  target_include_directories(veffects_sdl2_mixer INTERFACE "${SDL2_MIXER_INCLUDE_DIR}")
  set(VE_SDL2_MIXER_TARGET veffects_sdl2_mixer)
endif()

add_executable(veffects-play
  main.cpp
  renderer.cpp
  audio_player.cpp
  imgui_panel.cpp
  render_mp4.cpp
  ${PROJECT_SOURCE_DIR}/third_party/imgui/imgui.cpp
  ${PROJECT_SOURCE_DIR}/third_party/imgui/imgui_impl_sdl2.cpp
  ${PROJECT_SOURCE_DIR}/third_party/imgui/imgui_impl_opengl3.cpp
)

target_include_directories(veffects-play PRIVATE
  ${PROJECT_SOURCE_DIR}/include
  ${PROJECT_SOURCE_DIR}/third_party/glad
  ${PROJECT_SOURCE_DIR}/third_party/imgui
  ${PROJECT_SOURCE_DIR}/src/player
)

target_link_libraries(veffects-play PRIVATE glad ${VE_SDL2_TARGET} ${VE_SDL2_MIXER_TARGET})

if(UNIX)
  target_link_libraries(veffects-play PRIVATE ${CMAKE_DL_LIBS})
endif()
if(UNIX AND NOT APPLE)
  target_link_libraries(veffects-play PRIVATE m)
endif()
if(WIN32)
  target_compile_definitions(veffects-play PRIVATE NOMINMAX)
endif()

set_target_properties(veffects-play PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
