add_subdirectory(Nesys/Network)
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
        gc_rfid_core
        gc_test_mode_storage
        gc_win32_hooks
)

add_subdirectory(Patches)
