add_executable(ConfigFeatureTests ConfigFeatureTests.cpp)
target_link_libraries(ConfigFeatureTests PRIVATE gc_config gc_input_win32)
add_test(NAME ConfigFeatureTests COMMAND ConfigFeatureTests)

add_executable(NativeInputConfigTests NativeInputConfigTests.cpp)
target_link_libraries(NativeInputConfigTests PRIVATE gc_config)
add_test(NAME NativeInputConfigTests COMMAND NativeInputConfigTests)

add_test(
        NAME ConfigGuiWidgetTests
        COMMAND ${CMAKE_COMMAND}
                -DCONFIG_GUI_SOURCE=${PROJECT_SOURCE_DIR}/tools/ConfigGUI/Main.cpp
                -P ${CMAKE_CURRENT_SOURCE_DIR}/ConfigGuiWidgetTests.cmake)

add_test(
        NAME ConfigGuiHostContractTests
        COMMAND ${CMAKE_COMMAND}
                -DCONFIG_GUI_HOST_SOURCE=${PROJECT_SOURCE_DIR}/tools/ConfigGUI/Win32D3D11Host.cpp
                -P ${CMAKE_CURRENT_SOURCE_DIR}/ConfigGuiHostContractTests.cmake)

add_executable(ConfigGuiInputModelTests ConfigGuiInputModelTests.cpp)
target_include_directories(ConfigGuiInputModelTests PRIVATE
        ${PROJECT_SOURCE_DIR}/tools/ConfigGUI
)
target_link_libraries(ConfigGuiInputModelTests PRIVATE
        gc_config_gui_model
)
add_test(NAME ConfigGuiInputModelTests COMMAND ConfigGuiInputModelTests)
