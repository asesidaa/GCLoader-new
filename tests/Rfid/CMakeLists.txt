foreach(test_target IN ITEMS
        RfidRuntimeTests
        JvsCodecTests
        JvsDeviceTests
        ComPortStateTests
        CardReaderInterfaceTests
)
    add_executable(${test_target} ${test_target}.cpp)
    target_link_libraries(${test_target} PRIVATE gc_rfid_core)
    add_test(NAME ${test_target} COMMAND ${test_target})
endforeach()

set_tests_properties(CardReaderInterfaceTests PROPERTIES TIMEOUT 15)

add_executable(CardReaderClientTests CardReaderClientTests.cpp)
target_link_libraries(CardReaderClientTests PRIVATE
        gc_card_reader_test_client_transport
        gc_rfid_core
)
add_test(NAME CardReaderClientTests COMMAND CardReaderClientTests)
set_tests_properties(CardReaderClientTests PROPERTIES TIMEOUT 15)

add_executable(FeatureHookLayerTests FeatureHookLayerTests.cpp)
target_link_libraries(FeatureHookLayerTests PRIVATE gc_rfid_feature)
add_test(NAME FeatureHookLayerTests COMMAND FeatureHookLayerTests)
