add_subdirectory(Asio)

add_library(gc_audio STATIC
        Asio/AsioOutputBackend.cpp
        DirectSound/DirectSoundFacade.cpp
        DirectSound/GameplayAudioCursorObservation.cpp
        Mixer/AudioCursorTimeline.cpp
        Mixer/AudioRenderCore.cpp
        Mixer/AudioSnapshot.cpp
        Mixer/MiniaudioMixer.cpp
        Wasapi/ExclusiveAudioEngine.cpp
        Wasapi/OutputPacingTracker.cpp
        Wasapi/WasapiAudioPatch.cpp
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
        gc_config
        minhook
        miniaudio
        dsound
        dxguid
        ole32
        uuid
        avrt
        propsys
)
