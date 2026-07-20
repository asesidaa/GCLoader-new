add_library(gc_test_mode_timing STATIC
        TimingSettingsModel.cpp
)
target_include_directories(gc_test_mode_timing PUBLIC
        ${PROJECT_SOURCE_DIR}/src
)
