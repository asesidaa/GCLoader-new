add_subdirectory(Asio)

add_library(gc_audio STATIC
        AudioContractFatal.cpp
        AudioBackendController.cpp
        ExactJudgementTimeline.cpp
        Asio/AsioOutputBackend.cpp
        DirectSound/DirectSoundFacade.cpp
        DirectSound/DirectSoundHook.cpp
        DirectSound/GameplayAudioCursorObservation.cpp
        Mixer/AudioCursorTimeline.cpp
        Mixer/AudioRenderCore.cpp
        Mixer/AudioSnapshot.cpp
        Mixer/MiniaudioMixer.cpp
        Wasapi/ExactWasapiClock.cpp
        Wasapi/ExclusiveAudioEngine.cpp
        Wasapi/OutputPacingTracker.cpp
        AudioFeature.cpp
        AudioRuntimeState.cpp
        AudioBackendComposition.cpp
        AudioDiagnostics.cpp
        Asio/AsioCloseHook.cpp
        Asio/AsioCloseProfile.cpp
        Wasapi/WasapiAudioTypes.cpp
        Wasapi/WasapiEndpoint.cpp
        Wasapi/WasapiPresentedOutputClock.cpp
)
target_include_directories(gc_audio PUBLIC
        ${PROJECT_SOURCE_DIR}/src
        ${plog_SOURCE_DIR}/include
)
target_link_libraries(gc_audio PUBLIC
        gc_hooking
        gc_game_version
        gc_asio
        gc_timing
        miniaudio
        dsound
        dxguid
        ole32
        uuid
        avrt
        propsys
        user32
)
target_link_libraries(gc_audio PRIVATE gc_platform_win32 gc_fatal_process)
