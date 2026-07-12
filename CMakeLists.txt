cmake_minimum_required(VERSION 3.31)
project(GCLoader VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
set(CMAKE_CXX_STANDARD 23)
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE INTERNAL "")
endif()
find_package(Git REQUIRED)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Set warning level
if(MSVC)
    add_compile_options(/W3)
    # Set the source file encoding to UTF-8
    add_compile_options(/utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_definitions(-DNOMINMAX)

include(FetchContent)

# Fetch SDL3
FetchContent_Declare(
        SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG 5ac37a8ffcf89da390404c1016833d56e2d67ae4 # v3.2.12
        # URL https://github.com/libsdl-org/SDL/releases/download/preview-3.1.6/SDL3-3.1.6.tar.gz
)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(SDL3)

# Fetch Minhook
FetchContent_Declare(
        minhook
        GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
        GIT_TAG 05c06c5bbca226b72ffb40fc0caaef33bcaf6f74
)
FetchContent_MakeAvailable(minhook)

FetchContent_Declare(
        tomlplusplus
        GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
        GIT_TAG v3.4.0
)
FetchContent_MakeAvailable(tomlplusplus)
export(TARGETS tomlplusplus_tomlplusplus  FILE "${CMAKE_BINARY_DIR}/tomlplusplus-config.cmake")
set(tomlplusplus_DIR "${CMAKE_BINARY_DIR}")

set(SAFETYHOOK_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(SAFETYHOOK_BUILD_TEST OFF CACHE BOOL "" FORCE)
set(SAFETYHOOK_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SAFETYHOOK_FETCH_ZYDIS ON CACHE BOOL "" FORCE)
FetchContent_Declare(
        safetyhook
        GIT_REPOSITORY https://github.com/cursey/safetyhook.git
        GIT_TAG v0.6.9
)
FetchContent_MakeAvailable(safetyhook)

FetchContent_Declare(
        reflectcpp
        URL https://github.com/getml/reflect-cpp/archive/refs/tags/v0.20.0.zip
)
set(REFLECTCPP_TOML ON CACHE BOOL "" FORCE)
set(REFLECTCPP_XML OFF CACHE BOOL "" FORCE)
set(REFLECTCPP_USE_VCPKG OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(reflectcpp)

# Fetch plog
FetchContent_Declare(
        plog
        GIT_REPOSITORY https://github.com/SergiusTheBest/plog
        GIT_TAG 1.1.10
)
FetchContent_MakeAvailable(plog)

FetchContent_Declare(imgui_external
        URL https://github.com/ocornut/imgui/archive/refs/tags/v1.91.9b.tar.gz
        EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(imgui_external)

add_library(imgui
        ${imgui_external_SOURCE_DIR}/imgui.cpp
        ${imgui_external_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_external_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_external_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_external_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
        ${imgui_external_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp
        ${imgui_external_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
)

target_include_directories(imgui PUBLIC ${imgui_external_SOURCE_DIR}
        ${SDL3_SOURCE_DIR}/include)

set(SOURCES
        CountdownTimerFreeze.cpp
        config.cpp
        dllmain.cpp
        FrameratePatch.cpp
        iDmacDrv32.cpp
        InputManager.cpp
        NesysHookTransaction.cpp
        NesysNetworkConfig.cpp
        NesysServiceLauncher.cpp
        NesysServicePatch.cpp
        NesysServiceProcess.cpp
        RegistryConfig.cpp
        RegistryConfigOverride.cpp
        RfidEmu.cpp
        ServerAddressOverride.cpp
        SessionLog.cpp
        SwitchInputPolicy.cpp
        SwitchInputPatch.cpp
        SyntheticNetworkAdapter.cpp
        TestModeStorageRedirect.cpp
)

set(GUI_SOURCES
        config.cpp
        GUI_main.cpp
        NesysNetworkConfig.cpp
        RegistryConfig.cpp
)


add_library(iDmacDrv32 SHARED ${SOURCES})
target_include_directories(iDmacDrv32 PRIVATE
        ${SDL3_SOURCE_DIR}/include
        ${minhook_SOURCE_DIR}/include
        ${reflectcpp_SOURCE_DIR}/include
        ${plog_SOURCE_DIR}/include
)

target_link_libraries(iDmacDrv32 PRIVATE
        SDL3-static
        ntdll
        minhook
        safetyhook::safetyhook
        tomlplusplus::tomlplusplus
        reflectcpp
)

add_executable(ConfigGUI ${GUI_SOURCES})
target_include_directories(ConfigGUI PRIVATE
        ${SDL3_SOURCE_DIR}/include
        ${reflectcpp_SOURCE_DIR}/include
        ${plog_SOURCE_DIR}/include
)
target_link_libraries(ConfigGUI PRIVATE
        SDL3-static
        ntdll
        tomlplusplus::tomlplusplus
        reflectcpp
        imgui
)

enable_testing()

add_executable(CountdownTimerFreezeTests
        tests/CountdownTimerFreezeTests.cpp
)
target_include_directories(CountdownTimerFreezeTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)
add_test(NAME CountdownTimerFreezeTests COMMAND CountdownTimerFreezeTests)

add_executable(ConfigFeatureTests
        NesysNetworkConfig.cpp
        RegistryConfig.cpp
        tests/ConfigFeatureTests.cpp
)
target_include_directories(ConfigFeatureTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${SDL3_SOURCE_DIR}/include
        ${reflectcpp_SOURCE_DIR}/include
        ${plog_SOURCE_DIR}/include
)
target_link_libraries(ConfigFeatureTests PRIVATE
        tomlplusplus::tomlplusplus
        reflectcpp
)
add_test(NAME ConfigFeatureTests COMMAND ConfigFeatureTests)

add_executable(RegistryConfigOverrideTests
        NesysServiceProcess.cpp
        RegistryConfig.cpp
        RegistryConfigOverride.cpp
        tests/RegistryConfigOverrideTests.cpp
)
target_include_directories(RegistryConfigOverrideTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${minhook_SOURCE_DIR}/include
        ${reflectcpp_SOURCE_DIR}/include
        ${plog_SOURCE_DIR}/include
)
add_test(
        NAME RegistryConfigOverrideTests
        COMMAND RegistryConfigOverrideTests
)

add_executable(SessionLogTests
        SessionLog.cpp
        tests/SessionLogTests.cpp
)
target_include_directories(SessionLogTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${plog_SOURCE_DIR}/include
)
add_test(NAME SessionLogTests COMMAND SessionLogTests)

add_executable(NesysHookTransactionTests
        NesysHookTransaction.cpp
        tests/NesysHookTransactionTests.cpp
)
target_include_directories(NesysHookTransactionTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${minhook_SOURCE_DIR}/include
)
target_link_libraries(NesysHookTransactionTests PRIVATE minhook)
add_test(NAME NesysHookTransactionTests COMMAND NesysHookTransactionTests)

add_executable(SyntheticNetworkAdapterTests
        SyntheticNetworkAdapter.cpp
        tests/SyntheticNetworkAdapterTests.cpp
)
target_include_directories(SyntheticNetworkAdapterTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${minhook_SOURCE_DIR}/include
        ${plog_SOURCE_DIR}/include
)
target_link_libraries(SyntheticNetworkAdapterTests PRIVATE
        safetyhook::safetyhook
)
add_test(NAME SyntheticNetworkAdapterTests COMMAND SyntheticNetworkAdapterTests)

add_executable(ServerAddressOverrideTests
        NesysNetworkConfig.cpp
        ServerAddressOverride.cpp
        tests/ServerAddressOverrideTests.cpp
)
target_include_directories(ServerAddressOverrideTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${minhook_SOURCE_DIR}/include
        ${plog_SOURCE_DIR}/include
)
add_test(NAME ServerAddressOverrideTests COMMAND ServerAddressOverrideTests)

add_executable(SwitchInputPolicyTests
        SwitchInputPolicy.cpp
        tests/SwitchInputPolicyTests.cpp
)
target_include_directories(SwitchInputPolicyTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)
add_test(NAME SwitchInputPolicyTests COMMAND SwitchInputPolicyTests)

add_executable(SwitchInputPatchTests
        SwitchInputPolicy.cpp
        tests/SwitchInputPatchTests.cpp
)
target_include_directories(SwitchInputPatchTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)
add_test(NAME SwitchInputPatchTests COMMAND SwitchInputPatchTests)

add_executable(NesysServicePatchTests
        NesysServiceLauncher.cpp
        NesysServiceProcess.cpp
        tests/NesysServicePatchTests.cpp
)
target_include_directories(NesysServicePatchTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${minhook_SOURCE_DIR}/include
        ${plog_SOURCE_DIR}/include
)
add_test(NAME NesysServicePatchTests COMMAND NesysServicePatchTests)

add_executable(TestModeStorageRedirectTests
        TestModeStorageRedirect.cpp
        tests/TestModeStorageRedirectTests.cpp
)
target_include_directories(TestModeStorageRedirectTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)
add_test(NAME TestModeStorageRedirectTests COMMAND TestModeStorageRedirectTests)


if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Define the .def file
set(DEF_FILE "${CMAKE_CURRENT_SOURCE_DIR}/iDmacDrv32.def")

if (MSVC)
    # Add the .def file to the target properties
    set_target_properties(iDmacDrv32 PROPERTIES LINK_FLAGS "/DEF:${DEF_FILE}")
endif()
