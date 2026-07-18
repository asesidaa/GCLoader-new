add_executable(ConfigFeatureTests ConfigFeatureTests.cpp)
target_link_libraries(ConfigFeatureTests PRIVATE gc_config)
add_test(NAME ConfigFeatureTests COMMAND ConfigFeatureTests)

add_test(
        NAME ConfigGuiWidgetTests
        COMMAND ${CMAKE_COMMAND}
                -DCONFIG_GUI_SOURCE=${PROJECT_SOURCE_DIR}/tools/ConfigGUI/Main.cpp
                -P ${CMAKE_CURRENT_SOURCE_DIR}/ConfigGuiWidgetTests.cmake)
