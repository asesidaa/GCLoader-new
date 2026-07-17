add_library(gc_nesys_network_config STATIC
        NesysNetworkConfig.cpp
)
target_include_directories(gc_nesys_network_config PUBLIC
        ${PROJECT_SOURCE_DIR}/src
)
