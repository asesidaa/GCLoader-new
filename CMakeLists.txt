cmake_minimum_required(VERSION 3.31)
project(GCLoader VERSION 1.0.0 LANGUAGES C CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(ProjectOptions)
include(AsioSdk)
gc_require_asio_sdk()
find_package(Git REQUIRED)
include(Dependencies)

set(GC_DIST_DIR "${CMAKE_BINARY_DIR}/dist")
file(MAKE_DIRECTORY "${GC_DIST_DIR}")
set(GC_LICENSE_DIR "${GC_DIST_DIR}/licenses")
file(MAKE_DIRECTORY "${GC_LICENSE_DIR}")
foreach(license_file IN ITEMS
        LICENSE.md
        LICENSES/CC0-1.0.txt
        LICENSES/GPL-3.0-only.txt
        THIRD_PARTY_NOTICES.md)
    get_filename_component(license_name "${license_file}" NAME)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/${license_file}"
        "${GC_LICENSE_DIR}/${license_name}"
        COPYONLY
    )
endforeach()
configure_file(
    "${GC_ASIO_SDK_DIR}/LICENSE.txt"
    "${GC_LICENSE_DIR}/ASIO-SDK-LICENSE.txt"
    COPYONLY
)
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
