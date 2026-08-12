cmake_minimum_required(VERSION 3.16)
project(veffects VERSION 0.1.0 LANGUAGES C CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(VE_BUILD_ANALYZER "Build the analyzer CLI" ON)
option(VE_BUILD_PLAYER   "Build the player" ON)
option(VE_BUILD_SCENES   "Build scene plugins" ON)

add_subdirectory(third_party/glad)

if(VE_BUILD_ANALYZER)
  add_subdirectory(src/analyzer)
endif()

if(VE_BUILD_PLAYER)
  add_subdirectory(src/player)
endif()

if(VE_BUILD_SCENES)
  add_subdirectory(src/scenes)
endif()
