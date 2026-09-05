add_library(gc_hooking STATIC
        HookPlan.cpp
        HookRegistry.cpp
        HookDiagnostics.cpp
)
target_include_directories(gc_hooking PUBLIC
        ${PROJECT_SOURCE_DIR}/src
        ${plog_SOURCE_DIR}/include
)
target_link_libraries(gc_hooking PUBLIC safetyhook::safetyhook PRIVATE gc_fatal_process)
