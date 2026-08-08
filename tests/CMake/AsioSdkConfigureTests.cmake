cmake_minimum_required(VERSION 3.31)

foreach(required IN ITEMS
        GC_PROJECT_SOURCE_DIR
        GC_TEST_BINARY_DIR
        GC_REAL_ASIO_SDK_DIR
        GC_TEST_GENERATOR)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

cmake_path(ABSOLUTE_PATH GC_TEST_BINARY_DIR NORMALIZE
    OUTPUT_VARIABLE test_root)
cmake_path(ABSOLUTE_PATH GC_PROJECT_SOURCE_DIR NORMALIZE
    OUTPUT_VARIABLE project_root)
cmake_path(ABSOLUTE_PATH GC_REAL_ASIO_SDK_DIR NORMALIZE
    OUTPUT_VARIABLE real_sdk_root)

if(test_root STREQUAL project_root OR test_root STREQUAL real_sdk_root)
    message(FATAL_ERROR "Refusing unsafe ASIO SDK test root: ${test_root}")
endif()

file(REMOVE_RECURSE "${test_root}")
file(MAKE_DIRECTORY "${test_root}")

set(fixture_source "${project_root}/tests/CMake/AsioSdkFixture")

function(write_fake_sdk root version include_iasiodrv marker)
    file(MAKE_DIRECTORY
        "${root}/common"
        "${root}/Steinberg ASIO Logo Artwork")
    file(WRITE "${root}/README.md" "fixture ${marker}\n")
    file(WRITE "${root}/LICENSE.txt" "fixture license ${marker}\n")
    file(WRITE "${root}/changes.txt" "Changes in ASIO ${version}\n")
    file(WRITE "${root}/common/asio.h" "// fixture\n")
    file(WRITE "${root}/common/asiosys.h" "// fixture\n")
    if(include_iasiodrv)
        file(WRITE "${root}/common/iasiodrv.h" "// fixture\n")
    endif()
    file(WRITE
        "${root}/Steinberg ASIO Logo Artwork/ASIO-compatible-logo-Steinberg-TM-BW.png"
        "fixture logo ${marker}\n")
endfunction()

function(run_configure case_name env_sdk cache_sdk expected_result expected_text)
    set(build_dir "${test_root}/${case_name}-build")
    set(command
        "${CMAKE_COMMAND}" -E env "--unset=GC_ASIO_SDK_DIR")
    if(NOT env_sdk STREQUAL "")
        list(APPEND command "GC_ASIO_SDK_DIR=${env_sdk}")
    endif()
    list(APPEND command
        "${CMAKE_COMMAND}"
        -S "${fixture_source}"
        -B "${build_dir}"
        -G "${GC_TEST_GENERATOR}"
        "-DGC_PROJECT_SOURCE_DIR=${project_root}")
    if(NOT cache_sdk STREQUAL "")
        list(APPEND command "-DGC_ASIO_SDK_DIR=${cache_sdk}")
    endif()

    execute_process(
        COMMAND ${command}
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
    )
    string(CONCAT combined "${output}" "\n" "${error}")

    if(expected_result STREQUAL "success")
        if(NOT result EQUAL 0)
            message(FATAL_ERROR
                "${case_name}: configure unexpectedly failed (${result})\n${combined}")
        endif()
    elseif(result EQUAL 0)
        message(FATAL_ERROR "${case_name}: configure unexpectedly succeeded")
    endif()

    if(NOT expected_text STREQUAL "")
        string(FIND "${combined}" "${expected_text}" match_index)
        if(match_index EQUAL -1)
            message(FATAL_ERROR
                "${case_name}: output did not contain '${expected_text}'\n${combined}")
        endif()
    endif()

    set(${case_name}_build_dir "${build_dir}" PARENT_SCOPE)
endfunction()

set(incomplete_sdk "${test_root}/incomplete-sdk")
write_fake_sdk("${incomplete_sdk}" "2.3.4" FALSE "incomplete")

set(old_sdk "${test_root}/old-sdk")
write_fake_sdk("${old_sdk}" "2.3.3" TRUE "old")

set(cache_sdk "${test_root}/cache-sdk")
write_fake_sdk("${cache_sdk}" "3.0.0" TRUE "cache")

run_configure(
    missing
    ""
    ""
    failure
    "GC_ASIO_SDK_DIR is required")
run_configure(
    incomplete
    ""
    "${incomplete_sdk}"
    failure
    "common/iasiodrv.h")
run_configure(
    old
    ""
    "${old_sdk}"
    failure
    "ASIO SDK 2.3.4 or newer")
run_configure(
    environment
    "${real_sdk_root}"
    ""
    success
    "")
run_configure(
    cache_wins
    "${real_sdk_root}"
    "${cache_sdk}"
    success
    "")

file(READ "${cache_wins_build_dir}/resolved-sdk.txt" resolved_cache_sdk)
cmake_path(NORMAL_PATH resolved_cache_sdk OUTPUT_VARIABLE normalized_resolved)
cmake_path(NORMAL_PATH cache_sdk OUTPUT_VARIABLE normalized_expected)
if(NOT normalized_resolved STREQUAL normalized_expected)
    message(FATAL_ERROR
        "cache_wins: resolved '${normalized_resolved}', expected '${normalized_expected}'")
endif()

message(STATUS "ASIO SDK configure contract passed")
