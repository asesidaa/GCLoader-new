add_library(gc_input_win32 STATIC
        Win32/ControllerBindingEvaluator.cpp
        Win32/ControllerCatalog.cpp
        Win32/HidApi.cpp
        Win32/InputCapture.cpp
        Win32/PhysicalKeyWin32.cpp
        Win32/RawInputPacket.cpp
        Win32/RawHidController.cpp
        Win32/Win32InputWindow.cpp
        Win32/XInputApi.cpp
        Win32/XInputController.cpp
)
target_include_directories(gc_input_win32 PUBLIC
        ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(gc_input_win32 PUBLIC
        gc_input_types
        hid
        user32
)

add_library(gc_input STATIC
        Polling/ForegroundPolicy.cpp
        Polling/InputMapper.cpp
        Polling/InputPollingRuntime.cpp
        Polling/InputSnapshotState.cpp
        Polling/InputWorkerWait.cpp
        Switch/SwitchInputPatch.cpp
        Switch/SwitchInputPolicy.cpp
)
target_include_directories(gc_input PUBLIC
        ${PROJECT_SOURCE_DIR}/src
        ${plog_SOURCE_DIR}/include
)
target_link_libraries(gc_input PUBLIC
        gc_config
        gc_input_win32
        ntdll
        safetyhook::safetyhook
)
