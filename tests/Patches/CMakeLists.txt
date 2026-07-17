add_executable(CountdownTimerFreezeTests
        Countdown/CountdownTimerFreezeTests.cpp)
target_link_libraries(CountdownTimerFreezeTests PRIVATE gc_runtime_patches)
add_test(NAME CountdownTimerFreezeTests COMMAND CountdownTimerFreezeTests)

add_executable(FramerateProfileTests
        Framerate/FramerateProfileTests.cpp)
target_link_libraries(FramerateProfileTests PRIVATE gc_runtime_patches)
add_test(NAME FramerateProfileTests COMMAND FramerateProfileTests)

add_executable(FramerateMonitorTests
        Framerate/FramerateMonitorTests.cpp)
target_link_libraries(FramerateMonitorTests PRIVATE gc_runtime_patches)
add_test(NAME FramerateMonitorTests COMMAND FramerateMonitorTests)

add_executable(FramerateDiagnosticsTests
        Framerate/FramerateDiagnosticsTests.cpp)
target_link_libraries(FramerateDiagnosticsTests PRIVATE gc_runtime_patches)
add_test(NAME FramerateDiagnosticsTests COMMAND FramerateDiagnosticsTests)
