add_executable(CountdownTimerFreezeTests
        Countdown/CountdownTimerFreezeTests.cpp)
target_link_libraries(CountdownTimerFreezeTests PRIVATE gc_runtime_patches)
add_test(NAME CountdownTimerFreezeTests COMMAND CountdownTimerFreezeTests)
