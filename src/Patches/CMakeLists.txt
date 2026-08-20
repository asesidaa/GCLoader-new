add_library(gc_runtime_patches STATIC
        AbsoluteJudgement/AbsoluteJudgementDiagnostics.cpp
        AbsoluteJudgement/JudgementClockResolver.cpp
        AbsoluteJudgement/JudgementHistory.cpp
        AbsoluteJudgement/JudgementScheduler.cpp
        AbsoluteJudgement/JudgementScope.cpp
        AbsoluteJudgement/JudgementStage.cpp
        GameCompatibility/GameBinaryPatch.cpp
        GameCompatibility/GameBinaryPatchDiagnostics.cpp
        Framerate/FramerateAuthoredClock.cpp
        Framerate/GameplaySongClock.cpp
        Framerate/FramerateDiagnostics.cpp
        Framerate/FramerateEffectTiming.cpp
        Framerate/FramerateHookTransforms.cpp
        Framerate/FramerateMenuTiming.cpp
        Framerate/FrameratePatch.cpp
        Framerate/FrameratePatchPlan.cpp
        Framerate/FramerateMonitor.cpp
        Framerate/FramerateProfile.cpp
        Framerate/FrameratePatchTransaction.cpp
        RendererDeviceLoss/RendererDeviceLossPatch.cpp
        Countdown/CountdownTimerFreeze.cpp
)
target_include_directories(gc_runtime_patches PUBLIC
        ${PROJECT_SOURCE_DIR}/src
        ${plog_SOURCE_DIR}/include
)
target_link_libraries(gc_runtime_patches PUBLIC
        gc_audio
        gc_config
        gc_input
        gc_logging
        gc_timing
        safetyhook::safetyhook
)

add_subdirectory(TestModeTiming)
