add_executable(CountdownTimerFreezeTests
        Countdown/CountdownTimerFreezeTests.cpp)
target_link_libraries(CountdownTimerFreezeTests PRIVATE gc_runtime_patches)
add_test(NAME CountdownTimerFreezeTests COMMAND CountdownTimerFreezeTests)

add_executable(FramerateProfileTests
        Framerate/FramerateProfileTests.cpp)
target_link_libraries(FramerateProfileTests PRIVATE gc_runtime_patches)
add_test(NAME FramerateProfileTests COMMAND FramerateProfileTests)
