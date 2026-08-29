add_executable(gc_logical_presentation_clock_tests
        Audio/Logical/LogicalPresentationClockTests.cpp
)
target_link_libraries(gc_logical_presentation_clock_tests PRIVATE
        gc_audio
)

add_test(
        NAME LogicalPresentationClock
        COMMAND gc_logical_presentation_clock_tests
)

add_executable(gc_exact_wasapi_clock_compatibility_tests
        Audio/Wasapi/ExactWasapiClockCompatibilityTests.cpp
)
target_link_libraries(gc_exact_wasapi_clock_compatibility_tests PRIVATE
        gc_audio
)

add_test(
        NAME ExactWasapiClockCompatibility
        COMMAND gc_exact_wasapi_clock_compatibility_tests
)

add_executable(gc_asio_foreground_state_tests
        Audio/Asio/AsioForegroundStateTests.cpp
)
target_link_libraries(gc_asio_foreground_state_tests PRIVATE
        gc_asio
)

add_test(
        NAME AsioForegroundState
        COMMAND gc_asio_foreground_state_tests
)

add_executable(gc_asio_physical_session_controller_tests
        Audio/Asio/AsioPhysicalSessionControllerTests.cpp
)
target_link_libraries(gc_asio_physical_session_controller_tests PRIVATE
        gc_asio
)

add_test(
        NAME AsioPhysicalSessionController
        COMMAND gc_asio_physical_session_controller_tests
)

add_executable(gc_exact_history_isolation_tests
        Audio/Mixer/ExactHistoryIsolationTests.cpp
)
target_link_libraries(gc_exact_history_isolation_tests PRIVATE
        gc_audio
)

add_test(
        NAME ExactHistoryIsolation
        COMMAND gc_exact_history_isolation_tests
)

add_executable(gc_logical_judgement_timeline_tests
        Patches/AbsoluteJudgement/LogicalJudgementTimelineTests.cpp
)
target_link_libraries(gc_logical_judgement_timeline_tests PRIVATE
        gc_runtime_patches
)

add_test(
        NAME LogicalJudgementTimeline
        COMMAND gc_logical_judgement_timeline_tests
)

add_executable(gc_ime_suppression_tests
        Input/Win32/ImeSuppressionTests.cpp
)
target_link_libraries(gc_ime_suppression_tests PRIVATE
        gc_input_win32
)

add_test(
        NAME ImeSuppression
        COMMAND gc_ime_suppression_tests
)

add_executable(gc_config_contract_tests
        Config/ConfigContractTests.cpp
)
target_link_libraries(gc_config_contract_tests PRIVATE
        gc_config
        gc_input_win32
)
target_compile_definitions(gc_config_contract_tests PRIVATE
        GC_TEST_CONFIG_PATH="${PROJECT_SOURCE_DIR}/config.toml"
)

add_test(
        NAME ConfigContract
        COMMAND gc_config_contract_tests
)

add_executable(gc_config_startup_tests
        Config/ConfigStartupTests.cpp
)
target_link_libraries(gc_config_startup_tests PRIVATE
        gc_loader_startup
)
target_compile_definitions(gc_config_startup_tests PRIVATE
        GC_TEST_CONFIG_PATH="${PROJECT_SOURCE_DIR}/config.toml"
)

add_test(
        NAME ConfigStartup
        COMMAND gc_config_startup_tests
)
