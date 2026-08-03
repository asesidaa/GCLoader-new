add_executable(JapaneseLocalePolicyTests
        JapaneseLocalePolicyTests.cpp)
target_link_libraries(JapaneseLocalePolicyTests PRIVATE
        gc_japanese_locale_policy)
add_test(NAME JapaneseLocalePolicyTests
        COMMAND JapaneseLocalePolicyTests)
