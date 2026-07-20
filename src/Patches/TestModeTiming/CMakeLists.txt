add_library(gc_test_mode_timing STATIC
        TimingSettingsModel.cpp
        SystemConfigTimingStore.cpp
        TimingSettingsGameAbi.cpp
        TimingSettingsPatch.cpp
)
target_include_directories(gc_test_mode_timing PUBLIC
        ${PROJECT_SOURCE_DIR}/src
)
target_include_directories(gc_test_mode_timing PRIVATE
        ${plog_SOURCE_DIR}/include
)
target_link_libraries(gc_test_mode_timing PRIVATE
        safetyhook::safetyhook
)
