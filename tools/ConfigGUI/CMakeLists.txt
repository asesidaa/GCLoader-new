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
        AudioBackendEditorModel.cpp
        InputEditorModel.cpp
)
target_include_directories(gc_config_gui_model PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(gc_config_gui_model PUBLIC
        gc_asio
        gc_config
        gc_input_win32
)

if(NOT EXISTS "${GC_ASIO_COMPATIBLE_LOGO}")
    message(FATAL_ERROR
            "GC_ASIO_COMPATIBLE_LOGO does not name the required SDK asset")
endif()
get_filename_component(GC_ASIO_LOGO_FILENAME
        "${GC_ASIO_COMPATIBLE_LOGO}" NAME)
configure_file(
        "${GC_ASIO_COMPATIBLE_LOGO}"
        "${GC_DIST_DIR}/${GC_ASIO_LOGO_FILENAME}"
        COPYONLY
)

add_executable(ConfigGUI
        AsioLogoTexture.cpp
        AsioProbeMode.cpp
        Main.cpp
)
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
        ${reflectcpp_SOURCE_DIR}/include
        ${plog_SOURCE_DIR}/include
)
target_compile_definitions(ConfigGUI PRIVATE
        GC_ASIO_LOGO_FILENAME="${GC_ASIO_LOGO_FILENAME}"
)
target_link_libraries(ConfigGUI PRIVATE
        gc_asio
        gc_config
        ntdll
        tomlplusplus::tomlplusplus
        reflectcpp
        imgui
        gc_config_gui_host
        gc_config_gui_model
        windowscodecs
)
