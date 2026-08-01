add_library(gc_system_path STATIC
        StartupFatal.cpp
        SystemPathRouter.cpp
        SystemRoot.cpp
        TtxInitGuard.cpp
)
target_include_directories(gc_system_path PUBLIC
        ${PROJECT_SOURCE_DIR}/src
        ${plog_SOURCE_DIR}/include
)
target_link_libraries(gc_system_path PUBLIC safetyhook::safetyhook)
