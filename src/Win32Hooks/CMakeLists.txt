add_library(gc_win32_hooks STATIC Kernel32Hooks.cpp)
target_include_directories(gc_win32_hooks PUBLIC
        ${PROJECT_SOURCE_DIR}/src
        ${minhook_SOURCE_DIR}/include
        ${plog_SOURCE_DIR}/include
)
target_link_libraries(gc_win32_hooks PUBLIC
        gc_hooking
        gc_rfid_core
        gc_system_path
        gc_test_mode_storage
)
