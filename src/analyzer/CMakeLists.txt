find_package(DrLibs REQUIRED)

add_executable(veffects-analyze analyzer.cpp)
target_include_directories(veffects-analyze PRIVATE
  ${PROJECT_SOURCE_DIR}/include
  ${DRLIBS_INCLUDE_DIR}
)
if(UNIX AND NOT APPLE)
  target_link_libraries(veffects-analyze PRIVATE m)
endif()
set_target_properties(veffects-analyze PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
