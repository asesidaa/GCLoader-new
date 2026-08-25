add_subdirectory(Asio)

add_library(gc_audio STATIC
        AudioBackendController.cpp
        ExactOutputClock.cpp
        Asio/ExactAsioClock.cpp
        Asio/AsioOutputBackend.cpp
        DirectSound/DirectSoundFacade.cpp
        DirectSound/GameplayAudioCursorObservation.cpp
        Mixer/AudioCursorTimeline.cpp
        Mixer/AudioRenderCore.cpp
        Mixer/AudioSnapshot.cpp
        Mixer/MiniaudioMixer.cpp
        Wasapi/ExactWasapiClock.cpp
        Wasapi/ExclusiveAudioEngine.cpp
        Wasapi/OutputPacingTracker.cpp
        AudioPatch.cpp
        Wasapi/WasapiAudioTypes.cpp
        Wasapi/WasapiEndpoint.cpp
        Wasapi/WasapiPresentedOutputClock.cpp
)
target_include_directories(gc_audio PUBLIC
        ${PROJECT_SOURCE_DIR}/src
        ${minhook_SOURCE_DIR}/include
        ${plog_SOURCE_DIR}/include
)
target_link_libraries(gc_audio PUBLIC
        gc_asio
        gc_timing
        minhook
        miniaudio
        dsound
        dxguid
        ole32
        uuid
        avrt
        propsys
)
