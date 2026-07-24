add_library(gc_runtime_patches STATIC
        Framerate/FramerateAuthoredClock.cpp
        Framerate/FramerateDiagnostics.cpp
        Framerate/FramerateEffectTiming.cpp
        Framerate/FramerateHookTransforms.cpp
        Framerate/FrameratePatch.cpp
        Framerate/FrameratePatchPlan.cpp
        Framerate/FramerateMonitor.cpp
        Framerate/FramerateProfile.cpp
        Framerate/FrameratePatchTransaction.cpp
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

add_subdirectory(TestModeTiming)
