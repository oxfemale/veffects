include(FindPackageHandleStandardArgs)

find_path(DRLIBS_INCLUDE_DIR
  NAMES dr_mp3.h
  PATHS
    "${CMAKE_CURRENT_LIST_DIR}/../third_party"
    "${PROJECT_SOURCE_DIR}/third_party"
)

find_package_handle_standard_args(DrLibs DEFAULT_MSG DRLIBS_INCLUDE_DIR)
mark_as_advanced(DRLIBS_INCLUDE_DIR)
