add_library(gc_config_gui_host STATIC
        Win32D3D11Host.cpp
)
target_include_directories(gc_config_gui_host PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(gc_config_gui_host PUBLIC
        imgui
        d3d11
        dxgi
        user32
)

add_library(gc_config_gui_model STATIC
        InputEditorModel.cpp
)
target_include_directories(gc_config_gui_model PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(gc_config_gui_model PUBLIC
        gc_config
        gc_input_win32
)

add_executable(ConfigGUI Main.cpp)
set_target_properties(ConfigGUI PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${GC_DIST_DIR}"
        PDB_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
if(MSVC)
    target_link_options(ConfigGUI PRIVATE
            "/ILK:${CMAKE_CURRENT_BINARY_DIR}/ConfigGUI.ilk")
endif()
target_include_directories(ConfigGUI PRIVATE
        ${PROJECT_SOURCE_DIR}/src
        ${SDL3_SOURCE_DIR}/include
        ${reflectcpp_SOURCE_DIR}/include
        ${plog_SOURCE_DIR}/include
)
target_link_libraries(ConfigGUI PRIVATE
        gc_config
        SDL3-static
        ntdll
        tomlplusplus::tomlplusplus
        reflectcpp
        imgui
        gc_config_gui_host
        gc_config_gui_model
)
