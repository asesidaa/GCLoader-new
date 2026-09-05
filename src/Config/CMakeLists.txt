add_library(gc_config STATIC
        ConfigCompiler.cpp
        Validation/InputValidation.cpp
        Validation/RegistryValidation.cpp
        Validation/ExperimentalValidation.cpp
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
target_link_libraries(gc_config PRIVATE gc_platform_win32)
