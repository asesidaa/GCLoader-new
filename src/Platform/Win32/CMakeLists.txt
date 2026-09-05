add_subdirectory(Hooking)

add_library(gc_platform_win32 STATIC
        Utf.cpp
        Win32Error.cpp
        UniqueHandle.h
)
target_include_directories(gc_platform_win32 PUBLIC
        ${PROJECT_SOURCE_DIR}/src
)
