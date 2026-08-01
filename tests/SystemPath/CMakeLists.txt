add_executable(SystemRootTests SystemRootTests.cpp)
target_link_libraries(SystemRootTests PRIVATE gc_system_path)
add_test(NAME SystemRootTests COMMAND SystemRootTests)

add_executable(SystemPathRouterTests SystemPathRouterTests.cpp)
target_link_libraries(SystemPathRouterTests PRIVATE gc_system_path)
add_test(NAME SystemPathRouterTests COMMAND SystemPathRouterTests)

add_executable(TtxInitGuardTests TtxInitGuardTests.cpp)
target_link_libraries(TtxInitGuardTests PRIVATE gc_system_path)
add_test(NAME TtxInitGuardTests COMMAND TtxInitGuardTests)

add_executable(SystemPathIntegrationTests SystemPathIntegrationTests.cpp)
target_compile_definitions(SystemPathIntegrationTests PRIVATE
        GC_TEST_CONFIG_PATH="${PROJECT_SOURCE_DIR}/config.toml")
target_link_libraries(SystemPathIntegrationTests PRIVATE
        gc_config
        gc_system_path
)
add_test(NAME SystemPathIntegrationTests COMMAND SystemPathIntegrationTests)
