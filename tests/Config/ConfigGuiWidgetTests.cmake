if(NOT DEFINED CONFIG_GUI_SOURCE)
    message(FATAL_ERROR "CONFIG_GUI_SOURCE is required")
endif()

file(READ "${CONFIG_GUI_SOURCE}" config_gui_source)

string(REGEX MATCH
        "ImGui::InputScalar\\([	\r\n ]*\"Target FPS\""
        target_fps_input
        "${config_gui_source}")
if(target_fps_input STREQUAL "")
    message(FATAL_ERROR "Target FPS must use an integer InputScalar field")
endif()

string(REGEX MATCH
        "ImGui::SliderScalar\\([	\r\n ]*\"Target FPS\""
        target_fps_slider
        "${config_gui_source}")
if(NOT target_fps_slider STREQUAL "")
    message(FATAL_ERROR "Target FPS must not use a slider")
endif()
