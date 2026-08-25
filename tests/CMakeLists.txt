add_executable(gc_exact_asio_clock_tests
        Audio/Asio/ExactAsioClockTests.cpp
)
target_link_libraries(gc_exact_asio_clock_tests PRIVATE
        gc_audio
)

add_test(
        NAME ExactAsioClock
        COMMAND gc_exact_asio_clock_tests
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
