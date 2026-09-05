add_subdirectory(RuntimeImage)
add_subdirectory(GameVersion)

add_library(gc_patch_game_compatibility STATIC
        GameCompatibility/GameCompatibilityProfile.cpp
)
target_include_directories(gc_patch_game_compatibility PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(gc_patch_game_compatibility
        PUBLIC gc_game_version
)

add_library(gc_patch_auto_play STATIC
        AutoPlay/AutoPlayMarker.cpp
        AutoPlay/AutoPlayPatch.cpp
        AutoPlay/AutoPlayPatchDiagnostics.cpp
        AutoPlay/AutoPlayProfile.cpp
)
target_include_directories(gc_patch_auto_play PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(gc_patch_auto_play
        PUBLIC gc_game_version
        PRIVATE gc_fatal_process plog::plog
)

add_library(gc_patch_song_unlock STATIC
        SongUnlock/SongUnlockProfile.cpp
)
target_include_directories(gc_patch_song_unlock PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(gc_patch_song_unlock
        PUBLIC gc_game_version
)

add_library(gc_patch_absolute_judgement STATIC
        AbsoluteJudgement/AbsoluteJudgementDiagnostics.cpp
        AbsoluteJudgement/AbsoluteJudgementPatch.cpp
        AbsoluteJudgement/AbsoluteJudgementProfile.cpp
        AbsoluteJudgement/AbsoluteJudgementRuntime.cpp
        AbsoluteJudgement/JudgementClockResolver.cpp
        AbsoluteJudgement/JudgementHistory.cpp
        AbsoluteJudgement/JudgementScheduler.cpp
        AbsoluteJudgement/JudgementScope.cpp
        AbsoluteJudgement/JudgementStage.cpp
)
target_include_directories(gc_patch_absolute_judgement PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(gc_patch_absolute_judgement
        PUBLIC gc_game_version gc_audio gc_input gc_timing
        PRIVATE gc_fatal_process plog::plog
)

add_library(gc_patch_framerate STATIC
        Framerate/FramerateAuthoredClock.cpp
        Framerate/GameplaySongClock.cpp
        Framerate/FramerateDiagnostics.cpp
        Framerate/FramerateEffectTiming.cpp
        Framerate/FramerateHookTransforms.cpp
        Framerate/FramerateMenuTiming.cpp
        Framerate/FramerateFeature.cpp
        Framerate/FramerateTimingRuntime.cpp
        Framerate/FrameTimingHooks.cpp
        Framerate/EffectTimingHooks.cpp
        Framerate/MenuTimingHooks.cpp
        Framerate/FrameratePatchPlan.cpp
        Framerate/FramerateMonitor.cpp
        Framerate/FramerateTimingProfile.cpp
        Framerate/FramerateGameProfile.cpp
)
target_include_directories(gc_patch_framerate PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(gc_patch_framerate
        PUBLIC gc_game_version gc_audio
        PRIVATE gc_patch_absolute_judgement gc_fatal_process gc_platform_win32 gc_timing plog::plog
)

add_library(gc_patch_countdown STATIC
        Countdown/CountdownProfile.cpp
)
target_include_directories(gc_patch_countdown PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(gc_patch_countdown
        PUBLIC gc_game_version
)

add_library(gc_renderer_device_loss STATIC
        RendererDeviceLoss/RendererDeviceLossProfile.cpp
        RendererDeviceLoss/RendererDeviceLossPatch.cpp
        RendererDeviceLoss/RendererResourceLifecycle.cpp
)
target_include_directories(gc_renderer_device_loss PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(gc_renderer_device_loss
        PUBLIC gc_game_version
        PRIVATE gc_fatal_process
)

add_library(gc_windowed_widescreen STATIC
        WindowedWidescreen/D3D9CompositorDevice.cpp
        WindowedWidescreen/GameplayFeedbackPlacement.cpp
        WindowedWidescreen/NativeWindowPolicy.cpp
        WindowedWidescreen/NativeCanvasCompositor.cpp
        WindowedWidescreen/PassClassifier.cpp
        WindowedWidescreen/RenderSpacePolicy.cpp
        WindowedWidescreen/ResolutionModel.cpp
        WindowedWidescreen/WindowedWidescreenAbi.cpp
        WindowedWidescreen/WindowedWidescreenProfile.cpp
        WindowedWidescreen/WindowedWidescreenFeature.cpp
        WindowedWidescreen/WidescreenRuntime.cpp
        WindowedWidescreen/WindowHooks.cpp
        WindowedWidescreen/RenderHooks.cpp
        WindowedWidescreen/GameplayHudHooks.cpp
        WindowedWidescreen/NetworkStatusHooks.cpp
)
target_include_directories(gc_windowed_widescreen PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(gc_windowed_widescreen
        PUBLIC gc_game_version gc_renderer_device_loss
        PRIVATE gc_fatal_process plog::plog
)

add_library(gc_patch_switch_input STATIC
        ../Input/Switch/SwitchInputPatch.cpp
        ../Input/Switch/SwitchInputProfile.cpp
        ../Input/Switch/SwitchInputPolicy.cpp
)
target_include_directories(gc_patch_switch_input PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(gc_patch_switch_input
        PUBLIC gc_game_version gc_input_types
        PRIVATE gc_fatal_process plog::plog
)

add_subdirectory(TestModeTiming)
