add_library(gc_fatal_process STATIC FatalProcess.cpp)
target_include_directories(gc_fatal_process PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(gc_fatal_process PRIVATE plog::plog user32)

add_library(gc_crash_dump STATIC CrashDumpHandler.cpp)
target_include_directories(gc_crash_dump PUBLIC
        ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(gc_crash_dump PUBLIC
        gc_hooking
        dbghelp
)
