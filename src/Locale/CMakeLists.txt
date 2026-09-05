add_library(gc_japanese_locale_policy STATIC
        JapaneseLocalePolicy.cpp)
target_include_directories(gc_japanese_locale_policy PUBLIC
        ${PROJECT_SOURCE_DIR}/src)

add_library(gc_japanese_locale_compatibility STATIC
        JapaneseLocaleCompatibility.cpp)
target_include_directories(gc_japanese_locale_compatibility PUBLIC
        ${PROJECT_SOURCE_DIR}/src
        ${plog_SOURCE_DIR}/include)
target_link_libraries(gc_japanese_locale_compatibility PUBLIC
        gc_hooking
        gc_japanese_locale_policy
        gc_nesys_process)
