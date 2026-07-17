add_library(gc_audio STATIC
        DirectSound/DirectSoundFacade.cpp
        Mixer/AudioCursorTimeline.cpp
        Mixer/AudioSnapshot.cpp
        Mixer/MiniaudioMixer.cpp
        Wasapi/ExclusiveAudioEngine.cpp
        Wasapi/OutputPacingTracker.cpp
        Wasapi/WasapiAudioPatch.cpp
        Wasapi/WasapiAudioTypes.cpp
        Wasapi/WasapiEndpoint.cpp
)
target_include_directories(gc_audio PUBLIC
        ${PROJECT_SOURCE_DIR}/src
        ${SDL3_SOURCE_DIR}/include
        ${minhook_SOURCE_DIR}/include
        ${plog_SOURCE_DIR}/include
)
target_link_libraries(gc_audio PUBLIC
        gc_config
        SDL3-static
        minhook
        miniaudio
        dsound
        dxguid
        ole32
        uuid
        avrt
        propsys
)
