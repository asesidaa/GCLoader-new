add_library(gc_crash_dump STATIC CrashDumpHandler.cpp)
target_include_directories(gc_crash_dump PUBLIC
        ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(gc_crash_dump PUBLIC
        gc_hooking
        dbghelp
)
