cmake_minimum_required(VERSION 3.31)
project(GCLoader VERSION 1.0.0 LANGUAGES C CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(ProjectOptions)

find_package(Git REQUIRED)
include(Dependencies)

enable_testing()
add_subdirectory(src)
add_subdirectory(tools)
add_subdirectory(tests)

set(SOURCES
        CountdownTimerFreeze.cpp
        dllmain.cpp
        FrameratePatch.cpp
        iDmacDrv32.cpp
        Rfid/Feature.cpp
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
)

add_library(iDmacDrv32 SHARED ${SOURCES})
target_include_directories(iDmacDrv32 PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/src
        ${SDL3_SOURCE_DIR}/include
        ${minhook_SOURCE_DIR}/include
        ${reflectcpp_SOURCE_DIR}/include
        ${plog_SOURCE_DIR}/include
)

target_link_libraries(iDmacDrv32 PRIVATE
        gc_audio
        gc_config
        gc_input
        gc_logging
        gc_nesys
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











add_executable(CountdownTimerFreezeTests
        tests/CountdownTimerFreezeTests.cpp
)
target_include_directories(CountdownTimerFreezeTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)
add_test(NAME CountdownTimerFreezeTests COMMAND CountdownTimerFreezeTests)

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
