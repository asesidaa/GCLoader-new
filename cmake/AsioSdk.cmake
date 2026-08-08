function(gc_require_asio_sdk)
    if(NOT DEFINED GC_ASIO_SDK_DIR)
        set(GC_ASIO_SDK_DIR "" CACHE PATH
            "Root of Steinberg ASIO SDK 2.3.4+")
    endif()

    if(GC_ASIO_SDK_DIR STREQUAL "" AND
            NOT "$ENV{GC_ASIO_SDK_DIR}" STREQUAL "")
        set(GC_ASIO_SDK_DIR "$ENV{GC_ASIO_SDK_DIR}"
            CACHE PATH "Root of Steinberg ASIO SDK 2.3.4+" FORCE)
    endif()

    if(GC_ASIO_SDK_DIR STREQUAL "")
        message(FATAL_ERROR
            "GC_ASIO_SDK_DIR is required as a cache path or environment variable")
    endif()

    cmake_path(ABSOLUTE_PATH GC_ASIO_SDK_DIR NORMALIZE
        OUTPUT_VARIABLE asio_root)

    foreach(required IN ITEMS
            README.md
            LICENSE.txt
            changes.txt
            common/asio.h
            common/asiosys.h
            common/iasiodrv.h
            "Steinberg ASIO Logo Artwork/ASIO-compatible-logo-Steinberg-TM-BW.png")
        if(NOT EXISTS "${asio_root}/${required}")
            message(FATAL_ERROR
                "GC_ASIO_SDK_DIR is incomplete; missing ${required}")
        endif()
    endforeach()

    file(READ "${asio_root}/changes.txt" asio_changes LIMIT 4096)
    string(REGEX MATCH
        "Changes in ASIO ([0-9]+)\\.([0-9]+)(\\.([0-9]+))?"
        asio_version_line "${asio_changes}")
    if(asio_version_line STREQUAL "")
        message(FATAL_ERROR
            "Cannot identify ASIO SDK version from changes.txt")
    endif()

    set(asio_version_major "${CMAKE_MATCH_1}")
    set(asio_version_minor "${CMAKE_MATCH_2}")
    if(CMAKE_MATCH_4 STREQUAL "")
        set(asio_version_patch 0)
    else()
        set(asio_version_patch "${CMAKE_MATCH_4}")
    endif()

    if(asio_version_major LESS 2 OR
            (asio_version_major EQUAL 2 AND asio_version_minor LESS 3) OR
            (asio_version_major EQUAL 2 AND asio_version_minor EQUAL 3 AND
             asio_version_patch LESS 4))
        message(FATAL_ERROR "ASIO SDK 2.3.4 or newer is required")
    endif()

    if(NOT TARGET gc_asio_sdk)
        add_library(gc_asio_sdk INTERFACE)
        target_include_directories(gc_asio_sdk INTERFACE
            "${asio_root}/common")
    endif()

    set(GC_ASIO_SDK_DIR "${asio_root}" CACHE PATH
        "Root of Steinberg ASIO SDK 2.3.4+" FORCE)
    set(GC_ASIO_SDK_VERSION
        "${asio_version_major}.${asio_version_minor}.${asio_version_patch}"
        CACHE STRING "Resolved Steinberg ASIO SDK version" FORCE)
    set(GC_ASIO_COMPATIBLE_LOGO
        "${asio_root}/Steinberg ASIO Logo Artwork/ASIO-compatible-logo-Steinberg-TM-BW.png"
        CACHE FILEPATH "Official unmodified ASIO Compatible logo" FORCE)
endfunction()
