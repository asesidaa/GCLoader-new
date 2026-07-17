add_library(gc_runtime_patches STATIC
        Framerate/FramerateDiagnostics.cpp
        Framerate/FrameratePatch.cpp
        Framerate/FramerateMonitor.cpp
        Framerate/FramerateProfile.cpp
        Countdown/CountdownTimerFreeze.cpp
)
target_include_directories(gc_runtime_patches PUBLIC
        ${PROJECT_SOURCE_DIR}/src
        ${plog_SOURCE_DIR}/include
)
target_link_libraries(gc_runtime_patches PUBLIC
        gc_config
        safetyhook::safetyhook
)
