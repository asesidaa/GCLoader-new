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

foreach(threshold_label IN ITEMS
        "Axis press threshold (%)"
        "Axis release threshold (%)")
    string(REPLACE "(" "\\(" threshold_pattern "${threshold_label}")
    string(REPLACE ")" "\\)" threshold_pattern "${threshold_pattern}")
    string(REGEX MATCH
            "ImGui::InputScalar\\([	\r\n ]*\"${threshold_pattern}\""
            threshold_input
            "${config_gui_source}")
    if(threshold_input STREQUAL "")
        message(FATAL_ERROR
                "${threshold_label} must use an exact integer InputScalar field")
    endif()

    foreach(forbidden_widget IN ITEMS SliderInt SliderScalar DragInt)
        string(REGEX MATCH
                "ImGui::${forbidden_widget}\\([	\r\n ]*\"${threshold_pattern}\""
                forbidden_threshold_widget
                "${config_gui_source}")
        if(NOT forbidden_threshold_widget STREQUAL "")
            message(FATAL_ERROR
                    "${threshold_label} must not use ${forbidden_widget}")
        endif()
    endforeach()
endforeach()
