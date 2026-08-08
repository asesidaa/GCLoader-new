# SPDX-License-Identifier: CC0-1.0

cmake_minimum_required(VERSION 3.31)

function(gc_require_value variable_name)
    if(NOT DEFINED ${variable_name} OR "${${variable_name}}" STREQUAL "")
        message(FATAL_ERROR "${variable_name} is required")
    endif()
endfunction()

function(gc_hash_tree tree_root output_variable)
    set(options EXCLUDE_GIT)
    cmake_parse_arguments(ARG "${options}" "" "" ${ARGN})
    file(GLOB_RECURSE tree_files
        LIST_DIRECTORIES FALSE
        RELATIVE "${tree_root}"
        "${tree_root}/*")
    if(ARG_EXCLUDE_GIT)
        list(FILTER tree_files EXCLUDE REGEX "(^|/)\\.git($|/)")
    endif()
    list(SORT tree_files)
    if(NOT tree_files)
        message(FATAL_ERROR "Cannot hash empty source tree: ${tree_root}")
    endif()

    set(tree_inventory "")
    foreach(relative_path IN LISTS tree_files)
        file(SHA256 "${tree_root}/${relative_path}" file_hash)
        string(REPLACE "\\" "/" portable_path "${relative_path}")
        string(APPEND tree_inventory "${file_hash}  ${portable_path}\n")
    endforeach()
    string(SHA256 tree_hash "${tree_inventory}")
    set(${output_variable} "${tree_hash}" PARENT_SCOPE)
endfunction()

function(gc_find_license source_root output_variable)
    file(GLOB license_candidates
        LIST_DIRECTORIES FALSE
        "${source_root}/LICENSE*"
        "${source_root}/COPYING*"
        "${source_root}/NOTICE*"
        "${source_root}/COPYRIGHT*")
    list(SORT license_candidates)
    set(${output_variable} "${license_candidates}" PARENT_SCOPE)
endfunction()

foreach(required IN ITEMS
        GC_PACKAGE_BUILD_DIR
        GC_PACKAGE_DIST_DIR
        GC_PACKAGE_ASIO_SDK_DIR
        GC_PACKAGE_INPUTS_FILE
        GC_PACKAGE_POWERSHELL_EXECUTABLE)
    gc_require_value(${required})
endforeach()

cmake_path(ABSOLUTE_PATH GC_PACKAGE_BUILD_DIR NORMALIZE
    OUTPUT_VARIABLE package_build_dir)
if(package_build_dir STREQUAL "" OR
        package_build_dir MATCHES "^[A-Za-z]:/$" OR
        package_build_dir STREQUAL "/")
    message(FATAL_ERROR
        "Refusing unsafe corresponding-source build root: ${package_build_dir}")
endif()
cmake_path(ABSOLUTE_PATH GC_PACKAGE_DIST_DIR NORMALIZE
    OUTPUT_VARIABLE package_dist_dir)
cmake_path(ABSOLUTE_PATH GC_PACKAGE_ASIO_SDK_DIR NORMALIZE
    OUTPUT_VARIABLE asio_sdk_dir)
cmake_path(ABSOLUTE_PATH GC_PACKAGE_INPUTS_FILE NORMALIZE
    OUTPUT_VARIABLE inputs_file)

if(NOT EXISTS "${inputs_file}")
    message(FATAL_ERROR "Package inputs file does not exist: ${inputs_file}")
endif()
include("${inputs_file}")

gc_require_value(GC_PACKAGE_ASIO_SDK_SHA256)

if(NOT DEFINED GC_PACKAGE_DEPENDENCY_NAMES OR
        "${GC_PACKAGE_DEPENDENCY_NAMES}" STREQUAL "")
    message(FATAL_ERROR "No configured FetchContent dependencies were supplied")
endif()

if(NOT IS_DIRECTORY "${asio_sdk_dir}")
    message(FATAL_ERROR "ASIO SDK source tree does not exist: ${asio_sdk_dir}")
endif()
foreach(required_sdk_file IN ITEMS
        README.md
        LICENSE.txt
        common/asio.h
        "Steinberg ASIO Logo Artwork/ASIO-compatible-logo-Steinberg-TM-BW.png")
    if(NOT EXISTS "${asio_sdk_dir}/${required_sdk_file}")
        message(FATAL_ERROR
            "ASIO SDK source tree is missing ${required_sdk_file}")
    endif()
endforeach()
gc_hash_tree("${asio_sdk_dir}" current_asio_sdk_hash)
if(NOT "${current_asio_sdk_hash}" STREQUAL
        "${GC_PACKAGE_ASIO_SDK_SHA256}")
    message(FATAL_ERROR
        "ASIO SDK source tree changed after CMake configuration")
endif()

foreach(dependency IN LISTS GC_PACKAGE_DEPENDENCY_NAMES)
    if(NOT dependency MATCHES "^[A-Za-z0-9_][A-Za-z0-9_.+-]*$")
        message(FATAL_ERROR "Unsafe dependency package name: ${dependency}")
    endif()
    set(source_variable "GC_PACKAGE_DEPENDENCY_${dependency}_SOURCE_DIR")
    set(origin_variable "GC_PACKAGE_DEPENDENCY_${dependency}_ORIGIN")
    set(revision_variable "GC_PACKAGE_DEPENDENCY_${dependency}_REVISION")
    set(hash_variable "GC_PACKAGE_DEPENDENCY_${dependency}_SHA256")
    foreach(variable_name IN ITEMS
            "${source_variable}" "${origin_variable}" "${revision_variable}"
            "${hash_variable}")
        gc_require_value(${variable_name})
    endforeach()

    cmake_path(ABSOLUTE_PATH ${source_variable} NORMALIZE
        OUTPUT_VARIABLE dependency_source)
    if(NOT IS_DIRECTORY "${dependency_source}")
        message(FATAL_ERROR
            "Configured dependency source is missing: ${dependency_source}")
    endif()
    gc_find_license("${dependency_source}" dependency_licenses)
    if(NOT dependency_licenses)
        message(FATAL_ERROR
            "Configured dependency ${dependency} has no root license file")
    endif()
    gc_hash_tree("${dependency_source}" current_dependency_hash EXCLUDE_GIT)
    if(NOT "${current_dependency_hash}" STREQUAL "${${hash_variable}}")
        message(FATAL_ERROR
            "Configured dependency ${dependency} changed after CMake configuration")
    endif()
endforeach()

set(package_area "${package_build_dir}/source-package")
file(MAKE_DIRECTORY "${package_area}")

set(project_archive "")
set(project_archive_is_temporary FALSE)
if(DEFINED GC_PACKAGE_PROJECT_ARCHIVE AND
        NOT "${GC_PACKAGE_PROJECT_ARCHIVE}" STREQUAL "")
    if(DEFINED GC_PACKAGE_DIRTY AND GC_PACKAGE_DIRTY)
        message(FATAL_ERROR
            "Corresponding source requires a clean committed project tree")
    endif()
    gc_require_value(GC_PACKAGE_PROJECT_COMMIT)
    cmake_path(ABSOLUTE_PATH GC_PACKAGE_PROJECT_ARCHIVE NORMALIZE
        OUTPUT_VARIABLE project_archive)
    if(NOT EXISTS "${project_archive}")
        message(FATAL_ERROR "Project archive does not exist: ${project_archive}")
    endif()
    set(project_commit "${GC_PACKAGE_PROJECT_COMMIT}")
else()
    foreach(required IN ITEMS
            GC_PACKAGE_PROJECT_SOURCE_DIR
            GC_PACKAGE_GIT_EXECUTABLE)
        gc_require_value(${required})
    endforeach()
    cmake_path(ABSOLUTE_PATH GC_PACKAGE_PROJECT_SOURCE_DIR NORMALIZE
        OUTPUT_VARIABLE project_source_dir)

    execute_process(
        COMMAND "${GC_PACKAGE_GIT_EXECUTABLE}" status
            --porcelain=v1 --untracked-files=all
        WORKING_DIRECTORY "${project_source_dir}"
        RESULT_VARIABLE status_result
        OUTPUT_VARIABLE status_output
        ERROR_VARIABLE status_error
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(NOT status_result EQUAL 0)
        message(FATAL_ERROR "Could not inspect project tree: ${status_error}")
    endif()
    if(NOT status_output STREQUAL "")
        message(FATAL_ERROR
            "Corresponding source requires a clean committed project tree:\n"
            "${status_output}")
    endif()

    execute_process(
        COMMAND "${GC_PACKAGE_GIT_EXECUTABLE}" rev-parse HEAD
        WORKING_DIRECTORY "${project_source_dir}"
        RESULT_VARIABLE revision_result
        OUTPUT_VARIABLE project_commit
        ERROR_VARIABLE revision_error
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(NOT revision_result EQUAL 0)
        message(FATAL_ERROR "Could not resolve project revision: ${revision_error}")
    endif()

    set(project_archive "${package_area}/.project-archive.tmp")
    file(REMOVE "${project_archive}")
    execute_process(
        COMMAND "${GC_PACKAGE_GIT_EXECUTABLE}" archive
            --format=zip --prefix=project/
            "--output=${project_archive}" HEAD
        WORKING_DIRECTORY "${project_source_dir}"
        RESULT_VARIABLE archive_result
        OUTPUT_VARIABLE archive_output
        ERROR_VARIABLE archive_error
    )
    if(NOT archive_result EQUAL 0 OR NOT EXISTS "${project_archive}")
        file(REMOVE "${project_archive}")
        message(FATAL_ERROR
            "Could not archive project revision: ${archive_output}${archive_error}")
    endif()
    set(project_archive_is_temporary TRUE)
endif()

string(LENGTH "${project_commit}" project_commit_length)
if(NOT project_commit_length EQUAL 40 OR
        NOT project_commit MATCHES "^[0-9A-Fa-f]+$")
    if(project_archive_is_temporary)
        file(REMOVE "${project_archive}")
    endif()
    message(FATAL_ERROR "Project commit must be a full 40-digit Git object ID")
endif()
string(TOLOWER "${project_commit}" project_commit)

set(package_name "GCLoader-${project_commit}-corresponding-source")
set(staging_root "${package_area}/.staging-${project_commit}")
set(package_root "${staging_root}/${package_name}")
set(temporary_zip "${package_area}/.${package_name}.zip.tmp")
set(final_zip "${package_area}/${package_name}.zip")
set(dist_temporary_zip "${package_dist_dir}/.${package_name}.zip.tmp")
set(dist_final_zip "${package_dist_dir}/${package_name}.zip")

file(REMOVE_RECURSE "${staging_root}")
file(REMOVE "${temporary_zip}" "${dist_temporary_zip}")
file(MAKE_DIRECTORY "${package_root}")

file(ARCHIVE_EXTRACT
    INPUT "${project_archive}"
    DESTINATION "${package_root}")
if(project_archive_is_temporary)
    file(REMOVE "${project_archive}")
endif()

foreach(required_project_path IN ITEMS
        project/CMakeLists.txt
        project/CMakePresets.json
        project/cmake
        project/config.toml
        project/LICENSE.md
        project/LICENSES/CC0-1.0.txt
        project/LICENSES/GPL-3.0-only.txt
        project/THIRD_PARTY_NOTICES.md
        project/SOURCE-OFFER.md)
    if(NOT EXISTS "${package_root}/${required_project_path}")
        message(FATAL_ERROR
            "Project archive is missing ${required_project_path}")
    endif()
endforeach()

set(asio_destination "${package_root}/third_party/asiosdk")
file(MAKE_DIRECTORY "${asio_destination}")
file(COPY "${asio_sdk_dir}/" DESTINATION "${asio_destination}")
gc_hash_tree("${asio_destination}" asio_tree_hash)
if(NOT "${asio_tree_hash}" STREQUAL "${GC_PACKAGE_ASIO_SDK_SHA256}")
    message(FATAL_ERROR "Copied ASIO SDK tree failed source-hash verification")
endif()

set(fetchcontent_root "${package_root}/third_party/fetchcontent")
set(metadata_root "${package_root}/build-metadata")
file(MAKE_DIRECTORY "${fetchcontent_root}" "${metadata_root}")

file(WRITE "${metadata_root}/dependencies.txt"
    "GCLoader corresponding-source dependency inputs\n"
    "project_commit=${project_commit}\n"
    "asio_sdk_source_sha256=${asio_tree_hash}\n\n")
file(WRITE "${metadata_root}/fetchcontent-overrides.cmake"
    "# Generated matching-source cache entries.\n"
    "set(FETCHCONTENT_FULLY_DISCONNECTED ON CACHE BOOL \"\" FORCE)\n"
    "set(GC_ASIO_SDK_DIR \"\${CMAKE_CURRENT_LIST_DIR}/../third_party/asiosdk\" CACHE PATH \"\" FORCE)\n")

set(offline_overrides "")
foreach(dependency IN LISTS GC_PACKAGE_DEPENDENCY_NAMES)
    set(source_variable "GC_PACKAGE_DEPENDENCY_${dependency}_SOURCE_DIR")
    set(origin_variable "GC_PACKAGE_DEPENDENCY_${dependency}_ORIGIN")
    set(revision_variable "GC_PACKAGE_DEPENDENCY_${dependency}_REVISION")
    cmake_path(ABSOLUTE_PATH ${source_variable} NORMALIZE
        OUTPUT_VARIABLE dependency_source)
    set(dependency_destination "${fetchcontent_root}/${dependency}")
    file(MAKE_DIRECTORY "${dependency_destination}")
    file(COPY "${dependency_source}/"
        DESTINATION "${dependency_destination}"
        PATTERN ".git" EXCLUDE)
    gc_hash_tree("${dependency_destination}" dependency_tree_hash)
    set(hash_variable "GC_PACKAGE_DEPENDENCY_${dependency}_SHA256")
    if(NOT "${dependency_tree_hash}" STREQUAL "${${hash_variable}}")
        message(FATAL_ERROR
            "Copied dependency ${dependency} failed source-hash verification")
    endif()

    set(resolved_commit "not-a-git-checkout")
    if(EXISTS "${dependency_source}/.git")
        execute_process(
            COMMAND "${GC_PACKAGE_GIT_EXECUTABLE}" -C
                "${dependency_source}" rev-parse HEAD
            RESULT_VARIABLE dependency_revision_result
            OUTPUT_VARIABLE dependency_resolved_commit
            ERROR_QUIET
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        if(dependency_revision_result EQUAL 0)
            set(resolved_commit "${dependency_resolved_commit}")
        endif()
    endif()

    file(APPEND "${metadata_root}/dependencies.txt"
        "[${dependency}]\n"
        "name=${dependency}\n"
        "origin=${${origin_variable}}\n"
        "revision=${${revision_variable}}\n"
        "resolved_commit=${resolved_commit}\n"
        "source_sha256=${dependency_tree_hash}\n"
        "path=third_party/fetchcontent/${dependency}\n\n")

    string(TOUPPER "${dependency}" dependency_upper)
    file(APPEND "${metadata_root}/fetchcontent-overrides.cmake"
        "set(FETCHCONTENT_SOURCE_DIR_${dependency_upper} \"\${CMAKE_CURRENT_LIST_DIR}/../third_party/fetchcontent/${dependency}\" CACHE PATH \"\" FORCE)\n")
    string(APPEND offline_overrides
        "    ('-DFETCHCONTENT_SOURCE_DIR_${dependency_upper}=' + (Join-Path $PSScriptRoot 'third_party/fetchcontent/${dependency}'))\n")
endforeach()

foreach(toolchain_variable IN ITEMS
        GC_PACKAGE_GENERATOR
        GC_PACKAGE_BUILD_TYPE
        GC_PACKAGE_C_COMPILER
        GC_PACKAGE_CXX_COMPILER)
    if(NOT DEFINED ${toolchain_variable})
        set(${toolchain_variable} "")
    endif()
endforeach()
file(WRITE "${metadata_root}/toolchain.txt"
    "GCLoader matching-source build metadata\n"
    "project_commit=${project_commit}\n"
    "generator=${GC_PACKAGE_GENERATOR}\n"
    "build_type=${GC_PACKAGE_BUILD_TYPE}\n"
    "c_compiler=${GC_PACKAGE_C_COMPILER}\n"
    "cxx_compiler=${GC_PACKAGE_CXX_COMPILER}\n"
    "architecture=Win32 x86 (run from vcvars32.bat)\n"
    "configure=./configure-offline.ps1\n"
    "build=cmake --build build-offline --target iDmacDrv32 ConfigGUI AsioProbe\n"
    "dependency_overrides=build-metadata/fetchcontent-overrides.cmake\n")

set(offline_script [=[# SPDX-License-Identifier: CC0-1.0
param(
    [string]$BuildDirectory = (Join-Path $PSScriptRoot 'build-offline'),
    [ValidateSet('Debug', 'RelWithDebInfo', 'Release')]
    [string]$BuildType = 'RelWithDebInfo'
)

$ErrorActionPreference = 'Stop'
$configureArguments = @(
    '-S'
    (Join-Path $PSScriptRoot 'project')
    '-B'
    $BuildDirectory
    '-G'
    ']=])
string(APPEND offline_script "${GC_PACKAGE_GENERATOR}'\n")
string(APPEND offline_script [=[    ('-DCMAKE_BUILD_TYPE=' + $BuildType)
    ('-DGC_ASIO_SDK_DIR=' + (Join-Path $PSScriptRoot 'third_party/asiosdk'))
    '-DFETCHCONTENT_FULLY_DISCONNECTED=ON'
]=])
string(APPEND offline_script "${offline_overrides}")
string(APPEND offline_script [=[)

& cmake @configureArguments
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

& cmake --build $BuildDirectory --target iDmacDrv32 ConfigGUI AsioProbe
exit $LASTEXITCODE
]=])
file(WRITE "${package_root}/configure-offline.ps1" "${offline_script}")

file(GLOB_RECURSE manifest_files
    LIST_DIRECTORIES FALSE
    RELATIVE "${package_root}"
    "${package_root}/*")
list(SORT manifest_files)
file(WRITE "${package_root}/corresponding-source-manifest.txt"
    "GCLoader corresponding source\n"
    "commit=${project_commit}\n"
    "inventory=SHA-256\n\n")
foreach(relative_path IN LISTS manifest_files)
    file(SHA256 "${package_root}/${relative_path}" file_hash)
    string(REPLACE "\\" "/" portable_path "${relative_path}")
    file(APPEND "${package_root}/corresponding-source-manifest.txt"
        "${file_hash}  ${portable_path}\n")
endforeach()

file(GLOB_RECURSE archive_files
    LIST_DIRECTORIES FALSE
    RELATIVE "${staging_root}"
    "${staging_root}/*")
list(SORT archive_files)
set(expected_archive_entries "")
foreach(relative_path IN LISTS archive_files)
    string(REPLACE "\\" "/" portable_path "${relative_path}")
    string(APPEND expected_archive_entries "${portable_path}\n")
endforeach()
set(expected_entries_file "${package_area}/.expected-archive-entries.txt")
file(WRITE "${expected_entries_file}" "${expected_archive_entries}")

set(zip_helper "${package_area}/.create-verified-unicode-zip.ps1")
file(WRITE "${zip_helper}" [=[param(
    [Parameter(Mandatory = $true)]
    [string]$SourceDirectory,
    [Parameter(Mandatory = $true)]
    [string]$DestinationArchive,
    [Parameter(Mandatory = $true)]
    [string]$ExpectedEntriesPath
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$sourcePath = (Resolve-Path -LiteralPath $SourceDirectory).Path
$destinationPath = [IO.Path]::GetFullPath($DestinationArchive)
if (Test-Path -LiteralPath $destinationPath) {
    Remove-Item -LiteralPath $destinationPath -Force
}
[IO.Compression.ZipFile]::CreateFromDirectory(
    $sourcePath,
    $destinationPath,
    [IO.Compression.CompressionLevel]::Optimal,
    $false)

$utf8 = [Text.UTF8Encoding]::new($false)
$expected = @([IO.File]::ReadAllLines($ExpectedEntriesPath, $utf8) |
    Sort-Object)
$archive = [IO.Compression.ZipFile]::OpenRead($destinationPath)
try {
    $actual = @($archive.Entries |
        Where-Object { $_.Name.Length -ne 0 } |
        ForEach-Object { $_.FullName.Replace('\', '/') } |
        Sort-Object)
    $differences = @(Compare-Object -ReferenceObject $expected -DifferenceObject $actual)
    if ($differences.Count -ne 0) {
        throw "ZIP entry names differ from the staged source: $differences"
    }
    Write-Output "verified_entries=$($actual.Count)"
}
finally {
    $archive.Dispose()
}
]=])

execute_process(
    COMMAND "${GC_PACKAGE_POWERSHELL_EXECUTABLE}"
        -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass
        -File "${zip_helper}"
        -SourceDirectory "${staging_root}"
        -DestinationArchive "${temporary_zip}"
        -ExpectedEntriesPath "${expected_entries_file}"
    RESULT_VARIABLE create_result
    OUTPUT_VARIABLE create_output
    ERROR_VARIABLE create_error
)
if(NOT create_result EQUAL 0 OR NOT EXISTS "${temporary_zip}")
    file(REMOVE
        "${temporary_zip}"
        "${zip_helper}"
        "${expected_entries_file}")
    message(FATAL_ERROR
        "Could not create corresponding-source ZIP: ${create_output}${create_error}")
endif()
file(REMOVE "${zip_helper}" "${expected_entries_file}")

file(SHA256 "${temporary_zip}" archive_hash)
if(DEFINED GC_PACKAGE_EXPECTED_ARCHIVE_SHA256 AND
        NOT "${GC_PACKAGE_EXPECTED_ARCHIVE_SHA256}" STREQUAL "" AND
        NOT "${archive_hash}" STREQUAL
            "${GC_PACKAGE_EXPECTED_ARCHIVE_SHA256}")
    file(REMOVE "${temporary_zip}")
    message(FATAL_ERROR
        "Corresponding-source ZIP SHA-256 did not match the expected value")
endif()

file(RENAME "${temporary_zip}" "${final_zip}" RESULT rename_result)
if(NOT rename_result STREQUAL "0")
    file(REMOVE "${temporary_zip}")
    message(FATAL_ERROR
        "Could not publish corresponding-source ZIP: ${rename_result}")
endif()

file(MAKE_DIRECTORY "${package_dist_dir}")
file(COPY_FILE "${final_zip}" "${dist_temporary_zip}" RESULT copy_result)
if(NOT copy_result STREQUAL "0")
    file(REMOVE "${dist_temporary_zip}")
    message(FATAL_ERROR
        "Could not copy corresponding-source ZIP to dist: ${copy_result}")
endif()
file(SHA256 "${dist_temporary_zip}" dist_archive_hash)
if(NOT "${archive_hash}" STREQUAL "${dist_archive_hash}")
    file(REMOVE "${dist_temporary_zip}")
    message(FATAL_ERROR "Distribution corresponding-source ZIP hash mismatch")
endif()
file(RENAME "${dist_temporary_zip}" "${dist_final_zip}"
    RESULT dist_rename_result)
if(NOT dist_rename_result STREQUAL "0")
    file(REMOVE "${dist_temporary_zip}")
    message(FATAL_ERROR
        "Could not publish dist corresponding-source ZIP: ${dist_rename_result}")
endif()

file(REMOVE_RECURSE "${staging_root}")
message(STATUS
    "Published ${final_zip}\n"
    "SHA256=${archive_hash}\n"
    "Commit=${project_commit}")
