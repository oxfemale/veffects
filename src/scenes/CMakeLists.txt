function(add_ve_scene target source)
  add_library(${target} MODULE ${source})
  target_include_directories(${target} PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/third_party/glad
  )
  target_link_libraries(${target} PRIVATE glad)
  if(UNIX AND NOT APPLE)
    target_link_libraries(${target} PRIVATE m)
  endif()
  set_target_properties(${target} PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/scenes
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/scenes
  )
endfunction()

add_ve_scene(scene_git scene_git.cpp)
add_ve_scene(scene_tachikoma scene_tachikoma.cpp)
add_ve_scene(scene_bladerunner scene_bladerunner.cpp)
add_ve_scene(scene_neuromancer scene_neuromancer.cpp)
add_ve_scene(scene_nirvana scene_nirvana.cpp)
