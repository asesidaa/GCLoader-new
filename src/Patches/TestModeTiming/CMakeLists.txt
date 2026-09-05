add_library(gc_test_mode_timing STATIC
        TimingSettingsModel.cpp
        SystemConfigTimingStore.cpp
        TestModeTimingProfile.cpp
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
        gc_patch_absolute_judgement
        gc_game_version
        gc_fatal_process
)
