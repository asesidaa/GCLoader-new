# SPDX-License-Identifier: CC0-1.0

cmake_minimum_required(VERSION 3.31)

foreach(required IN ITEMS
        GC_PACKAGE_SCRIPT
        GC_TEST_BINARY_DIR
        GC_TEST_GIT_EXECUTABLE
        GC_TEST_POWERSHELL_EXECUTABLE)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

cmake_path(ABSOLUTE_PATH GC_TEST_BINARY_DIR NORMALIZE
    OUTPUT_VARIABLE test_root)
if(test_root STREQUAL "" OR test_root MATCHES "^[A-Za-z]:/$")
    message(FATAL_ERROR "Refusing unsafe package test root: ${test_root}")
endif()
file(REMOVE_RECURSE "${test_root}")
file(MAKE_DIRECTORY "${test_root}")

set(unicode_extract_script "${test_root}/extract-unicode-zip.ps1")
file(WRITE "${unicode_extract_script}" [=[param(
    [Parameter(Mandatory = $true)]
    [string]$Archive,
    [Parameter(Mandatory = $true)]
    [string]$Destination
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::ExtractToDirectory($Archive, $Destination)
]=])

set(test_commit 0123456789abcdef0123456789abcdef01234567)

function(write_project_archive fixture_root archive_path)
    set(project_root "${fixture_root}/project")
    file(MAKE_DIRECTORY
        "${project_root}/cmake"
        "${project_root}/LICENSES")
    file(WRITE "${project_root}/CMakeLists.txt"
        "cmake_minimum_required(VERSION 3.31)\nproject(Fixture)\n")
    file(WRITE "${project_root}/CMakePresets.json" "{}\n")
    file(WRITE "${project_root}/cmake/Dependencies.cmake" "# fixture\n")
    file(WRITE "${project_root}/config.toml" "[fixture]\n")
    file(WRITE "${project_root}/LICENSE.md" "fixture scope\n")
    file(WRITE "${project_root}/LICENSES/CC0-1.0.txt" "fixture CC0\n")
    file(WRITE "${project_root}/LICENSES/GPL-3.0-only.txt" "fixture GPL\n")
    file(WRITE "${project_root}/THIRD_PARTY_NOTICES.md" "fixture notices\n")
    file(WRITE "${project_root}/SOURCE-OFFER.md" "fixture source offer\n")
    file(WRITE "${project_root}/README.md" "fixture project source\n")

    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E tar cf "${archive_path}"
            --format=zip project
        WORKING_DIRECTORY "${fixture_root}"
        RESULT_VARIABLE archive_result
        OUTPUT_VARIABLE archive_output
        ERROR_VARIABLE archive_error
    )
    if(NOT archive_result EQUAL 0)
        message(FATAL_ERROR
            "Could not create project fixture: ${archive_output}${archive_error}")
    endif()
endfunction()

function(run_git_packager case_name project_root sdk_root inputs_path)
    foreach(git_arguments IN ITEMS
            "init"
            "config;user.name;Corresponding Source Test"
            "config;user.email;corresponding-source@example.invalid"
            "add;."
            "commit;-m;fixture")
        execute_process(
            COMMAND "${GC_TEST_GIT_EXECUTABLE}" ${git_arguments}
            WORKING_DIRECTORY "${project_root}"
            RESULT_VARIABLE git_result
            OUTPUT_VARIABLE git_output
            ERROR_VARIABLE git_error
        )
        if(NOT git_result EQUAL 0)
            message(FATAL_ERROR
                "Could not prepare Git fixture: ${git_output}${git_error}")
        endif()
    endforeach()
    execute_process(
        COMMAND "${GC_TEST_GIT_EXECUTABLE}" rev-parse HEAD
        WORKING_DIRECTORY "${project_root}"
        RESULT_VARIABLE revision_result
        OUTPUT_VARIABLE fixture_commit
        ERROR_VARIABLE revision_error
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(NOT revision_result EQUAL 0)
        message(FATAL_ERROR
            "Could not resolve Git fixture: ${revision_error}")
    endif()

    set(case_build "${test_root}/${case_name}/build")
    execute_process(
        COMMAND "${CMAKE_COMMAND}"
            "-DGC_PACKAGE_BUILD_DIR=${case_build}"
            "-DGC_PACKAGE_DIST_DIR=${case_build}/dist"
            "-DGC_PACKAGE_PROJECT_SOURCE_DIR=${project_root}"
            "-DGC_PACKAGE_GIT_EXECUTABLE=${GC_TEST_GIT_EXECUTABLE}"
            "-DGC_PACKAGE_POWERSHELL_EXECUTABLE=${GC_TEST_POWERSHELL_EXECUTABLE}"
            "-DGC_PACKAGE_ASIO_SDK_DIR=${sdk_root}"
            "-DGC_PACKAGE_INPUTS_FILE=${inputs_path}"
            -P "${GC_PACKAGE_SCRIPT}"
        RESULT_VARIABLE package_result
        OUTPUT_VARIABLE package_output
        ERROR_VARIABLE package_error
    )
    if(NOT package_result EQUAL 0)
        message(FATAL_ERROR
            "Git-backed packaging failed: ${package_output}${package_error}")
    endif()
    set(expected_zip
        "${case_build}/source-package/GCLoader-${fixture_commit}-corresponding-source.zip")
    if(NOT EXISTS "${expected_zip}")
        message(FATAL_ERROR
            "Git-backed packaging did not publish ${expected_zip}")
    endif()
endfunction()

function(write_sdk sdk_root with_license)
    file(MAKE_DIRECTORY
        "${sdk_root}/common"
        "${sdk_root}/documentation"
        "${sdk_root}/Steinberg ASIO Logo Artwork")
    file(WRITE "${sdk_root}/README.md" "fixture ASIO SDK\n")
    file(WRITE "${sdk_root}/common/asio.h" "/* fixture SDK header */\n")
    file(WRITE "${sdk_root}/documentation/ASIO SDK.pdf" "fixture PDF\n")
    file(WRITE "${sdk_root}/documentation/ASIO® exact name.pdf"
        "fixture Unicode PDF name\n")
    file(WRITE
        "${sdk_root}/Steinberg ASIO Logo Artwork/ASIO-compatible-logo-Steinberg-TM-BW.png"
        "fixture logo\n")
    if(with_license)
        file(WRITE "${sdk_root}/LICENSE.txt" "fixture SDK license\n")
    endif()
endfunction()

function(write_dependency dependency_root with_license marker)
    file(MAKE_DIRECTORY "${dependency_root}/include")
    file(WRITE "${dependency_root}/include/${marker}.h"
        "/* ${marker} source */\n")
    if(with_license)
        file(WRITE "${dependency_root}/LICENSE.txt"
            "${marker} fixture license\n")
    endif()
endfunction()

function(hash_fixture_tree tree_root output_variable)
    file(GLOB_RECURSE tree_files
        LIST_DIRECTORIES FALSE
        RELATIVE "${tree_root}"
        "${tree_root}/*")
    list(FILTER tree_files EXCLUDE REGEX "(^|/)\\.git($|/)")
    list(SORT tree_files)
    set(tree_inventory "")
    foreach(relative_path IN LISTS tree_files)
        file(SHA256 "${tree_root}/${relative_path}" file_hash)
        string(REPLACE "\\" "/" portable_path "${relative_path}")
        string(APPEND tree_inventory "${file_hash}  ${portable_path}\n")
    endforeach()
    string(SHA256 tree_hash "${tree_inventory}")
    set(${output_variable} "${tree_hash}" PARENT_SCOPE)
endfunction()

function(write_inputs inputs_path sdk_source first_root second_root)
    hash_fixture_tree("${sdk_source}" sdk_hash)
    hash_fixture_tree("${first_root}" first_hash)
    if(IS_DIRECTORY "${second_root}")
        hash_fixture_tree("${second_root}" second_hash)
    else()
        string(REPEAT 0 64 second_hash)
    endif()
    file(WRITE "${inputs_path}"
        "set(GC_PACKAGE_ASIO_SDK_SHA256 [==[${sdk_hash}]==])\n"
        "set(GC_PACKAGE_DEPENDENCY_NAMES dep_one dep_two)\n"
        "set(GC_PACKAGE_DEPENDENCY_dep_one_SOURCE_DIR [==[${first_root}]==])\n"
        "set(GC_PACKAGE_DEPENDENCY_dep_one_ORIGIN [==[https://example.invalid/one.git]==])\n"
        "set(GC_PACKAGE_DEPENDENCY_dep_one_REVISION [==[one-tag]==])\n"
        "set(GC_PACKAGE_DEPENDENCY_dep_one_SHA256 [==[${first_hash}]==])\n"
        "set(GC_PACKAGE_DEPENDENCY_dep_two_SOURCE_DIR [==[${second_root}]==])\n"
        "set(GC_PACKAGE_DEPENDENCY_dep_two_ORIGIN [==[https://example.invalid/two.zip]==])\n"
        "set(GC_PACKAGE_DEPENDENCY_dep_two_REVISION [==[two-tag]==])\n"
        "set(GC_PACKAGE_DEPENDENCY_dep_two_SHA256 [==[${second_hash}]==])\n"
        "set(GC_PACKAGE_GENERATOR [==[Ninja]==])\n"
        "set(GC_PACKAGE_BUILD_TYPE [==[RelWithDebInfo]==])\n"
        "set(GC_PACKAGE_C_COMPILER [==[cl]==])\n"
        "set(GC_PACKAGE_CXX_COMPILER [==[cl]==])\n")
endfunction()

function(run_packager case_name archive_path sdk_root inputs_path dirty)
    set(options EXPECT_SUCCESS)
    set(one_value EXPECTED_ARCHIVE_SHA256)
    cmake_parse_arguments(ARG "${options}" "${one_value}" "" ${ARGN})

    set(case_build "${test_root}/${case_name}/build")
    set(case_dist "${case_build}/dist")
    set(command
        "${CMAKE_COMMAND}"
        "-DGC_PACKAGE_BUILD_DIR=${case_build}"
        "-DGC_PACKAGE_DIST_DIR=${case_dist}"
        "-DGC_PACKAGE_PROJECT_ARCHIVE=${archive_path}"
        "-DGC_PACKAGE_PROJECT_COMMIT=${test_commit}"
        "-DGC_PACKAGE_DIRTY=${dirty}"
        "-DGC_PACKAGE_POWERSHELL_EXECUTABLE=${GC_TEST_POWERSHELL_EXECUTABLE}"
        "-DGC_PACKAGE_ASIO_SDK_DIR=${sdk_root}"
        "-DGC_PACKAGE_INPUTS_FILE=${inputs_path}")
    if(NOT "${ARG_EXPECTED_ARCHIVE_SHA256}" STREQUAL "")
        list(APPEND command
            "-DGC_PACKAGE_EXPECTED_ARCHIVE_SHA256=${ARG_EXPECTED_ARCHIVE_SHA256}")
    endif()
    list(APPEND command -P "${GC_PACKAGE_SCRIPT}")

    execute_process(
        COMMAND ${command}
        RESULT_VARIABLE package_result
        OUTPUT_VARIABLE package_output
        ERROR_VARIABLE package_error
    )
    set(package_log "${package_output}${package_error}")

    if(ARG_EXPECT_SUCCESS)
        if(NOT package_result EQUAL 0)
            message(FATAL_ERROR
                "${case_name} unexpectedly failed:\n${package_log}")
        endif()
        set(${case_name}_ZIP
            "${case_build}/source-package/GCLoader-${test_commit}-corresponding-source.zip"
            PARENT_SCOPE)
        set(${case_name}_DIST_ZIP
            "${case_dist}/GCLoader-${test_commit}-corresponding-source.zip"
            PARENT_SCOPE)
        return()
    endif()

    if(package_result EQUAL 0)
        message(FATAL_ERROR "${case_name} unexpectedly succeeded")
    endif()
    file(GLOB_RECURSE partial_archives
        "${case_build}/source-package/*.zip"
        "${case_build}/source-package/*.tmp")
    if(partial_archives)
        message(FATAL_ERROR
            "${case_name} published partial archives: ${partial_archives}")
    endif()
endfunction()

set(fixture_root "${test_root}/fixture")
file(MAKE_DIRECTORY "${fixture_root}")
set(project_archive "${fixture_root}/project.zip")
write_project_archive("${fixture_root}" "${project_archive}")

set(sdk_root "${fixture_root}/ASIOSDK")
write_sdk("${sdk_root}" TRUE)
set(dep_one_root "${fixture_root}/dependencies/dep-one")
set(dep_two_root "${fixture_root}/dependencies/dep-two")
write_dependency("${dep_one_root}" TRUE dep_one)
write_dependency("${dep_two_root}" TRUE dep_two)
set(inputs_path "${fixture_root}/inputs.cmake")
write_inputs("${inputs_path}" "${sdk_root}" "${dep_one_root}" "${dep_two_root}")

set(stale_dist_zip
    "${test_root}/success/build/dist/GCLoader-0000000000000000000000000000000000000000-corresponding-source.zip")
file(MAKE_DIRECTORY "${test_root}/success/build/dist")
file(WRITE "${stale_dist_zip}" "stale package\n")
run_packager(success "${project_archive}" "${sdk_root}" "${inputs_path}" OFF
    EXPECT_SUCCESS)
if(EXISTS "${stale_dist_zip}")
    message(FATAL_ERROR "Successful packaging retained a stale dist ZIP")
endif()
run_git_packager(git_success "${fixture_root}/project"
    "${sdk_root}" "${inputs_path}")

foreach(archive IN ITEMS "${success_ZIP}" "${success_DIST_ZIP}")
    if(NOT EXISTS "${archive}")
        message(FATAL_ERROR "Expected package was not published: ${archive}")
    endif()
endforeach()
file(SHA256 "${success_ZIP}" source_hash)
file(SHA256 "${success_DIST_ZIP}" dist_hash)
if(NOT source_hash STREQUAL dist_hash)
    message(FATAL_ERROR "Distribution ZIP is not the verified source ZIP")
endif()

set(extract_root "${test_root}/verified-package")
file(MAKE_DIRECTORY "${extract_root}")
execute_process(
    COMMAND "${GC_TEST_POWERSHELL_EXECUTABLE}"
        -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass
        -File "${unicode_extract_script}"
        -Archive "${success_ZIP}"
        -Destination "${extract_root}"
    RESULT_VARIABLE extract_result
    OUTPUT_VARIABLE extract_output
    ERROR_VARIABLE extract_error
)
if(NOT extract_result EQUAL 0)
    message(FATAL_ERROR
        "Could not extract verified package: ${extract_output}${extract_error}")
endif()
set(package_root
    "${extract_root}/GCLoader-${test_commit}-corresponding-source")
foreach(required IN ITEMS
        project/CMakeLists.txt
        project/CMakePresets.json
        project/cmake/Dependencies.cmake
        project/config.toml
        project/LICENSE.md
        project/LICENSES/CC0-1.0.txt
        project/LICENSES/GPL-3.0-only.txt
        project/THIRD_PARTY_NOTICES.md
        project/SOURCE-OFFER.md
        third_party/asiosdk/LICENSE.txt
        "third_party/asiosdk/documentation/ASIO SDK.pdf"
        "third_party/asiosdk/documentation/ASIO® exact name.pdf"
        third_party/fetchcontent/dep_one/LICENSE.txt
        third_party/fetchcontent/dep_one/include/dep_one.h
        third_party/fetchcontent/dep_two/LICENSE.txt
        third_party/fetchcontent/dep_two/include/dep_two.h
        build-metadata/dependencies.txt
        build-metadata/toolchain.txt
        corresponding-source-manifest.txt
        configure-offline.ps1)
    if(NOT EXISTS "${package_root}/${required}")
        message(FATAL_ERROR "Package is missing ${required}")
    endif()
endforeach()

file(READ "${package_root}/corresponding-source-manifest.txt" manifest)
foreach(fragment IN ITEMS
        "commit=${test_commit}"
        "project/CMakeLists.txt"
        "third_party/asiosdk/LICENSE.txt"
        "third_party/fetchcontent/dep_one/LICENSE.txt")
    string(FIND "${manifest}" "${fragment}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "Manifest is missing ${fragment}")
    endif()
endforeach()

file(READ "${package_root}/build-metadata/dependencies.txt" dependencies)
foreach(fragment IN ITEMS
        "name=dep_one"
        "origin=https://example.invalid/one.git"
        "revision=one-tag"
        "source_sha256=")
    string(FIND "${dependencies}" "${fragment}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "Dependency metadata is missing ${fragment}")
    endif()
endforeach()

file(READ "${package_root}/configure-offline.ps1" offline_script)
foreach(fragment IN ITEMS
        "FETCHCONTENT_FULLY_DISCONNECTED=ON"
        "[IO.Path]::GetTempPath()"
        "ItemType Junction"
        "$SourceRoot"
        "FETCHCONTENT_SOURCE_DIR_DEP_ONE"
        "FETCHCONTENT_SOURCE_DIR_DEP_TWO"
        "third_party/asiosdk"
        "iDmacDrv32"
        "ConfigGUI"
        "AsioProbe")
    string(FIND "${offline_script}" "${fragment}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "Offline script is missing ${fragment}")
    endif()
endforeach()

run_packager(dirty "${project_archive}" "${sdk_root}" "${inputs_path}" ON)
run_packager(missing_sdk "${project_archive}"
    "${fixture_root}/does-not-exist" "${inputs_path}" OFF)

set(unlicensed_sdk "${fixture_root}/unlicensed-sdk")
write_sdk("${unlicensed_sdk}" FALSE)
run_packager(missing_sdk_license "${project_archive}"
    "${unlicensed_sdk}" "${inputs_path}" OFF)

set(missing_dep_inputs "${fixture_root}/missing-dependency-inputs.cmake")
write_inputs("${missing_dep_inputs}" "${sdk_root}" "${dep_one_root}"
    "${fixture_root}/missing-dependency")
run_packager(missing_dependency "${project_archive}"
    "${sdk_root}" "${missing_dep_inputs}" OFF)

set(unlicensed_dep "${fixture_root}/unlicensed-dependency")
write_dependency("${unlicensed_dep}" FALSE unlicensed)
set(unlicensed_dep_inputs "${fixture_root}/unlicensed-dependency-inputs.cmake")
write_inputs("${unlicensed_dep_inputs}" "${sdk_root}"
    "${dep_one_root}" "${unlicensed_dep}")
run_packager(missing_dependency_license "${project_archive}"
    "${sdk_root}" "${unlicensed_dep_inputs}" OFF)

set(drift_dep "${fixture_root}/drift-dependency")
write_dependency("${drift_dep}" TRUE drift)
set(drift_inputs "${fixture_root}/drift-inputs.cmake")
write_inputs("${drift_inputs}" "${sdk_root}" "${dep_one_root}" "${drift_dep}")
file(APPEND "${drift_dep}/include/drift.h" "changed after configure\n")
run_packager(dependency_drift "${project_archive}"
    "${sdk_root}" "${drift_inputs}" OFF)

set(corrupt_archive "${fixture_root}/corrupt.zip")
file(WRITE "${corrupt_archive}" "not an archive\n")
run_packager(archive_error "${corrupt_archive}"
    "${sdk_root}" "${inputs_path}" OFF)

string(REPEAT 0 64 incorrect_hash)
run_packager(hash_error "${project_archive}"
    "${sdk_root}" "${inputs_path}" OFF
    EXPECTED_ARCHIVE_SHA256 "${incorrect_hash}")

message(STATUS "Corresponding-source package contract passed")
