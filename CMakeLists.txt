cmake_minimum_required(VERSION 3.31)
project(GCLoader VERSION 1.0.0 LANGUAGES C CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(ProjectOptions)
find_package(Git REQUIRED)
include(Dependencies)

set(GC_DIST_DIR "${CMAKE_BINARY_DIR}/dist")
file(MAKE_DIRECTORY "${GC_DIST_DIR}")
configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/config.toml"
        "${GC_DIST_DIR}/config.toml"
        COPYONLY
)
configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/card.txt"
        "${GC_DIST_DIR}/card.txt"
        COPYONLY
)

enable_testing()
add_subdirectory(src)
add_subdirectory(tools)
add_subdirectory(tests)
