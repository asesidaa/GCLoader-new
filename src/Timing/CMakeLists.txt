add_library(gc_timing STATIC
        CheckedRational.cpp
)
target_include_directories(gc_timing PUBLIC
        ${PROJECT_SOURCE_DIR}/src
)
