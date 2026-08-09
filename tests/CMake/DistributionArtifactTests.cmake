cmake_minimum_required(VERSION 3.31)

if(NOT DEFINED GC_TEST_DIST_DIR OR GC_TEST_DIST_DIR STREQUAL "")
    message(FATAL_ERROR "GC_TEST_DIST_DIR is required")
endif()

cmake_path(ABSOLUTE_PATH GC_TEST_DIST_DIR NORMALIZE
    OUTPUT_VARIABLE dist_dir)
if(dist_dir MATCHES "^[A-Za-z]:/?$" OR dist_dir STREQUAL "/")
    message(FATAL_ERROR "Refusing unsafe distribution directory: ${dist_dir}")
endif()
if(NOT IS_DIRECTORY "${dist_dir}")
    message(FATAL_ERROR "Distribution directory does not exist: ${dist_dir}")
endif()

foreach(required_file IN ITEMS
        ConfigGUI.exe
        config.toml
        card.txt
        iDmacDrv32.dll)
    if(NOT EXISTS "${dist_dir}/${required_file}")
        message(FATAL_ERROR
            "Distribution is missing required file: ${required_file}")
    endif()
endforeach()

foreach(prohibited_file IN ITEMS
        AsioProbe.exe
        ASIO-compatible-logo-Steinberg-TM-BW.png
        imgui.ini)
    if(EXISTS "${dist_dir}/${prohibited_file}")
        message(FATAL_ERROR
            "Distribution contains prohibited file: ${prohibited_file}")
    endif()
endforeach()

if(EXISTS "${dist_dir}/licenses")
    message(FATAL_ERROR "Distribution contains prohibited directory: licenses")
endif()

file(GLOB source_archives
    LIST_DIRECTORIES FALSE
    "${dist_dir}/GCLoader-*-corresponding-source.zip")
if(source_archives)
    message(FATAL_ERROR
        "Distribution contains corresponding-source archive: ${source_archives}")
endif()

message(STATUS "Distribution contains only deployable project artifacts")
