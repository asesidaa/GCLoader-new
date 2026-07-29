add_executable(ConfigFeatureTests ConfigFeatureTests.cpp)
target_compile_definitions(ConfigFeatureTests PRIVATE
        GC_TEST_CONFIG_PATH="${PROJECT_SOURCE_DIR}/config.toml")
target_link_libraries(ConfigFeatureTests PRIVATE gc_config gc_input_win32)
add_test(NAME ConfigFeatureTests COMMAND ConfigFeatureTests)

add_executable(NativeInputConfigTests NativeInputConfigTests.cpp)
target_link_libraries(NativeInputConfigTests PRIVATE gc_config)
add_test(NAME NativeInputConfigTests COMMAND NativeInputConfigTests)

add_executable(ConfigGuiInputModelTests ConfigGuiInputModelTests.cpp)
target_include_directories(ConfigGuiInputModelTests PRIVATE
        ${PROJECT_SOURCE_DIR}/tools/ConfigGUI
)
target_link_libraries(ConfigGuiInputModelTests PRIVATE
        gc_config_gui_model
)
add_test(NAME ConfigGuiInputModelTests COMMAND ConfigGuiInputModelTests)
