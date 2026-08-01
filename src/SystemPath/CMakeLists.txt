add_library(gc_system_path STATIC
        StartupFatal.cpp
        SystemRoot.cpp
)
target_include_directories(gc_system_path PUBLIC
        ${PROJECT_SOURCE_DIR}/src
        ${plog_SOURCE_DIR}/include
)
