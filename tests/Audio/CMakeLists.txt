set(GC_AUDIO_TESTS
        AudioCursorTimelineTests
        AudioFormatTests
        AudioRenderCoreTests
        AudioSnapshotTests
        DirectSoundDeviceTests
        ExclusiveAudioEngineTests
        GameplayAudioCursorObservationTests
        MiniaudioMixerTests
        OutputPacingTrackerTests
        SecondarySoundBufferTests
        WasapiAudioPatchTests
        WasapiEndpointTests
        WasapiPresentedOutputClockTests
)

foreach(test_target IN LISTS GC_AUDIO_TESTS)
    add_executable(${test_target} ${test_target}.cpp)
    target_link_libraries(${test_target} PRIVATE gc_audio)
    add_test(NAME ${test_target} COMMAND ${test_target})
endforeach()

add_executable(AsioBufferRulesTests AsioBufferRulesTests.cpp)
target_link_libraries(AsioBufferRulesTests PRIVATE gc_asio)
add_test(NAME AsioBufferRulesTests COMMAND AsioBufferRulesTests)

add_executable(AsioSampleConverterTests AsioSampleConverterTests.cpp)
target_link_libraries(AsioSampleConverterTests PRIVATE gc_asio)
add_test(NAME AsioSampleConverterTests COMMAND AsioSampleConverterTests)

add_executable(AsioDriverCatalogTests AsioDriverCatalogTests.cpp)
target_link_libraries(AsioDriverCatalogTests PRIVATE gc_asio)
add_test(NAME AsioDriverCatalogTests COMMAND AsioDriverCatalogTests)

add_executable(AsioDriverTests AsioDriverTests.cpp)
target_link_libraries(AsioDriverTests PRIVATE gc_asio)
add_test(NAME AsioDriverTests COMMAND AsioDriverTests)

add_executable(AsioSessionTests AsioSessionTests.cpp)
target_link_libraries(AsioSessionTests PRIVATE gc_asio)
add_test(NAME AsioSessionTests COMMAND AsioSessionTests)

add_executable(AsioCapabilityProbeTests AsioCapabilityProbeTests.cpp)
target_link_libraries(AsioCapabilityProbeTests PRIVATE gc_asio)
add_test(NAME AsioCapabilityProbeTests COMMAND AsioCapabilityProbeTests)

add_executable(AsioClockTests AsioClockTests.cpp)
target_link_libraries(AsioClockTests PRIVATE gc_asio)
add_test(NAME AsioClockTests COMMAND AsioClockTests)

add_executable(AsioCallbackRuntimeTests AsioCallbackRuntimeTests.cpp)
target_link_libraries(AsioCallbackRuntimeTests PRIVATE gc_asio)
add_test(NAME AsioCallbackRuntimeTests COMMAND AsioCallbackRuntimeTests)

add_executable(AsioProbeProtocolTests AsioProbeProtocolTests.cpp)
target_link_libraries(AsioProbeProtocolTests PRIVATE gc_asio)
add_test(NAME AsioProbeProtocolTests COMMAND AsioProbeProtocolTests)

add_executable(AsioProbeClientTests AsioProbeClientTests.cpp)
target_link_libraries(AsioProbeClientTests PRIVATE gc_asio)
target_compile_definitions(AsioProbeClientTests PRIVATE
        GC_ASIO_PROBE_TEST_PATH="$<TARGET_FILE:AsioProbe>"
)
add_dependencies(AsioProbeClientTests AsioProbe)
add_test(NAME AsioProbeClientTests COMMAND AsioProbeClientTests)

add_executable(AsioOutputBackendTests AsioOutputBackendTests.cpp)
target_link_libraries(AsioOutputBackendTests PRIVATE gc_audio)
add_test(NAME AsioOutputBackendTests COMMAND AsioOutputBackendTests)
