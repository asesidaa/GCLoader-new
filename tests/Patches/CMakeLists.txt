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

add_executable(FrameratePatchTransactionTests
        Framerate/FrameratePatchTransactionTests.cpp)
target_link_libraries(FrameratePatchTransactionTests PRIVATE gc_runtime_patches)
add_test(NAME FrameratePatchTransactionTests COMMAND FrameratePatchTransactionTests)

add_executable(FrameratePatchPlanTests
        Framerate/FrameratePatchPlanTests.cpp)
target_link_libraries(FrameratePatchPlanTests PRIVATE gc_runtime_patches)
add_test(NAME FrameratePatchPlanTests COMMAND FrameratePatchPlanTests)

add_executable(FramerateRuntimeTests
        Framerate/FramerateRuntimeTests.cpp)
target_link_libraries(FramerateRuntimeTests PRIVATE gc_runtime_patches)
add_test(NAME FramerateRuntimeTests COMMAND FramerateRuntimeTests)

add_executable(FramerateAuthoredClockTests
        Framerate/FramerateAuthoredClockTests.cpp)
target_link_libraries(FramerateAuthoredClockTests PRIVATE gc_runtime_patches)
add_test(NAME FramerateAuthoredClockTests COMMAND FramerateAuthoredClockTests)

add_subdirectory(TestModeTiming)
