add_library(gc_input_types STATIC
        DigitalLatch.cpp
        PhysicalKey.cpp
)
target_include_directories(gc_input_types PUBLIC
        ${PROJECT_SOURCE_DIR}/src
)
