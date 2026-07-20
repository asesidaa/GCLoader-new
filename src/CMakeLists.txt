add_subdirectory(Nesys/Network)
add_subdirectory(Input/Types)
add_subdirectory(Config)
add_subdirectory(Audio)
add_subdirectory(Input)
add_subdirectory(Nesys)
add_subdirectory(Logging)
add_subdirectory(Platform)
add_subdirectory(Rfid)
add_subdirectory(TestModeStorage)
add_subdirectory(Win32Hooks)

add_library(gc_rfid_feature STATIC Rfid/Feature.cpp)
target_include_directories(gc_rfid_feature PUBLIC
        ${PROJECT_SOURCE_DIR}/src
        ${minhook_SOURCE_DIR}/include
        ${plog_SOURCE_DIR}/include
)
target_link_libraries(gc_rfid_feature PUBLIC
        gc_config
        gc_hooking
        gc_input_win32
        gc_rfid_core
        gc_test_mode_storage
        gc_win32_hooks
)

add_subdirectory(Patches)

add_library(iDmacDrv32 SHARED
        Loader/DllMain.cpp
        Driver/iDmac/iDmacDrv32.cpp
)
set_target_properties(iDmacDrv32 PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${GC_DIST_DIR}"
        PDB_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
target_include_directories(iDmacDrv32 PRIVATE
        ${PROJECT_SOURCE_DIR}/src
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
        gc_runtime_patches
        gc_test_mode_timing
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

set(DEF_FILE
        "${CMAKE_CURRENT_SOURCE_DIR}/Driver/iDmac/iDmacDrv32.def")
if(MSVC)
    target_link_options(iDmacDrv32 PRIVATE
            "/ILK:${CMAKE_CURRENT_BINARY_DIR}/iDmacDrv32.ilk")
    set_target_properties(iDmacDrv32 PROPERTIES
            LINK_FLAGS "/DEF:${DEF_FILE}")
endif()
