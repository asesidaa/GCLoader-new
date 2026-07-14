cmake_minimum_required(VERSION 3.31)
project(GCLoader VERSION 1.0.0 LANGUAGES C CXX)

# CMake 4.2 can decode localized MSVC /showIncludes output twice on a
# Simplified Chinese host. Ninja then records zero header dependencies because
# its generated prefix is mojibake. Repair only that detected value; leave all
# other locales and generators untouched.
if(MSVC AND CMAKE_GENERATOR MATCHES "^Ninja" AND
        CMAKE_CL_SHOWINCLUDES_PREFIX MATCHES "^娉")
    set(CMAKE_C_CL_SHOWINCLUDES_PREFIX "注意: 包含文件:  ")
    set(CMAKE_CXX_CL_SHOWINCLUDES_PREFIX "注意: 包含文件:  ")
    set(CMAKE_CL_SHOWINCLUDES_PREFIX "注意: 包含文件:  ")
endif()

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
        AudioCursorTimeline.cpp
        AudioSnapshot.cpp
        DirectSoundFacade.cpp
        ExclusiveAudioEngine.cpp
        MiniaudioMixer.cpp
        OutputPacingTracker.cpp
        CountdownTimerFreeze.cpp
        config.cpp
        dllmain.cpp
        FrameratePatch.cpp
        iDmacDrv32.cpp
        InputManager.cpp
        InputSnapshotState.cpp
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
        WasapiAudioPatch.cpp
        WasapiAudioTypes.cpp
        WasapiEndpoint.cpp
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
        dsound
        dxguid
        ntdll
        ole32
        uuid
        avrt
        propsys
        minhook
        miniaudio
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

add_executable(AudioFormatTests
        WasapiAudioTypes.cpp
        tests/AudioFormatTests.cpp
)
target_include_directories(AudioFormatTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(AudioFormatTests PRIVATE miniaudio)
add_test(NAME AudioFormatTests COMMAND AudioFormatTests)

add_executable(AudioSnapshotTests
        AudioSnapshot.cpp
        tests/AudioSnapshotTests.cpp
)
target_include_directories(AudioSnapshotTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)
add_test(NAME AudioSnapshotTests COMMAND AudioSnapshotTests)

add_executable(AudioCursorTimelineTests
        AudioCursorTimeline.cpp
        tests/AudioCursorTimelineTests.cpp
)
target_include_directories(AudioCursorTimelineTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(AudioCursorTimelineTests PRIVATE miniaudio)
add_test(NAME AudioCursorTimelineTests COMMAND AudioCursorTimelineTests)

add_executable(OutputPacingTrackerTests
        OutputPacingTracker.cpp
        tests/OutputPacingTrackerTests.cpp
)
target_include_directories(OutputPacingTrackerTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)
add_test(NAME OutputPacingTrackerTests COMMAND OutputPacingTrackerTests)

add_executable(MiniaudioMixerTests
        WasapiAudioTypes.cpp
        AudioSnapshot.cpp
        AudioCursorTimeline.cpp
        MiniaudioMixer.cpp
        tests/MiniaudioMixerTests.cpp
)
target_include_directories(MiniaudioMixerTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(MiniaudioMixerTests PRIVATE miniaudio)
add_test(NAME MiniaudioMixerTests COMMAND MiniaudioMixerTests)

add_executable(SecondarySoundBufferTests
        WasapiAudioTypes.cpp
        AudioSnapshot.cpp
        AudioCursorTimeline.cpp
        MiniaudioMixer.cpp
        DirectSoundFacade.cpp
        tests/SecondarySoundBufferTests.cpp
)
target_include_directories(SecondarySoundBufferTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(SecondarySoundBufferTests PRIVATE
        miniaudio
        dsound
        dxguid
)
add_test(NAME SecondarySoundBufferTests COMMAND SecondarySoundBufferTests)

add_executable(DirectSoundDeviceTests
        WasapiAudioTypes.cpp
        AudioSnapshot.cpp
        AudioCursorTimeline.cpp
        MiniaudioMixer.cpp
        DirectSoundFacade.cpp
        tests/DirectSoundDeviceTests.cpp
)
target_include_directories(DirectSoundDeviceTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(DirectSoundDeviceTests PRIVATE
        miniaudio
        dsound
        dxguid
)
add_test(NAME DirectSoundDeviceTests COMMAND DirectSoundDeviceTests)

add_executable(WasapiEndpointTests
        WasapiAudioTypes.cpp
        WasapiEndpoint.cpp
        tests/WasapiEndpointTests.cpp
)
target_include_directories(WasapiEndpointTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(WasapiEndpointTests PRIVATE
        miniaudio
        ole32
        uuid
        avrt
        propsys
)
add_test(NAME WasapiEndpointTests COMMAND WasapiEndpointTests)

add_executable(ExclusiveAudioEngineTests
        WasapiAudioTypes.cpp
        AudioSnapshot.cpp
        AudioCursorTimeline.cpp
        MiniaudioMixer.cpp
        DirectSoundFacade.cpp
        WasapiEndpoint.cpp
        OutputPacingTracker.cpp
        ExclusiveAudioEngine.cpp
        tests/ExclusiveAudioEngineTests.cpp
)
target_include_directories(ExclusiveAudioEngineTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(ExclusiveAudioEngineTests PRIVATE
        miniaudio
        dsound
        dxguid
        ole32
        uuid
        avrt
        propsys
)
add_test(NAME ExclusiveAudioEngineTests COMMAND ExclusiveAudioEngineTests)

add_executable(WasapiAudioPatchTests
        WasapiAudioTypes.cpp
        AudioSnapshot.cpp
        AudioCursorTimeline.cpp
        MiniaudioMixer.cpp
        DirectSoundFacade.cpp
        WasapiEndpoint.cpp
        OutputPacingTracker.cpp
        ExclusiveAudioEngine.cpp
        WasapiAudioPatch.cpp
        NesysNetworkConfig.cpp
        RegistryConfig.cpp
        config.cpp
        tests/WasapiAudioPatchTests.cpp
)
target_include_directories(WasapiAudioPatchTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${SDL3_SOURCE_DIR}/include
        ${minhook_SOURCE_DIR}/include
        ${reflectcpp_SOURCE_DIR}/include
        ${plog_SOURCE_DIR}/include
)
target_link_libraries(WasapiAudioPatchTests PRIVATE
        SDL3-static
        minhook
        miniaudio
        dsound
        dxguid
        ole32
        uuid
        avrt
        propsys
        tomlplusplus::tomlplusplus
        reflectcpp
)
add_test(NAME WasapiAudioPatchTests COMMAND WasapiAudioPatchTests)

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

add_executable(InputSnapshotStateTests
        InputSnapshotState.cpp
        tests/InputSnapshotStateTests.cpp
)
target_include_directories(InputSnapshotStateTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)
add_test(NAME InputSnapshotStateTests COMMAND InputSnapshotStateTests)

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
