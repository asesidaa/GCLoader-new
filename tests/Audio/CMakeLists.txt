set(GC_AUDIO_TESTS
        AudioCursorTimelineTests
        AudioFormatTests
        AudioSnapshotTests
        DirectSoundDeviceTests
        ExclusiveAudioEngineTests
        GameplayAudioCursorObservationTests
        MiniaudioMixerTests
        OutputPacingTrackerTests
        SecondarySoundBufferTests
        WasapiAudioPatchTests
        WasapiEndpointTests
)

foreach(test_target IN LISTS GC_AUDIO_TESTS)
    add_executable(${test_target} ${test_target}.cpp)
    target_link_libraries(${test_target} PRIVATE gc_audio)
    add_test(NAME ${test_target} COMMAND ${test_target})
endforeach()

add_executable(AsioBufferRulesTests AsioBufferRulesTests.cpp)
target_link_libraries(AsioBufferRulesTests PRIVATE gc_asio)
add_test(NAME AsioBufferRulesTests COMMAND AsioBufferRulesTests)
