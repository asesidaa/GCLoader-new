add_library(gc_win32_hooks STATIC Kernel32Dispatcher.cpp Kernel32Detours.cpp)
target_include_directories(gc_win32_hooks PUBLIC
        ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(gc_win32_hooks PUBLIC
        gc_hooking
)
