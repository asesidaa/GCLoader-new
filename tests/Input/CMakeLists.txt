add_executable(InputTypesTests Types/InputTypesTests.cpp)
target_link_libraries(InputTypesTests PRIVATE gc_input_types)
add_test(NAME InputTypesTests COMMAND InputTypesTests)

add_executable(InputSnapshotStateTests Polling/InputSnapshotStateTests.cpp)
target_link_libraries(InputSnapshotStateTests PRIVATE gc_input)
add_test(NAME InputSnapshotStateTests COMMAND InputSnapshotStateTests)

add_executable(InputPollingRuntimeStartupTests
        Polling/InputPollingRuntimeStartupTests.cpp)
target_link_libraries(InputPollingRuntimeStartupTests PRIVATE gc_input)
add_test(NAME InputPollingRuntimeStartupTests
        COMMAND InputPollingRuntimeStartupTests)
set_tests_properties(InputPollingRuntimeStartupTests PROPERTIES
        TIMEOUT 5
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
)

add_executable(SwitchInputPolicyTests Switch/SwitchInputPolicyTests.cpp)
target_link_libraries(SwitchInputPolicyTests PRIVATE gc_input)
add_test(NAME SwitchInputPolicyTests COMMAND SwitchInputPolicyTests)

add_executable(SwitchInputPatchTests Switch/SwitchInputPatchTests.cpp)
target_link_libraries(SwitchInputPatchTests PRIVATE gc_input)
add_test(NAME SwitchInputPatchTests COMMAND SwitchInputPatchTests)
