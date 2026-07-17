add_library(gc_config STATIC
        config.cpp
        RegistryConfig.cpp
)
target_include_directories(gc_config PUBLIC
        ${PROJECT_SOURCE_DIR}/src
        ${SDL3_SOURCE_DIR}/include
        ${reflectcpp_SOURCE_DIR}/include
        ${plog_SOURCE_DIR}/include
)
target_link_libraries(gc_config PUBLIC
        gc_nesys_network_config
        SDL3-static
        tomlplusplus::tomlplusplus
        reflectcpp
)
