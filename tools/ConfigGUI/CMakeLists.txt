add_executable(ConfigGUI Main.cpp)
target_include_directories(ConfigGUI PRIVATE
        ${PROJECT_SOURCE_DIR}/src
        ${SDL3_SOURCE_DIR}/include
        ${reflectcpp_SOURCE_DIR}/include
        ${plog_SOURCE_DIR}/include
)
target_link_libraries(ConfigGUI PRIVATE
        gc_config
        SDL3-static
        ntdll
        tomlplusplus::tomlplusplus
        reflectcpp
        imgui
)
