add_library(gc_font_compatibility STATIC
        FontCharsetCompatibility.cpp
)
target_include_directories(gc_font_compatibility PUBLIC
        ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(gc_font_compatibility PUBLIC
        gc_hooking
)
