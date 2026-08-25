add_library(gc_config STATIC
        AudioConfig.cpp
        ConfigCompiler.cpp
        ConfigDocument.cpp
        ConfigError.cpp
        config.cpp
        NativeInputConfig.cpp
        RegistryConfig.cpp
)
target_include_directories(gc_config PUBLIC
        ${PROJECT_SOURCE_DIR}/src
        ${reflectcpp_SOURCE_DIR}/include
        ${plog_SOURCE_DIR}/include
)
target_link_libraries(gc_config PUBLIC
        gc_input_types
        gc_nesys_network_config
        gc_system_path
        tomlplusplus::tomlplusplus
        reflectcpp
)
