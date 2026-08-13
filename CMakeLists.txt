cmake_minimum_required(VERSION 3.16)
project(veffects LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(VEFFECTS_FETCH_SDL "Download and build SDL2 with FetchContent" OFF)

# Output layout: bin/ for executables, bin/plugins/ for scene plugins.
set(VEFFECTS_BIN     "${CMAKE_BINARY_DIR}/bin")
set(VEFFECTS_PLUGINS "${VEFFECTS_BIN}/plugins")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${VEFFECTS_BIN}")
# Pin per-config dirs too so multi-config generators (MSVC) don't add Release/ etc.
foreach(cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${cfg} "${VEFFECTS_BIN}")
endforeach()

find_package(Threads REQUIRED)

# ---------------- SDL2 ----------------
set(VEFFECTS_HAVE_SDL FALSE)
if(VEFFECTS_FETCH_SDL)
  include(FetchContent)
  FetchContent_Declare(SDL2
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-2.30.9)
  set(SDL2_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(SDL2)
  set(VEFFECTS_HAVE_SDL TRUE)
  set(VEFFECTS_SDL_TARGET SDL2::SDL2-static)
else()
  find_package(SDL2 QUIET)
  if(SDL2_FOUND)
    set(VEFFECTS_HAVE_SDL TRUE)
    if(TARGET SDL2::SDL2)
      set(VEFFECTS_SDL_TARGET SDL2::SDL2)
    else()
      set(VEFFECTS_SDL_TARGET ${SDL2_LIBRARIES})
    endif()
  endif()
endif()

# ---------------- shared include dirs ----------------
set(VEFFECTS_INCLUDES
  "${CMAKE_SOURCE_DIR}/include"
  "${CMAKE_SOURCE_DIR}/third_party/minimp3"
  "${CMAKE_SOURCE_DIR}/third_party/stb"
  "${CMAKE_SOURCE_DIR}/third_party/dr"
  "${CMAKE_SOURCE_DIR}/third_party/tml")

# ---------------- analyzer CLI ----------------
add_executable(veffects_gen src/veffects_gen.cpp)
target_include_directories(veffects_gen PRIVATE ${VEFFECTS_INCLUDES})
target_link_libraries(veffects_gen PRIVATE Threads::Threads)

# ---------------- player (SDL2 + Dear ImGui + tinyfiledialogs) ----------------
if(VEFFECTS_HAVE_SDL)
  set(IMGUI_DIR "${CMAKE_SOURCE_DIR}/third_party/imgui")
  set(IMGUI_SRC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdlrenderer2.cpp)
  set(TFD_DIR "${CMAKE_SOURCE_DIR}/third_party/tinyfiledialogs")

  add_executable(veffects_play
    src/veffects_play.cpp
    ${IMGUI_SRC}
    ${TFD_DIR}/tinyfiledialogs.c)
  target_compile_definitions(veffects_play PRIVATE USE_SDL)
  target_include_directories(veffects_play PRIVATE
    ${VEFFECTS_INCLUDES} ${IMGUI_DIR} ${IMGUI_DIR}/backends ${TFD_DIR})
  target_link_libraries(veffects_play PRIVATE
    ${VEFFECTS_SDL_TARGET} Threads::Threads ${CMAKE_DL_LIBS})
  if(NOT VEFFECTS_FETCH_SDL AND SDL2_INCLUDE_DIRS)
    target_include_directories(veffects_play PRIVATE ${SDL2_INCLUDE_DIRS})
  endif()
  if(WIN32)
    target_link_libraries(veffects_play PRIVATE comdlg32 ole32 shell32 user32)
    if(TARGET SDL2::SDL2main)
      target_link_libraries(veffects_play PRIVATE SDL2::SDL2main)
    endif()
  endif()
else()
  message(WARNING "SDL2 not found -> building veffects_play headless (no GUI). "
                  "Install SDL2 or configure with -DVEFFECTS_FETCH_SDL=ON.")
  add_executable(veffects_play src/veffects_play.cpp)
  target_include_directories(veffects_play PRIVATE ${VEFFECTS_INCLUDES})
  target_link_libraries(veffects_play PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
endif()

# ---------------- scene plugins ----------------
file(GLOB VEFFECTS_PLUGIN_SRC "${CMAKE_SOURCE_DIR}/plugins/*.cpp")
foreach(psrc ${VEFFECTS_PLUGIN_SRC})
  get_filename_component(pname "${psrc}" NAME_WE)
  add_library(${pname} MODULE "${psrc}")
  target_include_directories(${pname} PRIVATE ${VEFFECTS_INCLUDES})
  set_target_properties(${pname} PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY "${VEFFECTS_PLUGINS}"
    RUNTIME_OUTPUT_DIRECTORY "${VEFFECTS_PLUGINS}")
  foreach(cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(${pname} PROPERTIES
      LIBRARY_OUTPUT_DIRECTORY_${cfg} "${VEFFECTS_PLUGINS}"
      RUNTIME_OUTPUT_DIRECTORY_${cfg} "${VEFFECTS_PLUGINS}")
  endforeach()
  if(APPLE)
    set_target_properties(${pname} PROPERTIES SUFFIX ".dylib")  # match the loader
  endif()
  if(MSVC)
    target_compile_options(${pname} PRIVATE /O2)
  else()
    target_compile_options(${pname} PRIVATE -O3 -fvisibility=hidden)
  endif()
endforeach()

message(STATUS "veffects: SDL2=${VEFFECTS_HAVE_SDL}  plugins -> ${VEFFECTS_PLUGINS}")
