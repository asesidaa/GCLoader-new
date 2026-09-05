cmake_minimum_required(VERSION 3.31)
project(GCLoader VERSION 1.0.0 LANGUAGES C CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(ProjectOptions)
include(CTest)
include(AsioSdk)
gc_require_asio_sdk()
find_package(Git REQUIRED)
find_program(GC_POWERSHELL_EXECUTABLE
        NAMES powershell.exe pwsh.exe
        REQUIRED)
include(Dependencies)

# Keep this list synchronized with every FetchContent source used by the build,
# including transitive sources such as SafetyHook's Zydis checkout. The
# configure-time directory audit below catches an unregistered default checkout.
set(GC_CORRESPONDING_SOURCE_DEPENDENCIES
        miniaudio
        tomlplusplus
        safetyhook
        reflectcpp
        plog
        imgui_external
        zydis)
set(GC_PACKAGE_ORIGIN_miniaudio
        "https://github.com/mackron/miniaudio.git")
set(GC_PACKAGE_REVISION_miniaudio
        "9634bedb5b5a2ca38c1ee7108a9358a4e233f14d")
set(GC_PACKAGE_ORIGIN_tomlplusplus
        "https://github.com/marzer/tomlplusplus.git")
set(GC_PACKAGE_REVISION_tomlplusplus "v3.4.0")
set(GC_PACKAGE_ORIGIN_safetyhook
        "https://github.com/cursey/safetyhook.git")
set(GC_PACKAGE_REVISION_safetyhook "v0.7.0")
set(GC_PACKAGE_ORIGIN_reflectcpp
        "https://github.com/getml/reflect-cpp/archive/refs/tags/v0.25.0.zip")
set(GC_PACKAGE_REVISION_reflectcpp "v0.25.0")
set(GC_PACKAGE_ORIGIN_plog
        "https://github.com/SergiusTheBest/plog")
set(GC_PACKAGE_REVISION_plog "1.1.11")
set(GC_PACKAGE_ORIGIN_imgui_external
        "https://github.com/ocornut/imgui/archive/refs/tags/v1.92.8.tar.gz")
set(GC_PACKAGE_REVISION_imgui_external "v1.92.8")
set(GC_PACKAGE_ORIGIN_zydis
        "https://github.com/zyantific/zydis.git")
set(GC_PACKAGE_REVISION_zydis "v4.1.0")

function(gc_hash_corresponding_source_tree tree_root output_variable)
    set(options EXCLUDE_GIT)
    cmake_parse_arguments(ARG "${options}" "" "" ${ARGN})
    file(GLOB_RECURSE tree_files
            LIST_DIRECTORIES FALSE
            RELATIVE "${tree_root}"
            "${tree_root}/*")
    if (ARG_EXCLUDE_GIT)
        list(FILTER tree_files EXCLUDE REGEX "(^|/)\\.git($|/)")
    endif ()
    list(SORT tree_files)
    if (NOT tree_files)
        message(FATAL_ERROR
                "Cannot snapshot empty source tree: ${tree_root}")
    endif ()
    set(tree_inventory "")
    foreach (relative_path IN LISTS tree_files)
        file(SHA256 "${tree_root}/${relative_path}" file_hash)
        string(REPLACE "\\" "/" portable_path "${relative_path}")
        string(APPEND tree_inventory
                "${file_hash}  ${portable_path}\n")
    endforeach ()
    string(SHA256 tree_hash "${tree_inventory}")
    set(${output_variable} "${tree_hash}" PARENT_SCOPE)
endfunction()

gc_hash_corresponding_source_tree(
        "${GC_ASIO_SDK_DIR}" GC_PACKAGE_ASIO_SDK_SHA256)

set(GC_PACKAGE_INPUTS_FILE
        "${CMAKE_BINARY_DIR}/source-package/corresponding-source-inputs.cmake")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/source-package")
file(WRITE "${GC_PACKAGE_INPUTS_FILE}"
        "# Generated from the exact configured dependency graph.\n"
        "set(GC_PACKAGE_ASIO_SDK_SHA256 [==[${GC_PACKAGE_ASIO_SDK_SHA256}]==])\n"
        "set(GC_PACKAGE_DEPENDENCY_NAMES")
foreach (dependency IN LISTS GC_CORRESPONDING_SOURCE_DEPENDENCIES)
    string(TOLOWER "${dependency}" dependency_lower)
    FetchContent_GetProperties(${dependency})
    if (NOT ${dependency_lower}_POPULATED OR
            NOT IS_DIRECTORY "${${dependency_lower}_SOURCE_DIR}")
        message(FATAL_ERROR
                "Corresponding-source input ${dependency} was not populated")
    endif ()
    file(APPEND "${GC_PACKAGE_INPUTS_FILE}" " ${dependency_lower}")
endforeach ()
file(APPEND "${GC_PACKAGE_INPUTS_FILE}" ")\n")

foreach (dependency IN LISTS GC_CORRESPONDING_SOURCE_DEPENDENCIES)
    string(TOLOWER "${dependency}" dependency_lower)
    gc_hash_corresponding_source_tree(
            "${${dependency_lower}_SOURCE_DIR}"
            dependency_source_sha256
            EXCLUDE_GIT)
    file(APPEND "${GC_PACKAGE_INPUTS_FILE}"
            "set(GC_PACKAGE_DEPENDENCY_${dependency_lower}_SOURCE_DIR [==[${${dependency_lower}_SOURCE_DIR}]==])\n"
            "set(GC_PACKAGE_DEPENDENCY_${dependency_lower}_ORIGIN [==[${GC_PACKAGE_ORIGIN_${dependency_lower}}]==])\n"
            "set(GC_PACKAGE_DEPENDENCY_${dependency_lower}_REVISION [==[${GC_PACKAGE_REVISION_${dependency_lower}}]==])\n"
            "set(GC_PACKAGE_DEPENDENCY_${dependency_lower}_SHA256 [==[${dependency_source_sha256}]==])\n")
endforeach ()
file(APPEND "${GC_PACKAGE_INPUTS_FILE}"
        "set(GC_PACKAGE_GENERATOR [==[${CMAKE_GENERATOR}]==])\n"
        "set(GC_PACKAGE_BUILD_TYPE [==[${CMAKE_BUILD_TYPE}]==])\n"
        "set(GC_PACKAGE_C_COMPILER [==[${CMAKE_C_COMPILER}]==])\n"
        "set(GC_PACKAGE_CXX_COMPILER [==[${CMAKE_CXX_COMPILER}]==])\n")

if (DEFINED FETCHCONTENT_BASE_DIR)
    file(GLOB populated_fetchcontent_sources
            LIST_DIRECTORIES TRUE
            "${FETCHCONTENT_BASE_DIR}/*-src")
    foreach (populated_source IN LISTS populated_fetchcontent_sources)
        get_filename_component(populated_name "${populated_source}" NAME)
        string(REGEX REPLACE "-src$" "" populated_name "${populated_name}")
        # Reused preset trees can retain checkouts from retired dependencies.
        # Audit only content populated by this configure run, not stale caches.
        FetchContent_GetProperties(${populated_name} POPULATED dependency_is_active)
        if (dependency_is_active AND
                NOT populated_name IN_LIST GC_CORRESPONDING_SOURCE_DEPENDENCIES)
            message(FATAL_ERROR
                    "FetchContent source ${populated_name} is missing from "
                    "GC_CORRESPONDING_SOURCE_DEPENDENCIES")
        endif ()
    endforeach ()
endif ()

set(GC_DIST_DIR "${CMAKE_BINARY_DIR}/dist")
file(MAKE_DIRECTORY "${GC_DIST_DIR}")

# Remove only retired project-owned outputs so an existing build tree cannot
# silently preserve files that no longer belong in the deployable directory.
file(REMOVE
        "${GC_DIST_DIR}/AsioProbe.exe"
        "${GC_DIST_DIR}/ASIO-compatible-logo-Steinberg-TM-BW.png"
        "${GC_DIST_DIR}/imgui.ini")
file(REMOVE_RECURSE "${GC_DIST_DIR}/licenses")
file(GLOB stale_corresponding_source_archives
        LIST_DIRECTORIES FALSE
        "${GC_DIST_DIR}/GCLoader-*-corresponding-source.zip")
if (stale_corresponding_source_archives)
    file(REMOVE ${stale_corresponding_source_archives})
endif ()

add_custom_target(gc-package-corresponding-source
        COMMAND "${CMAKE_COMMAND}"
        "-DGC_PACKAGE_BUILD_DIR=${CMAKE_BINARY_DIR}"
        "-DGC_PACKAGE_PROJECT_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
        "-DGC_PACKAGE_GIT_EXECUTABLE=${GIT_EXECUTABLE}"
        "-DGC_PACKAGE_POWERSHELL_EXECUTABLE=${GC_POWERSHELL_EXECUTABLE}"
        "-DGC_PACKAGE_ASIO_SDK_DIR=${GC_ASIO_SDK_DIR}"
        "-DGC_PACKAGE_INPUTS_FILE=${GC_PACKAGE_INPUTS_FILE}"
        -P "${CMAKE_SOURCE_DIR}/cmake/PackageCorrespondingSource.cmake"
        COMMENT "Packaging exact GPL corresponding source from a clean revision"
        USES_TERMINAL
        VERBATIM
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

add_subdirectory(src)
if (BUILD_TESTING)
    add_subdirectory(tests)
endif ()
add_subdirectory(tools)
