add_library(gc_system_path STATIC
        SystemRoot.cpp
)
target_include_directories(gc_system_path PUBLIC
        ${PROJECT_SOURCE_DIR}/src
)
