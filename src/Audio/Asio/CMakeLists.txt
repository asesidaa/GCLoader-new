add_library(gc_asio STATIC
        AsioBufferRules.cpp
        AsioCapabilityProbe.cpp
        AsioControlPanelClient.cpp
        AsioDriver.cpp
        AsioDriverCatalog.cpp
        AsioIsolatedProcess.cpp
        AsioProbeClient.cpp
        AsioProbeProtocol.cpp
        AsioSampleConverter.cpp
)
target_include_directories(gc_asio PUBLIC
        ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(gc_asio PUBLIC
        gc_asio_sdk
        ole32
        advapi32
)
target_link_libraries(gc_asio PRIVATE gc_platform_win32)
