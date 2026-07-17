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
        gc_rfid_feature
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

# Define the .def file
set(DEF_FILE "${CMAKE_CURRENT_SOURCE_DIR}/iDmacDrv32.def")

if (MSVC)
    # Add the .def file to the target properties
    set_target_properties(iDmacDrv32 PROPERTIES LINK_FLAGS "/DEF:${DEF_FILE}")
endif()
