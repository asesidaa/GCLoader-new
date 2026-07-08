#include "config.h"
#include "WinKeyMapping.h"

#include <Windows.h>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>

#include "rfl/toml.hpp"

namespace {

constexpr const char* kRequiredConfigPrefix = R"toml(
axis_threshold = 16384
gamepad_index = 0
input_mode = 'Keyboard'

[gamepad]
p1_axis_horizontal = 'leftx'
p1_axis_vertical = 'lefty'
p1_button1 = 'south'
p1_dpad_down = 'dpad_down'
p1_dpad_left = 'dpad_left'
p1_dpad_right = 'dpad_right'
p1_dpad_up = 'dpad_up'
p2_axis_horizontal = 'rightx'
p2_axis_vertical = 'righty'
p2_button1 = 'east'
p2_button_down = 'invalid'
p2_button_left = 'invalid'
p2_button_right = 'invalid'
p2_button_up = 'invalid'

[keyboard]
p1_button1 = 'space'
p1_down = 's'
p1_left = 'a'
p1_right = 'd'
p1_start = '1'
p1_up = 'w'
p2_button1 = 'k'
p2_down = 'down'
p2_left = 'left'
p2_right = 'right'
p2_service = 'f2'
p2_start = '2'
p2_up = 'up'
service1 = 'f1'
service2 = 'i'
service3 = 'p'
test = 't'
)toml";

constexpr const char* kDefaultExperimentalConfig = R"toml(
card_read = 'f4'

[experimental]
enable_120fps_timer_patches = false
enable_testmode_storage_redirect = false
enable_timer_freeze_patches = false
)toml";

constexpr const char* kDefaultCardReadConfig = R"toml(
card_read = 'f4'
)toml";

constexpr const char* kDefaultExperimentalTable = R"toml(
[experimental]
enable_120fps_timer_patches = false
enable_testmode_storage_redirect = false
enable_timer_freeze_patches = false
)toml";

constexpr const char* kEnabledExperimentalConfig = R"toml(
card_read = 'f8'

[experimental]
enable_120fps_timer_patches = true
enable_testmode_storage_redirect = true
enable_timer_freeze_patches = true
)toml";

InputConfig parse_config(const std::string& toml) {
    std::istringstream stream(toml);
    auto result = rfl::toml::read<InputConfig>(stream);
    if (!result) {
        std::cerr << "Failed to parse test config: " << result.error().what() << "\n";
        std::exit(1);
    }

    return result.value();
}

int expect_parse_failure(const std::string& toml, const char* name) {
    std::istringstream stream(toml);
    auto result = rfl::toml::read<InputConfig>(stream);
    if (!result) {
        return 0;
    }

    std::cerr << "Expected parse failure for " << name << "\n";
    return 1;
}

int expect_bool(bool actual, bool expected, const char* name) {
    if (actual == expected) {
        return 0;
    }

    std::cerr << "Expected " << name << " to be " << expected
              << ", got " << actual << "\n";
    return 1;
}

int expect_key(SDL_Keycode actual, SDL_Keycode expected, const char* name) {
    if (actual == expected) {
        return 0;
    }

    std::cerr << "Expected " << name << " keycode 0x" << std::hex << expected
              << ", got 0x" << actual << std::dec << "\n";
    return 1;
}

int expect_vk(int actual, int expected, const char* name) {
    if (actual == expected) {
        return 0;
    }

    std::cerr << "Expected " << name << " virtual key 0x" << std::hex << expected
              << ", got 0x" << actual << std::dec << "\n";
    return 1;
}

int expect_string(const std::string& actual, const std::string& expected, const char* name) {
    if (actual == expected) {
        return 0;
    }

    std::cerr << "Expected " << name << " to be '" << expected
              << "', got '" << actual << "'\n";
    return 1;
}

} // namespace

int main() {
    int failures = 0;

    const auto upgraded_defaults = parse_config(
        std::string(kRequiredConfigPrefix) + kDefaultExperimentalConfig);
    failures += expect_bool(
        upgraded_defaults.experimental().enable_120fps_timer_patches(),
        false,
        "upgraded default enable_120fps_timer_patches");
    failures += expect_bool(
        upgraded_defaults.experimental().enable_timer_freeze_patches(),
        false,
        "upgraded default enable_timer_freeze_patches");
    failures += expect_bool(
        upgraded_defaults.experimental().enable_testmode_storage_redirect(),
        false,
        "upgraded default enable_testmode_storage_redirect");
    failures += expect_key(upgraded_defaults.keyboard().card_read(), SDLK_F4, "upgraded default card_read");

    const auto custom = parse_config(
        std::string(kRequiredConfigPrefix) + kEnabledExperimentalConfig);
    failures += expect_bool(
        custom.experimental().enable_120fps_timer_patches(),
        true,
        "custom enable_120fps_timer_patches");
    failures += expect_bool(
        custom.experimental().enable_timer_freeze_patches(),
        true,
        "custom enable_timer_freeze_patches");
    failures += expect_bool(
        custom.experimental().enable_testmode_storage_redirect(),
        true,
        "custom enable_testmode_storage_redirect");
    failures += expect_key(custom.keyboard().card_read(), SDLK_F8, "custom card_read");

    failures += expect_parse_failure(kRequiredConfigPrefix, "missing card_read and experimental table");
    failures += expect_parse_failure(
        std::string(kRequiredConfigPrefix) + kDefaultExperimentalTable,
        "missing card_read");
    failures += expect_parse_failure(
        std::string(kRequiredConfigPrefix) + kDefaultCardReadConfig,
        "missing experimental table");
    failures += expect_parse_failure(
        std::string(kRequiredConfigPrefix) + kDefaultCardReadConfig + R"toml(
[experimental]
enable_timer_freeze_patches = false
enable_testmode_storage_redirect = false
)toml",
        "missing enable_120fps_timer_patches");
    failures += expect_parse_failure(
        std::string(kRequiredConfigPrefix) + kDefaultCardReadConfig + R"toml(
[experimental]
enable_120fps_timer_patches = false
enable_testmode_storage_redirect = false
)toml",
        "missing enable_timer_freeze_patches");
    failures += expect_parse_failure(
        std::string(kRequiredConfigPrefix) + kDefaultCardReadConfig + R"toml(
[experimental]
enable_120fps_timer_patches = false
enable_timer_freeze_patches = false
)toml",
        "missing enable_testmode_storage_redirect");

    failures += expect_vk(SdlKeycodeToVirtualKey(SDLK_F4), VK_F4, "F4");
    failures += expect_vk(SdlKeycodeToVirtualKey(SDLK_F8), VK_F8, "F8");
    failures += expect_vk(SdlKeycodeToVirtualKey(SDLK_A), 'A', "A");
    failures += expect_vk(SdlKeycodeToVirtualKey(SDLK_UNKNOWN), 0, "unknown");
    failures += expect_vk(SdlKeycodeToVirtualKey(custom.keyboard().card_read()), VK_F8, "custom card_read");

    const auto punctuation = parse_config(
        std::string(kRequiredConfigPrefix) + R"toml(
card_read = ';'

[experimental]
enable_120fps_timer_patches = false
enable_testmode_storage_redirect = false
enable_timer_freeze_patches = false
)toml");
    failures += expect_key(punctuation.keyboard().card_read(), SDLK_SEMICOLON, "semicolon card_read");
    failures += expect_string(KeycodeToString(SDLK_SEMICOLON), ";", "semicolon display name");

    return failures == 0 ? 0 : 1;
}
