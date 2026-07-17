cmake_minimum_required(VERSION 3.31)
project(GCLoader VERSION 1.0.0 LANGUAGES C CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(ProjectOptions)
find_package(Git REQUIRED)
include(Dependencies)

enable_testing()
add_subdirectory(src)
add_subdirectory(tools)
add_subdirectory(tests)
