add_library(gc_test_mode_storage STATIC
        Redirector.cpp
        Hooks.cpp
        NativeStorageProbe.cpp
)
target_include_directories(gc_test_mode_storage PUBLIC
        ${PROJECT_SOURCE_DIR}/src
        ${plog_SOURCE_DIR}/include
)
