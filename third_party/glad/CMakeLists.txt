add_library(glad STATIC glad.c)
target_include_directories(glad PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(glad PROPERTIES POSITION_INDEPENDENT_CODE ON)
