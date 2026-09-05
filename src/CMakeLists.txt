add_subdirectory(SystemPath)
add_subdirectory(Nesys/Network)
add_subdirectory(Input/Types)
add_subdirectory(Config)
add_subdirectory(Timing)
add_subdirectory(Audio)
add_subdirectory(Input)
add_subdirectory(Nesys)
add_subdirectory(Logging)
add_subdirectory(Platform)
add_subdirectory(Locale)
add_subdirectory(Diagnostics)
add_subdirectory(Rfid)
add_subdirectory(TestModeStorage)
add_subdirectory(Win32Hooks)

add_library(gc_rfid_feature STATIC Rfid/Feature.cpp)
target_include_directories(gc_rfid_feature PUBLIC
        ${PROJECT_SOURCE_DIR}/src
        ${plog_SOURCE_DIR}/include
)
target_link_libraries(gc_rfid_feature PUBLIC
        gc_hooking
        gc_input_win32
        gc_rfid_core
)

add_subdirectory(Patches)

# Common configuration, process logging, validation diagnostics, and installation.
# Role-specific feature composition belongs only to the two roots below.
add_library(gc_loader_startup STATIC
        Loader/StartupConfiguration.cpp
        Loader/StartupFailure.cpp
        Loader/ProcessStartup.cpp
        Loader/VersionedStartupExecutor.cpp
)
target_include_directories(gc_loader_startup PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(gc_loader_startup
        PUBLIC gc_config gc_system_path gc_test_mode_storage gc_game_version gc_fatal_process
        PRIVATE gc_logging gc_platform_win32
)

add_library(gc_game_startup STATIC
        Loader/GameStartup.cpp
        Loader/GameVersionedStartupPlan.cpp
        Loader/NonVersionedHookPlan.cpp
        Loader/GameWin32HookComposition.cpp
)
target_include_directories(gc_game_startup PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(gc_game_startup
        PUBLIC gc_loader_startup
        PRIVATE
        gc_audio
        gc_crash_dump
        gc_input
        gc_japanese_locale_compatibility
        gc_logging
        gc_nesys
        gc_rfid_feature
        gc_win32_hooks
        gc_patch_game_compatibility
        gc_patch_auto_play
        gc_patch_song_unlock
        gc_patch_absolute_judgement
        gc_patch_framerate
        gc_patch_countdown
        gc_patch_switch_input
        gc_test_mode_timing
        gc_renderer_device_loss
        gc_windowed_widescreen
)

add_library(gc_nesys_startup STATIC
        Loader/NesysStartup.cpp
        Loader/NesysVersionedStartupPlan.cpp
)
target_include_directories(gc_nesys_startup PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(gc_nesys_startup
        PUBLIC gc_loader_startup
        PRIVATE gc_nesys gc_japanese_locale_compatibility gc_logging
)

add_library(iDmacDrv32 SHARED
        Loader/DllMain.cpp
        Driver/iDmac/iDmacDrv32.cpp
)
set_target_properties(iDmacDrv32 PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${GC_DIST_DIR}"
        PDB_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
target_include_directories(iDmacDrv32 PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(iDmacDrv32 PRIVATE
        gc_game_startup
        gc_nesys_startup
        gc_input
        gc_logging
)
target_link_libraries(gc_rfid_feature PRIVATE gc_platform_win32)

set(DEF_FILE
        "${CMAKE_CURRENT_SOURCE_DIR}/Driver/iDmac/iDmacDrv32.def")
if (MSVC)
    target_link_options(iDmacDrv32 PRIVATE
            "/ILK:${CMAKE_CURRENT_BINARY_DIR}/iDmacDrv32.ilk")
    set_target_properties(iDmacDrv32 PROPERTIES
            LINK_FLAGS "/DEF:${DEF_FILE}")
endif ()
