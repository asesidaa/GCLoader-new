add_library(gc_test_mode_storage STATIC
        Redirector.cpp
        Hooks.cpp
        NativeStorageProbe.cpp
        Win32PathHandlers.cpp
)
target_include_directories(gc_test_mode_storage PUBLIC
        ${PROJECT_SOURCE_DIR}/src
        ${plog_SOURCE_DIR}/include
)
target_link_libraries(gc_test_mode_storage PUBLIC gc_win32_hooks)
target_link_libraries(gc_test_mode_storage PRIVATE gc_platform_win32)
