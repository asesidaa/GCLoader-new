add_library(gc_input STATIC
        Polling/InputManager.cpp
        Polling/InputPollingRuntime.cpp
        Polling/InputSnapshotState.cpp
        Switch/SwitchInputPatch.cpp
        Switch/SwitchInputPolicy.cpp
        XioOnly/XioOnlyInputPatch.cpp
)
target_include_directories(gc_input PUBLIC
        ${PROJECT_SOURCE_DIR}/src
        ${SDL3_SOURCE_DIR}/include
        ${plog_SOURCE_DIR}/include
)
target_link_libraries(gc_input PUBLIC
        gc_config
        SDL3-static
        ntdll
        safetyhook::safetyhook
)
