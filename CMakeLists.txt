cmake_minimum_required(VERSION 3.31)
project(GCLoader VERSION 1.0.0 LANGUAGES C CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(ProjectOptions)

find_package(Git REQUIRED)
include(Dependencies)

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
        InputPollingRuntime.cpp
        InputSnapshotState.cpp
        NesysHookTransaction.cpp
        NesysNetworkConfig.cpp
        NesysServiceLauncher.cpp
        NesysServicePatch.cpp
        NesysServiceProcess.cpp
        RegistryConfig.cpp
        RegistryConfigOverride.cpp
        Rfid/Feature.cpp
        Rfid/ComPortState.cpp
        Rfid/Runtime.cpp
        Rfid/State.cpp
        Rfid/TaitoCommands.cpp
        Rfid/Jvs/Decoder.cpp
        Rfid/Jvs/Device.cpp
        Rfid/Jvs/Encoder.cpp
        ServerAddressOverride.cpp
        SessionLog.cpp
        SwitchInputPolicy.cpp
        SwitchInputPatch.cpp
        SyntheticNetworkAdapter.cpp
        TestModeStorage/Redirector.cpp
        TestModeStorage/Hooks.cpp
        Win32Hooks/Kernel32Hooks.cpp
        Win32Hooks/MinHookTransaction.cpp
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
        ${CMAKE_CURRENT_SOURCE_DIR}
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

add_executable(RfidRuntimeTests
        Rfid/Runtime.cpp
        Rfid/ComPortState.cpp
        Rfid/State.cpp
        Rfid/TaitoCommands.cpp
        Rfid/Jvs/Decoder.cpp
        Rfid/Jvs/Device.cpp
        Rfid/Jvs/Encoder.cpp
        tests/Rfid/RfidRuntimeTests.cpp
)
target_include_directories(RfidRuntimeTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${plog_SOURCE_DIR}/include
)
add_test(NAME RfidRuntimeTests COMMAND RfidRuntimeTests)

add_executable(JvsCodecTests
        Rfid/Jvs/Decoder.cpp
        Rfid/Jvs/Encoder.cpp
        tests/Rfid/JvsCodecTests.cpp
)
target_include_directories(JvsCodecTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)
add_test(NAME JvsCodecTests COMMAND JvsCodecTests)

add_executable(JvsDeviceTests
        Rfid/Jvs/Device.cpp
        Rfid/State.cpp
        Rfid/TaitoCommands.cpp
        tests/Rfid/JvsDeviceTests.cpp
)
target_include_directories(JvsDeviceTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)
add_test(NAME JvsDeviceTests COMMAND JvsDeviceTests)

add_executable(ComPortStateTests
        Rfid/ComPortState.cpp
        Rfid/State.cpp
        Rfid/TaitoCommands.cpp
        Rfid/Jvs/Decoder.cpp
        Rfid/Jvs/Device.cpp
        Rfid/Jvs/Encoder.cpp
        tests/Rfid/ComPortStateTests.cpp
)
target_include_directories(ComPortStateTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${plog_SOURCE_DIR}/include
)
add_test(NAME ComPortStateTests COMMAND ComPortStateTests)

add_executable(Kernel32HookTests
        Rfid/ComPortState.cpp
        Rfid/Runtime.cpp
        Rfid/State.cpp
        Rfid/TaitoCommands.cpp
        Rfid/Jvs/Decoder.cpp
        Rfid/Jvs/Device.cpp
        Rfid/Jvs/Encoder.cpp
        TestModeStorage/Redirector.cpp
        TestModeStorage/Hooks.cpp
        Win32Hooks/Kernel32Hooks.cpp
        Win32Hooks/MinHookTransaction.cpp
        tests/Win32Hooks/Kernel32HookTests.cpp
)
target_include_directories(Kernel32HookTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${minhook_SOURCE_DIR}/include
        ${plog_SOURCE_DIR}/include
)
target_link_libraries(Kernel32HookTests PRIVATE minhook)
add_test(NAME Kernel32HookTests COMMAND Kernel32HookTests)

add_executable(InputSnapshotStateTests
        InputSnapshotState.cpp
        tests/InputSnapshotStateTests.cpp
)
target_include_directories(InputSnapshotStateTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)
add_test(NAME InputSnapshotStateTests COMMAND InputSnapshotStateTests)

add_executable(InputPollingRuntimeStartupTests
        config.cpp
        InputManager.cpp
        InputPollingRuntime.cpp
        InputSnapshotState.cpp
        NesysNetworkConfig.cpp
        RegistryConfig.cpp
        tests/InputPollingRuntimeStartupTests.cpp
)
target_include_directories(InputPollingRuntimeStartupTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${SDL3_SOURCE_DIR}/include
        ${reflectcpp_SOURCE_DIR}/include
        ${plog_SOURCE_DIR}/include
)
target_link_libraries(InputPollingRuntimeStartupTests PRIVATE
        SDL3-static
        ntdll
        tomlplusplus::tomlplusplus
        reflectcpp
)
add_test(
        NAME InputPollingRuntimeStartupTests
        COMMAND InputPollingRuntimeStartupTests
)
set_tests_properties(InputPollingRuntimeStartupTests PROPERTIES
        TIMEOUT 5
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

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
        TestModeStorage/Redirector.cpp
        TestModeStorage/Hooks.cpp
        tests/TestModeStorage/TestModeStorageRedirectTests.cpp
)
target_include_directories(TestModeStorageRedirectTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${plog_SOURCE_DIR}/include
)
add_test(NAME TestModeStorageRedirectTests COMMAND TestModeStorageRedirectTests)

# Define the .def file
set(DEF_FILE "${CMAKE_CURRENT_SOURCE_DIR}/iDmacDrv32.def")

if (MSVC)
    # Add the .def file to the target properties
    set_target_properties(iDmacDrv32 PROPERTIES LINK_FLAGS "/DEF:${DEF_FILE}")
endif()
