add_library(gc_asio STATIC
        AsioBufferRules.cpp
        AsioSampleConverter.cpp
)
target_include_directories(gc_asio PUBLIC
        ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(gc_asio PUBLIC
        gc_asio_sdk
        ole32
        advapi32
        avrt
        winmm
)
