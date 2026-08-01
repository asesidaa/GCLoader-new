foreach(test_target IN ITEMS
        RfidRuntimeTests
        JvsCodecTests
        JvsDeviceTests
        ComPortStateTests
)
    add_executable(${test_target} ${test_target}.cpp)
    target_link_libraries(${test_target} PRIVATE gc_rfid_core)
    add_test(NAME ${test_target} COMMAND ${test_target})
endforeach()

add_executable(FeatureHookLayerTests FeatureHookLayerTests.cpp)
target_link_libraries(FeatureHookLayerTests PRIVATE gc_rfid_feature)
add_test(NAME FeatureHookLayerTests COMMAND FeatureHookLayerTests)
