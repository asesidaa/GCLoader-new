#include "Config/config.h"
#include "Config/TargetFps.h"
#include "Input/Types/PhysicalKey.h"
#include "Input/Win32/PhysicalKeyWin32.h"
#include "Nesys/Network/NesysNetworkConfig.h"

#include <Windows.h>
#include <array>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

#include "rfl/toml.hpp"

namespace {

static_assert(
    sizeof(WasapiBufferMillisecondsConfigValue) == sizeof(std::uint32_t));

constexpr const char* kRequiredConfigPrefix = R"toml(
input_schema_version = 2
input_poll_hz = 1000
input_mode = 'Keyboard'
gameplay_input_style = 'Arcade'
axis_press_threshold_percent = 50
axis_release_threshold_percent = 40

[keyboard]
left_booster_up = 'sc:0011'
left_booster_down = 'sc:001f'
left_booster_left = 'sc:001e'
left_booster_right = 'sc:0020'
left_booster_button = 'sc:0039'
right_booster_up = 'e0:0048'
right_booster_down = 'e0:0050'
right_booster_left = 'e0:004b'
right_booster_right = 'e0:004d'
right_booster_button = 'sc:0025'
test = 'sc:0014'
service1 = 'sc:003b'
service2 = 'sc:0017'
service3 = 'sc:0019'
p1_start = 'sc:0002'
p2_start = 'sc:0003'
p2_service = 'sc:003c'
card_read = 'sc:003e'

[controller]
backend = 'XInput'
device_id = '0'
bindings = [
  { action = 'LeftBoosterUp', type = 'XInputButton', control = 'DPadUp' },
  { action = 'LeftBoosterUp', type = 'XInputAxis', control = 'LeftY', direction = 'Negative' },
  { action = 'LeftBoosterDown', type = 'XInputButton', control = 'DPadDown' },
  { action = 'LeftBoosterDown', type = 'XInputAxis', control = 'LeftY', direction = 'Positive' },
  { action = 'LeftBoosterLeft', type = 'XInputButton', control = 'DPadLeft' },
  { action = 'LeftBoosterLeft', type = 'XInputAxis', control = 'LeftX', direction = 'Negative' },
  { action = 'LeftBoosterRight', type = 'XInputButton', control = 'DPadRight' },
  { action = 'LeftBoosterRight', type = 'XInputAxis', control = 'LeftX', direction = 'Positive' },
  { action = 'LeftBoosterButton', type = 'XInputButton', control = 'A' },
  { action = 'RightBoosterUp', type = 'XInputAxis', control = 'RightY', direction = 'Negative' },
  { action = 'RightBoosterDown', type = 'XInputAxis', control = 'RightY', direction = 'Positive' },
  { action = 'RightBoosterLeft', type = 'XInputAxis', control = 'RightX', direction = 'Negative' },
  { action = 'RightBoosterRight', type = 'XInputAxis', control = 'RightX', direction = 'Positive' },
  { action = 'RightBoosterButton', type = 'XInputButton', control = 'B' },
]

[logging]
level = 'Info'
)toml";

constexpr std::string_view kDefaultRegistryConfig = R"toml(
[registry]
enabled = false

[registry.game]
country = 'GrooveCoasterJpn'

[registry.nesys]
game_kind = 303801
event_next_time = 900
condition_time = 300
log_level = 3
news_path = 'D:\system\DUA\news'
event_path = 'D:\system\DUA\event'
log_path = 'D:\system\CmdFile\log'

)toml";

constexpr const char* kDefaultExperimentalConfig = R"toml(
[nesys]
server_ip = '127.0.0.1'

[registry]
enabled = false

[registry.game]
country = 'GrooveCoasterJpn'

[registry.nesys]
game_kind = 303801
event_next_time = 900
condition_time = 300
log_level = 3
news_path = 'D:\system\DUA\news'
event_path = 'D:\system\DUA\event'
log_path = 'D:\system\CmdFile\log'

[experimental]
target_fps = 60
enable_testmode_storage_redirect = false
enable_timer_freeze_patches = false
enable_nesys_service_adapter_patch = true
enable_wasapi_exclusive_audio = false
wasapi_exclusive_buffer_ms = 10
)toml";

constexpr const char* kDefaultCardReadConfig = R"toml(
[nesys]
server_ip = '127.0.0.1'

[registry]
enabled = false

[registry.game]
country = 'GrooveCoasterJpn'

[registry.nesys]
game_kind = 303801
event_next_time = 900
condition_time = 300
log_level = 3
news_path = 'D:\system\DUA\news'
event_path = 'D:\system\DUA\event'
log_path = 'D:\system\CmdFile\log'
)toml";

constexpr const char* kDefaultExperimentalTable = R"toml(
[nesys]
server_ip = '127.0.0.1'

[registry]
enabled = false

[registry.game]
country = 'GrooveCoasterJpn'

[registry.nesys]
game_kind = 303801
event_next_time = 900
condition_time = 300
log_level = 3
news_path = 'D:\system\DUA\news'
event_path = 'D:\system\DUA\event'
log_path = 'D:\system\CmdFile\log'

[experimental]
target_fps = 60
enable_testmode_storage_redirect = false
enable_timer_freeze_patches = false
enable_nesys_service_adapter_patch = true
enable_wasapi_exclusive_audio = false
wasapi_exclusive_buffer_ms = 10
)toml";

constexpr const char* kEnabledExperimentalConfig = R"toml(
[nesys]
server_ip = '127.0.0.1'

[registry]
enabled = false

[registry.game]
country = 'GrooveCoasterJpn'

[registry.nesys]
game_kind = 303801
event_next_time = 900
condition_time = 300
log_level = 3
news_path = 'D:\system\DUA\news'
event_path = 'D:\system\DUA\event'
log_path = 'D:\system\CmdFile\log'

[experimental]
target_fps = 240
enable_testmode_storage_redirect = true
enable_timer_freeze_patches = true
enable_nesys_service_adapter_patch = false
enable_wasapi_exclusive_audio = true
wasapi_exclusive_buffer_ms = 20
)toml";

InputConfig parse_config(const std::string& toml) {
    auto result = gc::config::ParseAndValidateInputConfig(toml);
    if (!result) {
        std::cerr << "Failed to parse test config: " << result.error() << "\n";
        std::exit(1);
    }
    return std::move(result.value());
}

int expect_parse_failure(const std::string& toml, const char* name) {
    if (!gc::config::ParseAndValidateInputConfig(toml)) {
        return 0;
    }
    std::cerr << "Expected parse failure for " << name << "\n";
    return 1;
}

int expect_parse_failure_contains(
    const std::string& text,
    std::string_view expected,
    const char* name) {
    const auto result = gc::config::ParseAndValidateInputConfig(text);
    if (!result && result.error().find(expected) != std::string::npos) {
        return 0;
    }
    std::cerr << "Expected parse failure for " << name << " containing '"
              << expected << "', got '"
              << (result ? "success" : result.error()) << "'\n";
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

int expect_key(
    gc::input::PhysicalKey actual,
    gc::input::PhysicalKey expected,
    const char* name) {
    if (actual == expected) {
        return 0;
    }

    std::cerr << "Expected " << name << " physical key "
              << gc::input::FormatPhysicalKey(expected) << ", got "
              << gc::input::FormatPhysicalKey(actual) << "\n";
    return 1;
}

int expect_poll_rate_validation(
    std::uint32_t value,
    bool expected_valid,
    const char* name) {
    try {
        ValidateInputPollHertz(value);
        if (expected_valid) {
            return 0;
        }
    } catch (const std::runtime_error&) {
        if (!expected_valid) {
            return 0;
        }
    }

    std::cerr << "Expected " << name << " validation to be "
              << expected_valid << "\n";
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

int expect_ipv4_valid(std::string_view value, bool expected, const char* name) {
    const bool actual = gc::nesys_service::IsDottedDecimalIpv4(value);
    if (actual == expected) {
        return 0;
    }

    std::cerr << "Expected " << name << " validity to be " << expected
              << ", got " << actual << "\n";
    return 1;
}

int expect_style(
    gc::input::GameplayInputStyle actual,
    gc::input::GameplayInputStyle expected,
    const char* name) {
    if (actual == expected) {
        return 0;
    }

    std::cerr << "Expected " << name << " enum value "
              << static_cast<int>(expected) << ", got "
              << static_cast<int>(actual) << "\n";
    return 1;
}

int expect_country(
    GameCountry actual,
    GameCountry expected,
    const char* name) {
    if (actual == expected) {
        return 0;
    }
    std::cerr << "Expected " << name << " country value "
              << static_cast<std::uint32_t>(expected) << ", got "
              << static_cast<std::uint32_t>(actual) << "\n";
    return 1;
}

int expect_log_level(
    gc::config::LoaderLogLevel actual,
    gc::config::LoaderLogLevel expected,
    const char* name) {
    if (actual == expected) {
        return 0;
    }
    std::cerr << "Expected " << name << " log level "
              << static_cast<int>(expected) << ", got "
              << static_cast<int>(actual) << "\n";
    return 1;
}

int expect_u32(
    std::uint32_t actual,
    std::uint32_t expected,
    const char* name) {
    if (actual == expected) {
        return 0;
    }
    std::cerr << "Expected " << name << " to be " << expected
              << ", got " << actual << "\n";
    return 1;
}

int expect_i64(
    std::int64_t actual,
    std::int64_t expected,
    const char* name) {
    if (actual == expected) {
        return 0;
    }
    std::cerr << "Expected " << name << " to be " << expected
              << ", got " << actual << "\n";
    return 1;
}

int expect_registry_valid(
    const RegistryConfig& config,
    bool expected,
    const char* name) {
    const bool actual =
        gc::registry_config::ValidateRegistryConfig(config).valid();
    if (actual == expected) {
        return 0;
    }
    std::cerr << "Expected " << name << " registry validity to be "
              << expected << ", got " << actual << "\n";
    return 1;
}

std::string replace_once(
    std::string input,
    std::string_view needle,
    std::string_view replacement) {
    const auto position = input.find(needle);
    if (position == std::string::npos) {
        std::cerr << "Test fixture did not contain '" << needle << "'\n";
        std::exit(1);
    }

    input.replace(position, needle.size(), replacement);
    return input;
}

} // namespace

int main() {
    int failures = 0;

    const auto upgraded_defaults = parse_config(
        std::string(kRequiredConfigPrefix) + kDefaultExperimentalConfig);
    failures += expect_string(
        upgraded_defaults.nesys().server_ip(),
        "127.0.0.1",
        "default NESYS server IPv4");

    InputConfig generated_defaults{};
    failures += expect_string(
        generated_defaults.nesys().server_ip(),
        "127.0.0.1",
        "constructed ConfigGUI NESYS server IPv4");
    failures += expect_log_level(
        upgraded_defaults.logging().level(),
        gc::config::LoaderLogLevel::Info,
        "parsed default loader");
    failures += expect_log_level(
        generated_defaults.logging().level(),
        gc::config::LoaderLogLevel::Info,
        "constructed default loader");
    const auto generated_toml = rfl::toml::write(generated_defaults);
    failures += expect_bool(
        generated_toml.find("level = 'Info'") != std::string::npos,
        true,
        "generated TOML loader log level");

    for (const auto [name, expected] : std::array{
             std::pair{"Info", gc::config::LoaderLogLevel::Info},
             std::pair{"Debug", gc::config::LoaderLogLevel::Debug},
             std::pair{"Verbose", gc::config::LoaderLogLevel::Verbose},
         }) {
        const auto custom_level = parse_config(replace_once(
            std::string(kRequiredConfigPrefix) + kDefaultExperimentalConfig,
            "level = 'Info'",
            std::string{"level = '"} + name + "'"));
        failures += expect_log_level(
            custom_level.logging().level(), expected, name);
        const auto round_trip = parse_config(rfl::toml::write(custom_level));
        failures += expect_log_level(
            round_trip.logging().level(), expected, "loader level round trip");
    }
    failures += expect_parse_failure(
        replace_once(
            std::string(kRequiredConfigPrefix) + kDefaultExperimentalConfig,
            "level = 'Info'",
            "level = 'Trace'"),
        "unsupported loader log level");

    failures += expect_u32(
        upgraded_defaults.input_poll_hz(),
        1000,
        "upgraded default input_poll_hz");
    failures += expect_u32(
        generated_defaults.input_poll_hz(),
        1000,
        "constructed ConfigGUI input_poll_hz");
    failures += expect_bool(
        generated_toml.find("input_poll_hz = 1000") != std::string::npos,
        true,
        "generated TOML input_poll_hz");

    for (const auto rate : std::array<std::uint32_t, 4>{
             125, 250, 500, 1000}) {
        failures += expect_poll_rate_validation(
            rate,
            true,
            "supported input poll rate");
    }
    failures += expect_poll_rate_validation(
        0,
        false,
        "zero input poll rate");
    failures += expect_poll_rate_validation(
        333,
        false,
        "unsupported input poll rate");
    failures += expect_poll_rate_validation(
        2000,
        false,
        "too-high input poll rate");

    failures += expect_parse_failure(
        replace_once(
            std::string(kRequiredConfigPrefix) + kDefaultExperimentalConfig,
            "input_poll_hz = 1000\n",
            ""),
        "missing input_poll_hz");

    const auto custom_poll_config = parse_config(replace_once(
        std::string(kRequiredConfigPrefix) + kDefaultExperimentalConfig,
        "input_poll_hz = 1000",
        "input_poll_hz = 250"));
    failures += expect_u32(
        custom_poll_config.input_poll_hz(),
        250,
        "custom input_poll_hz");
    const auto reparsed_poll_config =
        parse_config(rfl::toml::write(custom_poll_config));
    failures += expect_u32(
        reparsed_poll_config.input_poll_hz(),
        250,
        "input_poll_hz TOML round trip");

    using gc::input::PhysicalKey;
    using gc::input::ScanCodePrefix;
    failures += expect_key(
        generated_defaults.keyboard().left_booster_up(),
        PhysicalKey{0x11, ScanCodePrefix::None},
        "default left booster up");
    failures += expect_key(
        generated_defaults.keyboard().left_booster_down(),
        PhysicalKey{0x1f, ScanCodePrefix::None},
        "default left booster down");
    failures += expect_key(
        generated_defaults.keyboard().left_booster_left(),
        PhysicalKey{0x1e, ScanCodePrefix::None},
        "default left booster left");
    failures += expect_key(
        generated_defaults.keyboard().left_booster_right(),
        PhysicalKey{0x20, ScanCodePrefix::None},
        "default left booster right");
    failures += expect_key(
        generated_defaults.keyboard().left_booster_button(),
        PhysicalKey{0x39, ScanCodePrefix::None},
        "default left booster center button");
    failures += expect_key(
        generated_defaults.keyboard().right_booster_up(),
        PhysicalKey{0x48, ScanCodePrefix::E0},
        "default right booster up");
    failures += expect_key(
        generated_defaults.keyboard().right_booster_down(),
        PhysicalKey{0x50, ScanCodePrefix::E0},
        "default right booster down");
    failures += expect_key(
        generated_defaults.keyboard().right_booster_left(),
        PhysicalKey{0x4b, ScanCodePrefix::E0},
        "default right booster left");
    failures += expect_key(
        generated_defaults.keyboard().right_booster_right(),
        PhysicalKey{0x4d, ScanCodePrefix::E0},
        "default right booster right");
    failures += expect_key(
        generated_defaults.keyboard().right_booster_button(),
        PhysicalKey{0x25, ScanCodePrefix::None},
        "default right booster center button");
    failures += expect_bool(
        generated_defaults.controller().backend() ==
            gc::input::ControllerBackend::XInput,
        true,
        "default controller backend");
    failures += expect_string(
        generated_defaults.controller().device_id(),
        "0",
        "default controller identity");
    failures += expect_u32(
        static_cast<std::uint32_t>(
            generated_defaults.controller().bindings().size()),
        0,
        "constructed controller binding count");

    failures += expect_bool(
        generated_toml.find("[nesys]") != std::string::npos,
        true,
        "generated TOML NESYS table");
    failures += expect_bool(
        generated_toml.find("server_ip") != std::string::npos,
        true,
        "generated TOML NESYS server field");

    failures += expect_bool(
        upgraded_defaults.registry().enabled(),
        false,
        "default registry enabled");
    failures += expect_country(
        upgraded_defaults.registry().game().country(),
        GameCountry::GrooveCoasterJpn,
        "default game country");
    failures += expect_i64(
        upgraded_defaults.registry().nesys().game_kind(),
        303801,
        "default registry game_kind");
    failures += expect_i64(
        upgraded_defaults.registry().nesys().event_next_time(),
        900,
        "default registry event_next_time");
    failures += expect_i64(
        upgraded_defaults.registry().nesys().condition_time(),
        300,
        "default registry condition_time");
    failures += expect_i64(
        upgraded_defaults.registry().nesys().log_level(),
        3,
        "default registry log_level");
    failures += expect_string(
        upgraded_defaults.registry().nesys().news_path(),
        "D:\\system\\DUA\\news",
        "default registry news_path");
    failures += expect_string(
        upgraded_defaults.registry().nesys().event_path(),
        "D:\\system\\DUA\\event",
        "default registry event_path");
    failures += expect_string(
        upgraded_defaults.registry().nesys().log_path(),
        "D:\\system\\CmdFile\\log",
        "default registry log_path");
    failures += expect_registry_valid(
        upgraded_defaults.registry(),
        true,
        "default registry config");

    failures += expect_bool(
        generated_toml.find("[registry]") != std::string::npos,
        true,
        "generated TOML registry table");
    failures += expect_bool(
        generated_toml.find("[registry.game]") != std::string::npos,
        true,
        "generated TOML registry.game table");
    failures += expect_bool(
        generated_toml.find("[registry.nesys]") != std::string::npos,
        true,
        "generated TOML registry.nesys table");
    failures += expect_bool(
        generated_toml.find("enabled = false") != std::string::npos,
        true,
        "generated TOML registry disabled default");

    const auto valid_nesys_config =
        std::string(kRequiredConfigPrefix) + kDefaultExperimentalConfig;
    failures += expect_parse_failure(
        replace_once(
            valid_nesys_config,
            "[nesys]\nserver_ip = '127.0.0.1'\n\n",
            ""),
        "missing NESYS table");
    failures += expect_parse_failure(
        replace_once(
            valid_nesys_config,
            "server_ip = '127.0.0.1'\n",
            ""),
        "missing NESYS server_ip");

    const auto valid_registry_config =
        std::string(kRequiredConfigPrefix) + kDefaultExperimentalConfig;

    failures += expect_parse_failure(
        replace_once(
            valid_registry_config,
            kDefaultRegistryConfig,
            ""),
        "missing registry table tree");
    failures += expect_parse_failure(
        replace_once(
            valid_registry_config,
            "[registry.game]\ncountry = 'GrooveCoasterJpn'\n\n",
            ""),
        "missing registry.game table");
    failures += expect_parse_failure(
        replace_once(
            valid_registry_config,
            "[registry.nesys]\ngame_kind = 303801\nevent_next_time = 900\n"
            "condition_time = 300\nlog_level = 3\n"
            "news_path = 'D:\\system\\DUA\\news'\n"
            "event_path = 'D:\\system\\DUA\\event'\n"
            "log_path = 'D:\\system\\CmdFile\\log'\n",
            ""),
        "missing registry.nesys table");

    constexpr std::array<std::string_view, 9> required_registry_fields{
        "enabled = false\n",
        "country = 'GrooveCoasterJpn'\n",
        "game_kind = 303801\n",
        "event_next_time = 900\n",
        "condition_time = 300\n",
        "log_level = 3\n",
        "news_path = 'D:\\system\\DUA\\news'\n",
        "event_path = 'D:\\system\\DUA\\event'\n",
        "log_path = 'D:\\system\\CmdFile\\log'\n",
    };
    for (const auto field : required_registry_fields) {
        failures += expect_parse_failure(
            replace_once(valid_registry_config, field, ""),
            std::string("missing registry field ").append(field).c_str());
    }

    failures += expect_parse_failure(
        replace_once(
            valid_registry_config,
            "country = 'GrooveCoasterJpn'",
            "country = 'UnknownCountry'"),
        "unknown registry country");
    failures += expect_parse_failure(
        replace_once(
            valid_registry_config,
            "country = 'GrooveCoasterJpn'",
            "country = 1"),
        "numeric registry country");

    struct CountryCase {
        std::string_view name;
        GameCountry country;
        std::uint32_t dword;
    };
    constexpr std::array<CountryCase, 3> country_cases{{
        {"GrooveCoasterJpn", GameCountry::GrooveCoasterJpn, 0},
        {"Rhythmvaders", GameCountry::Rhythmvaders, 1},
        {"GrooveCoasterEng", GameCountry::GrooveCoasterEng, 2},
    }};
    for (const auto& test : country_cases) {
        const auto country_text = replace_once(
            valid_registry_config,
            "country = 'GrooveCoasterJpn'",
            std::string("country = '").append(test.name).append("'"));
        const auto parsed = parse_config(country_text);
        failures += expect_country(
            parsed.registry().game().country(),
            test.country,
            std::string(test.name).append(" parsed country").c_str());
        failures += expect_u32(
            gc::registry_config::GameCountryRegistryDword(test.country),
            test.dword,
            std::string(test.name).append(" DWORD").c_str());
        const auto round_trip = parse_config(rfl::toml::write(parsed));
        failures += expect_country(
            round_trip.registry().game().country(),
            test.country,
            std::string(test.name).append(" round-trip").c_str());
    }

    auto zero_timers = upgraded_defaults.registry();
    zero_timers.nesys().event_next_time = 0;
    zero_timers.nesys().condition_time = 0;
    failures += expect_registry_valid(zero_timers, true, "zero timing values");

    auto negative_game_kind = upgraded_defaults.registry();
    negative_game_kind.nesys().game_kind = -1;
    failures += expect_registry_valid(
        negative_game_kind,
        false,
        "negative game_kind");

    auto oversized_condition = upgraded_defaults.registry();
    oversized_condition.nesys().condition_time = 4'294'967'296LL;
    failures += expect_registry_valid(
        oversized_condition,
        false,
        "condition_time above DWORD range");

    auto invalid_log_level = upgraded_defaults.registry();
    invalid_log_level.nesys().log_level = 4;
    failures += expect_registry_valid(
        invalid_log_level,
        false,
        "log_level above 3");

    auto empty_news_path = upgraded_defaults.registry();
    empty_news_path.nesys().news_path = "";
    failures += expect_registry_valid(
        empty_news_path,
        false,
        "empty news_path");

    auto maximum_log_path = upgraded_defaults.registry();
    maximum_log_path.nesys().log_path = std::string(259, 'x');
    failures += expect_registry_valid(
        maximum_log_path,
        true,
        "259-byte log_path");

    auto oversized_log_path = upgraded_defaults.registry();
    oversized_log_path.nesys().log_path = std::string(260, 'x');
    failures += expect_registry_valid(
        oversized_log_path,
        false,
        "260-byte log_path");

    constexpr std::array<std::string_view, 5> valid_ipv4{
        "127.0.0.1",
        "10.23.45.67",
        "192.168.100.200",
        "203.0.113.9",
        "255.255.255.255",
    };
    for (const auto value : valid_ipv4) {
        failures += expect_ipv4_valid(value, true, std::string(value).c_str());
    }

    constexpr std::array<std::string_view, 13> invalid_ipv4{
        "",
        "localhost",
        "::1",
        "http://127.0.0.1",
        "127.0.0.1/path",
        "127.0.0.1:80",
        "1.2.3",
        "1.2.3.4.5",
        ".1.2.3",
        "1..2.3",
        "1.2.3.",
        "256.1.2.3",
        "1.2.-3.4",
    };
    for (const auto value : invalid_ipv4) {
        failures += expect_ipv4_valid(value, false, std::string(value).c_str());
    }

    auto custom_server_text = replace_once(
        valid_nesys_config,
        "server_ip = '127.0.0.1'",
        "server_ip = '10.23.45.67'");
    const auto custom_server = parse_config(custom_server_text);
    const auto custom_server_round_trip =
        parse_config(rfl::toml::write(custom_server));
    failures += expect_string(
        custom_server_round_trip.nesys().server_ip(),
        "10.23.45.67",
        "custom NESYS server round-trip");

    failures += expect_u32(
        upgraded_defaults.experimental().target_fps(),
        60,
        "upgraded default target_fps");
    failures += expect_bool(
        upgraded_defaults.experimental().enable_timer_freeze_patches(),
        false,
        "upgraded default enable_timer_freeze_patches");
    failures += expect_bool(
        upgraded_defaults.experimental().enable_testmode_storage_redirect(),
        false,
        "upgraded default enable_testmode_storage_redirect");
    failures += expect_bool(
        upgraded_defaults.experimental().enable_nesys_service_adapter_patch(),
        true,
        "upgraded default enable_nesys_service_adapter_patch");
    failures += expect_bool(
        upgraded_defaults.experimental().enable_wasapi_exclusive_audio(),
        false,
        "upgraded default enable_wasapi_exclusive_audio");
    failures += expect_bool(
        generated_toml.find("enable_wasapi_exclusive_audio = false") !=
            std::string::npos,
        true,
        "ConfigGUI default WASAPI field serialization");
    failures += expect_u32(
        upgraded_defaults.experimental().wasapi_exclusive_buffer_ms(),
        10,
        "upgraded default wasapi_exclusive_buffer_ms");
    failures += expect_bool(
        generated_toml.find("wasapi_exclusive_buffer_ms = 10") !=
            std::string::npos,
        true,
        "ConfigGUI default WASAPI buffer serialization");
    failures += expect_key(
        upgraded_defaults.keyboard().card_read(),
        PhysicalKey{0x3e, ScanCodePrefix::None},
        "upgraded default card_read");
    failures += expect_style(
        upgraded_defaults.gameplay_input_style(),
        gc::input::GameplayInputStyle::Arcade,
        "upgraded default gameplay_input_style");

    const auto switch_prefix = replace_once(
        replace_once(
            kRequiredConfigPrefix,
            "gameplay_input_style = 'Arcade'",
            "gameplay_input_style = 'Switch'"),
        "card_read = 'sc:003e'",
        "card_read = 'sc:0042'");
    const auto custom = parse_config(switch_prefix + kEnabledExperimentalConfig);
    failures += expect_style(
        custom.gameplay_input_style(),
        gc::input::GameplayInputStyle::Switch,
        "custom gameplay_input_style");
    failures += expect_u32(
        custom.experimental().target_fps(),
        240,
        "custom target_fps");
    failures += expect_bool(
        custom.experimental().enable_timer_freeze_patches(),
        true,
        "custom enable_timer_freeze_patches");
    failures += expect_bool(
        custom.experimental().enable_testmode_storage_redirect(),
        true,
        "custom enable_testmode_storage_redirect");
    failures += expect_bool(
        custom.experimental().enable_nesys_service_adapter_patch(),
        false,
        "custom enable_nesys_service_adapter_patch");
    failures += expect_bool(
        custom.experimental().enable_wasapi_exclusive_audio(),
        true,
        "custom enable_wasapi_exclusive_audio");
    failures += expect_u32(
        custom.experimental().wasapi_exclusive_buffer_ms(),
        20,
        "custom wasapi_exclusive_buffer_ms");
    const auto serialized_wasapi = rfl::toml::write(custom);
    const auto reparsed_wasapi = parse_config(serialized_wasapi);
    failures += expect_bool(
        reparsed_wasapi.experimental().enable_wasapi_exclusive_audio(),
        true,
        "WASAPI field TOML round trip");
    failures += expect_u32(
        reparsed_wasapi.experimental().wasapi_exclusive_buffer_ms(),
        20,
        "WASAPI buffer TOML round trip");

    constexpr std::uint32_t accepted_targets[]{
        60, 61, 120, 144, 165, 240, 360, 500,
    };
    for (const auto target : accepted_targets) {
        auto text = replace_once(
            std::string(kRequiredConfigPrefix) + kDefaultExperimentalConfig,
            "target_fps = 60",
            "target_fps = " + std::to_string(target));
        const auto parsed = parse_config(text);
        failures += expect_u32(
            parsed.experimental().target_fps(), target, "accepted target_fps");
        const auto round_trip = parse_config(rfl::toml::write(parsed));
        failures += expect_u32(
            round_trip.experimental().target_fps(), target, "target_fps round trip");
    }

    const auto native_config =
        std::string(kRequiredConfigPrefix) + kDefaultExperimentalConfig;
    for (const std::string_view obsolete : {
             "gamepad_index = 0\n",
             "axis_threshold = 16384\n",
             "[gamepad]\n"}) {
        failures += expect_parse_failure_contains(
            std::string(obsolete),
            "obsolete SDL input schema",
            "isolated obsolete SDL input field");
        failures += expect_parse_failure_contains(
            std::string(obsolete) + native_config,
            "obsolete SDL input schema",
            "mixed obsolete SDL input field");
    }
    failures += expect_parse_failure(
        "[experimental]\ntarget_fps = [",
        "malformed TOML syntax");
    failures += expect_parse_failure(
        replace_once(native_config, "target_fps = 60", "target_fps = 59"),
        "target_fps below range");
    failures += expect_parse_failure(
        replace_once(native_config, "target_fps = 60", "target_fps = 501"),
        "target_fps above range");
    failures += expect_parse_failure(
        replace_once(native_config, "target_fps = 60", "target_fps = 120.0"),
        "fractional target_fps");
    failures += expect_parse_failure(
        replace_once(native_config, "target_fps = 60\n", ""),
        "missing target_fps");
    failures += expect_parse_failure(
        replace_once(
            native_config,
            "target_fps = 60",
            "enable_120fps_timer_patches = false"),
        "obsolete boolean only");
    failures += expect_parse_failure(
        replace_once(
            native_config,
            "target_fps = 60",
            "target_fps = 60\nenable_120fps_timer_patches = false"),
        "mixed target and obsolete boolean");

    auto invalid_for_gui = parse_config(native_config);
    invalid_for_gui.experimental().target_fps = 59;
    failures += expect_bool(
        gc::config::ValidateInputConfig(invalid_for_gui).has_value(),
        false,
        "GUI persistence rejects out-of-range target_fps");

    failures += expect_bool(
        gc::config::IsGameplayValidatedTargetFps(60) &&
            gc::config::IsGameplayValidatedTargetFps(120) &&
            gc::config::IsGameplayValidatedTargetFps(144) &&
            gc::config::IsGameplayValidatedTargetFps(165) &&
            gc::config::IsGameplayValidatedTargetFps(240) &&
            gc::config::IsGameplayValidatedTargetFps(360) &&
            !gc::config::IsGameplayValidatedTargetFps(200),
        true,
        "gameplay-validated target set");

    failures += expect_bool(
        std::string_view(kWasapiExclusiveBufferTooltip).find(
            "Value must be greater than zero") != std::string_view::npos,
        true,
        "WASAPI buffer tooltip rejects zero");
    failures += expect_bool(
        std::string_view(kWasapiExclusiveBufferTooltip).find(
            "Values below the endpoint minimum fail") !=
            std::string_view::npos,
        true,
        "WASAPI buffer tooltip rejects below-minimum values");
    const auto invalid_zero_buffer = parse_config(replace_once(
        std::string(kRequiredConfigPrefix) + kDefaultExperimentalConfig,
        "wasapi_exclusive_buffer_ms = 10",
        "wasapi_exclusive_buffer_ms = 0"));
    failures += expect_u32(
        invalid_zero_buffer.experimental().wasapi_exclusive_buffer_ms(),
        0,
        "zero WASAPI buffer remains representable for endpoint validation");
    failures += expect_key(
        custom.keyboard().card_read(),
        PhysicalKey{0x42, ScanCodePrefix::None},
        "custom card_read");

    failures += expect_parse_failure(kRequiredConfigPrefix, "missing unrelated tables and experimental table");
    failures += expect_parse_failure(
        replace_once(
            kRequiredConfigPrefix,
            "card_read = 'sc:003e'\n",
            "") + kDefaultExperimentalTable,
        "missing card_read");
    failures += expect_parse_failure(
        std::string(kRequiredConfigPrefix) + kDefaultCardReadConfig,
        "missing experimental table");
    failures += expect_parse_failure(
        std::string(kRequiredConfigPrefix) + kDefaultCardReadConfig + R"toml(
[experimental]
enable_timer_freeze_patches = false
enable_testmode_storage_redirect = false
enable_nesys_service_adapter_patch = true
enable_wasapi_exclusive_audio = false
wasapi_exclusive_buffer_ms = 10
)toml",
        "missing target_fps");
    failures += expect_parse_failure(
        std::string(kRequiredConfigPrefix) + kDefaultCardReadConfig + R"toml(
[experimental]
target_fps = 60
enable_testmode_storage_redirect = false
enable_nesys_service_adapter_patch = true
enable_wasapi_exclusive_audio = false
wasapi_exclusive_buffer_ms = 10
)toml",
        "missing enable_timer_freeze_patches");
    failures += expect_parse_failure(
        std::string(kRequiredConfigPrefix) + kDefaultCardReadConfig + R"toml(
[experimental]
target_fps = 60
enable_timer_freeze_patches = false
enable_nesys_service_adapter_patch = true
enable_wasapi_exclusive_audio = false
wasapi_exclusive_buffer_ms = 10
)toml",
        "missing enable_testmode_storage_redirect");
    failures += expect_parse_failure(
        std::string(kRequiredConfigPrefix) + kDefaultCardReadConfig + R"toml(
[experimental]
target_fps = 60
enable_testmode_storage_redirect = false
enable_timer_freeze_patches = false
enable_wasapi_exclusive_audio = false
wasapi_exclusive_buffer_ms = 10
)toml",
        "missing enable_nesys_service_adapter_patch");
    failures += expect_parse_failure(
        std::string(kRequiredConfigPrefix) + kDefaultCardReadConfig + R"toml(
[experimental]
target_fps = 60
enable_testmode_storage_redirect = false
enable_timer_freeze_patches = false
enable_nesys_service_adapter_patch = true
wasapi_exclusive_buffer_ms = 10
)toml",
        "missing enable_wasapi_exclusive_audio");
    failures += expect_parse_failure(
        std::string(kRequiredConfigPrefix) + kDefaultCardReadConfig + R"toml(
[experimental]
target_fps = 60
enable_testmode_storage_redirect = false
enable_timer_freeze_patches = false
enable_nesys_service_adapter_patch = true
enable_wasapi_exclusive_audio = false
)toml",
        "missing wasapi_exclusive_buffer_ms");

    const auto valid_arcade_config =
        std::string(kRequiredConfigPrefix) + kDefaultExperimentalConfig;
    failures += expect_parse_failure(
        replace_once(
            valid_arcade_config,
            "gameplay_input_style = 'Arcade'\n",
            ""),
        "missing gameplay_input_style");
    failures += expect_parse_failure(
        replace_once(
            valid_arcade_config,
            "gameplay_input_style = 'Arcade'",
            "gameplay_input_style = 'Touch'"),
        "unsupported gameplay_input_style");

    const auto serialized_switch = rfl::toml::write(custom);
    const auto reparsed_switch = parse_config(serialized_switch);
    failures += expect_style(
        reparsed_switch.gameplay_input_style(),
        gc::input::GameplayInputStyle::Switch,
        "serialized gameplay_input_style");

    auto exact_round_trip = upgraded_defaults;
    exact_round_trip.axis_press_threshold_percent = 65;
    exact_round_trip.axis_release_threshold_percent = 35;
    exact_round_trip.keyboard().right_booster_up =
        PhysicalKey{0x1d, ScanCodePrefix::E1};
    exact_round_trip.controller().backend =
        gc::input::ControllerBackend::RawHid;
    exact_round_trip.controller().device_id =
        R"(\\?\HID#VID_1234&PID_5678#7&exact-path)";
    exact_round_trip.controller().bindings = {
        gc::input::DigitalControlBinding{
            .action = gc::input::LogicalAction::LeftBoosterButton,
            .type = gc::input::DigitalControlType::RawHidButton,
            .usage_page = 9,
            .usage = 1,
            .link_collection = 0,
            .report_id = 1,
        },
        gc::input::DigitalControlBinding{
            .action = gc::input::LogicalAction::LeftBoosterButton,
            .type = gc::input::DigitalControlType::RawHidButton,
            .usage_page = 9,
            .usage = 2,
            .link_collection = 0,
            .report_id = 1,
        },
    };
    const auto exact_serialized = rfl::toml::write(exact_round_trip);
    const auto exact_reparsed = parse_config(exact_serialized);
    failures += expect_u32(
        exact_reparsed.axis_press_threshold_percent(),
        65,
        "axis press threshold round trip");
    failures += expect_u32(
        exact_reparsed.axis_release_threshold_percent(),
        35,
        "axis release threshold round trip");
    failures += expect_key(
        exact_reparsed.keyboard().right_booster_up(),
        PhysicalKey{0x1d, ScanCodePrefix::E1},
        "E1 scan code round trip");
    failures += expect_string(
        exact_reparsed.controller().device_id(),
        exact_round_trip.controller().device_id(),
        "exact Raw HID path round trip");
    failures += expect_bool(
        exact_reparsed.controller().bindings() ==
            exact_round_trip.controller().bindings(),
        true,
        "multiple bindings for one action round trip");

    failures += expect_vk(
        gc::input::PhysicalKeyToVirtualKey(
            PhysicalKey{0x3e, ScanCodePrefix::None}),
        VK_F4,
        "F4 scan code");
    failures += expect_vk(
        gc::input::PhysicalKeyToVirtualKey(
            PhysicalKey{0x14, ScanCodePrefix::None}),
        'T',
        "T scan code");
    failures += expect_vk(
        gc::input::PhysicalKeyToVirtualKey(PhysicalKey{}),
        0,
        "invalid physical key");
    failures += expect_vk(
        gc::input::PhysicalKeyToVirtualKey(custom.keyboard().card_read()),
        VK_F8,
        "custom card_read");

    const auto punctuation = parse_config(
        replace_once(
            kRequiredConfigPrefix,
            "card_read = 'sc:003e'",
            "card_read = 'sc:0027'") + R"toml(
[nesys]
server_ip = '127.0.0.1'

[registry]
enabled = false

[registry.game]
country = 'GrooveCoasterJpn'

[registry.nesys]
game_kind = 303801
event_next_time = 900
condition_time = 300
log_level = 3
news_path = 'D:\system\DUA\news'
event_path = 'D:\system\DUA\event'
log_path = 'D:\system\CmdFile\log'

[experimental]
target_fps = 60
enable_testmode_storage_redirect = false
enable_timer_freeze_patches = false
enable_nesys_service_adapter_patch = true
enable_wasapi_exclusive_audio = false
wasapi_exclusive_buffer_ms = 10
)toml");
    failures += expect_key(
        punctuation.keyboard().card_read(),
        PhysicalKey{0x27, ScanCodePrefix::None},
        "punctuation-position card_read");
    failures += expect_bool(
        !gc::input::PhysicalKeyLabel(
            punctuation.keyboard().card_read()).empty(),
        true,
        "punctuation logical label is presentation-only");

    return failures == 0 ? 0 : 1;
}
