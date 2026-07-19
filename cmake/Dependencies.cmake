include(FetchContent)

FetchContent_Declare(
        minhook
        GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
        GIT_TAG c3fcafdc10146beb5919319d0683e44e3c30d537
)
FetchContent_MakeAvailable(minhook)

set(MINIAUDIO_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(MINIAUDIO_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(MINIAUDIO_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(MINIAUDIO_INSTALL OFF CACHE BOOL "" FORCE)
set(MINIAUDIO_NO_EXTRA_NODES ON CACHE BOOL "" FORCE)
set(MINIAUDIO_NO_LIBVORBIS ON CACHE BOOL "" FORCE)
set(MINIAUDIO_NO_LIBOPUS ON CACHE BOOL "" FORCE)
set(MINIAUDIO_NO_DEVICEIO ON CACHE BOOL "" FORCE)
set(MINIAUDIO_NO_DECODING ON CACHE BOOL "" FORCE)
set(MINIAUDIO_NO_ENCODING ON CACHE BOOL "" FORCE)
set(MINIAUDIO_NO_RESOURCE_MANAGER ON CACHE BOOL "" FORCE)
set(MINIAUDIO_NO_GENERATION ON CACHE BOOL "" FORCE)
FetchContent_Declare(
        miniaudio
        GIT_REPOSITORY https://github.com/mackron/miniaudio.git
        GIT_TAG 9634bedb5b5a2ca38c1ee7108a9358a4e233f14d
)
FetchContent_MakeAvailable(miniaudio)
target_compile_definitions(miniaudio PUBLIC
        MA_NO_DEVICE_IO
        MA_NO_DECODING
        MA_NO_ENCODING
        MA_NO_RESOURCE_MANAGER
        MA_NO_GENERATION
)

FetchContent_Declare(
        tomlplusplus
        GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
        GIT_TAG v3.4.0
)
FetchContent_MakeAvailable(tomlplusplus)
export(TARGETS tomlplusplus_tomlplusplus
        FILE "${CMAKE_BINARY_DIR}/tomlplusplus-config.cmake")
set(tomlplusplus_DIR "${CMAKE_BINARY_DIR}")

set(SAFETYHOOK_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(SAFETYHOOK_BUILD_TEST OFF CACHE BOOL "" FORCE)
set(SAFETYHOOK_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SAFETYHOOK_FETCH_ZYDIS ON CACHE BOOL "" FORCE)
FetchContent_Declare(
        safetyhook
        GIT_REPOSITORY https://github.com/cursey/safetyhook.git
        GIT_TAG v0.7.0
)
FetchContent_MakeAvailable(safetyhook)

FetchContent_Declare(
        reflectcpp
        URL https://github.com/getml/reflect-cpp/archive/refs/tags/v0.25.0.zip
)
set(REFLECTCPP_TOML ON CACHE BOOL "" FORCE)
set(REFLECTCPP_XML OFF CACHE BOOL "" FORCE)
set(REFLECTCPP_USE_VCPKG OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(reflectcpp)

FetchContent_Declare(
        plog
        GIT_REPOSITORY https://github.com/SergiusTheBest/plog
        GIT_TAG 1.1.11
)
FetchContent_MakeAvailable(plog)

FetchContent_Declare(
        imgui_external
        URL https://github.com/ocornut/imgui/archive/refs/tags/v1.92.8.tar.gz
        EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(imgui_external)

add_library(imgui
        ${imgui_external_SOURCE_DIR}/imgui.cpp
        ${imgui_external_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_external_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_external_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_external_SOURCE_DIR}/backends/imgui_impl_win32.cpp
        ${imgui_external_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
        ${imgui_external_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
)
target_include_directories(imgui PUBLIC
        ${imgui_external_SOURCE_DIR}
        ${imgui_external_SOURCE_DIR}/backends
)
