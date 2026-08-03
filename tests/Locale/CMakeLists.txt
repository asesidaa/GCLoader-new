add_executable(JapaneseLocalePolicyTests
        JapaneseLocalePolicyTests.cpp)
target_link_libraries(JapaneseLocalePolicyTests PRIVATE
        gc_japanese_locale_policy)
add_test(NAME JapaneseLocalePolicyTests
        COMMAND JapaneseLocalePolicyTests)

add_executable(JapaneseLocaleCompatibilityTests
        JapaneseLocaleCompatibilityTests.cpp)
target_link_libraries(JapaneseLocaleCompatibilityTests PRIVATE
        gc_japanese_locale_compatibility)
add_test(NAME JapaneseLocaleCompatibilityTests
        COMMAND JapaneseLocaleCompatibilityTests)

add_executable(FilesystemDiagnosticsTests
        FilesystemDiagnosticsTests.cpp)
target_link_libraries(FilesystemDiagnosticsTests PRIVATE
        gc_locale_filesystem_diagnostics)
add_test(NAME FilesystemDiagnosticsTests
        COMMAND FilesystemDiagnosticsTests)
