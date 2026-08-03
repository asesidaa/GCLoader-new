add_library(gc_japanese_locale_policy STATIC
        JapaneseLocalePolicy.cpp)
target_include_directories(gc_japanese_locale_policy PUBLIC
        ${PROJECT_SOURCE_DIR}/src)
