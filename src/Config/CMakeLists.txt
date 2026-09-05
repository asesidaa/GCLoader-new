add_library(gc_config STATIC
        ConfigCompiler.cpp
        ConfigDocument.cpp
        ConfigError.cpp
        DeclaredEnum.h
        RegistryConfig.cpp
)
target_include_directories(gc_config PUBLIC
        ${PROJECT_SOURCE_DIR}/src
        ${reflectcpp_SOURCE_DIR}/include
)
target_link_libraries(gc_config PUBLIC
        gc_input_types
        gc_nesys_network_config
        tomlplusplus::tomlplusplus
        reflectcpp
)
