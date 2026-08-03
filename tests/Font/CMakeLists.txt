add_executable(FontCharsetCompatibilityTests
        FontCharsetCompatibilityTests.cpp)
target_link_libraries(FontCharsetCompatibilityTests PRIVATE
        gc_font_compatibility)
add_test(NAME FontCharsetCompatibilityTests
        COMMAND FontCharsetCompatibilityTests)
