#include "Config/config.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>

#include "rfl/toml.hpp"

#ifndef GC_TEST_CONFIG_PATH
#error GC_TEST_CONFIG_PATH must name the distributed config.toml
#endif

namespace {

int Expect(bool condition, std::string_view name) {
    if (condition) {
        return 0;
    }
    std::cerr << "Expectation failed: " << name << '\n';
    return 1;
}

std::string ReadDistributedConfig() {
    std::ifstream input{GC_TEST_CONFIG_PATH, std::ios::binary};
    if (!input) {
        std::cerr << "Could not open distributed config: "
                  << GC_TEST_CONFIG_PATH << '\n';
        std::exit(2);
    }
    return {
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{},
    };
}

std::size_t FindAssignment(
    const std::string& text,
    std::string_view key) {
    const std::string marker = std::string{key} + " = ";
    std::size_t position = text.find(marker);
    while (position != std::string::npos &&
           position != 0 &&
           text[position - 1] != '\n') {
        position = text.find(marker, position + marker.size());
    }
    if (position == std::string::npos) {
        std::cerr << "Config fixture does not contain assignment for "
                  << key << '\n';
        std::exit(2);
    }
    return position;
}

std::string RemoveAssignment(
    std::string text,
    std::string_view key) {
    const auto position = FindAssignment(text, key);
    auto end = text.find('\n', position);
    if (end == std::string::npos) {
        end = text.size();
    } else {
        ++end;
    }
    text.erase(position, end - position);
    return text;
}

std::string RemoveArrayAssignment(
    std::string text,
    std::string_view key) {
    const auto position = FindAssignment(text, key);
    auto line_begin = text.find('\n', position);
    while (line_begin != std::string::npos) {
        ++line_begin;
        auto line_end = text.find('\n', line_begin);
        if (line_end == std::string::npos) {
            line_end = text.size();
        }
        auto content_end = line_end;
        if (content_end > line_begin &&
            text[content_end - 1] == '\r') {
            --content_end;
        }
        if (std::string_view{
                text.data() + line_begin,
                content_end - line_begin} == "]") {
            const auto erase_end =
                line_end == text.size()
                    ? line_end
                    : line_end + 1;
            text.erase(position, erase_end - position);
            return text;
        }
        line_begin =
            line_end == text.size()
                ? std::string::npos
                : line_end;
    }

    std::cerr << "Config fixture array has no closing bracket: "
              << key << '\n';
    std::exit(2);
}

std::string ReplaceAssignment(
    std::string text,
    std::string_view key,
    std::string_view value) {
    const auto position = FindAssignment(text, key);
    const auto value_begin =
        position + key.size() + std::string_view{" = "}.size();
    auto value_end = text.find('\n', value_begin);
    if (value_end == std::string::npos) {
        value_end = text.size();
    }
    text.replace(value_begin, value_end - value_begin, value);
    return text;
}

std::string InsertAfterLine(
    std::string text,
    std::string_view line,
    std::string_view insertion) {
    const auto position = text.find(line);
    if (position == std::string::npos) {
        std::cerr << "Config fixture does not contain line: "
                  << line << '\n';
        std::exit(2);
    }
    const auto line_end = text.find('\n', position + line.size());
    if (line_end == std::string::npos) {
        std::cerr << "Config fixture line has no terminator: "
                  << line << '\n';
        std::exit(2);
    }
    const std::string_view newline =
        line_end != 0 && text[line_end - 1] == '\r'
            ? "\r\n"
            : "\n";
    text.insert(
        line_end + 1,
        std::string{insertion} + std::string{newline});
    return text;
}

InputConfig ParseOrExit(
    std::string_view text,
    std::string_view name) {
    auto result = gc::config::ParseAndValidateInputConfig(text);
    if (!result) {
        std::cerr << name << " did not parse: "
                  << result.error() << '\n';
        std::exit(2);
    }
    return std::move(result.value());
}

int ExpectParseFailure(
    std::string_view text,
    std::string_view name,
    std::string_view expected_error = {}) {
    const auto result =
        gc::config::ParseAndValidateInputConfig(text);
    if (!result &&
        (expected_error.empty() ||
         result.error().find(expected_error) !=
             std::string::npos)) {
        return 0;
    }

    std::cerr << name << ": expected parse failure";
    if (!expected_error.empty()) {
        std::cerr << " containing '" << expected_error << '\'';
    }
    std::cerr << ", got "
              << (result ? "success" : result.error()) << '\n';
    return 1;
}

} // namespace

int main() {
    using gc::config::LoaderLogLevel;

    int failures = 0;
    const std::string distributed = ReadDistributedConfig();

    const auto canonical =
        ParseOrExit(distributed, "distributed config");
    failures += Expect(
        gc::config::ValidateInputConfig(canonical).has_value(),
        "distributed config passes production validation");

    const auto distributed_round_trip =
        gc::config::ParseAndValidateInputConfig(
            rfl::toml::write(canonical));
    failures += Expect(
        distributed_round_trip.has_value(),
        "distributed config survives reflect-cpp round trip");

    InputConfig generated{};
    failures += Expect(
        gc::config::ValidateInputConfig(generated).has_value(),
        "ConfigGUI defaults pass production validation");
    failures += Expect(
        gc::config::ParseAndValidateInputConfig(
            rfl::toml::write(generated)).has_value(),
        "ConfigGUI defaults serialize as a complete strict config");

    constexpr std::array required_assignments{
        "input_schema_version",
        "input_poll_hz",
        "input_mode",
        "gameplay_input_style",
        "axis_press_threshold_percent",
        "axis_release_threshold_percent",
        "left_booster_up",
        "left_booster_down",
        "left_booster_left",
        "left_booster_right",
        "left_booster_button",
        "right_booster_up",
        "right_booster_down",
        "right_booster_left",
        "right_booster_right",
        "right_booster_button",
        "test",
        "service1",
        "service2",
        "service3",
        "p1_start",
        "p2_start",
        "p2_service",
        "card_read",
        "backend",
        "device_id",
        "server_ip",
        "enabled",
        "country",
        "game_kind",
        "event_next_time",
        "condition_time",
        "log_level",
        "system_path",
        "level",
        "target_fps",
        "enable_testmode_storage_redirect",
        "enable_timer_freeze_patches",
        "enable_nesys_service_adapter_patch",
        "enable_wasapi_exclusive_audio",
        "wasapi_exclusive_buffer_ms",
    };
    for (const std::string_view key : required_assignments) {
        failures += ExpectParseFailure(
            RemoveAssignment(distributed, key),
            std::string{"missing required assignment: "} +
                std::string{key});
    }

    constexpr std::array legacy_registry_paths{
        "news_path = 'D:\\system\\DUA\\news'",
        "event_path = 'D:\\system\\DUA\\event'",
        "log_path = 'D:\\system\\CmdFile\\log'",
    };
    for (const std::string_view assignment : legacy_registry_paths) {
        failures += ExpectParseFailure(
            InsertAfterLine(
                distributed,
                "[registry.nesys]",
                assignment),
            "system_path plus legacy registry leaf is rejected");
    }
    failures += ExpectParseFailure(
        RemoveArrayAssignment(distributed, "bindings"),
        "missing required controller bindings");

    failures += ExpectParseFailure(
        ReplaceAssignment(
            distributed,
            "input_schema_version",
            "1"),
        "obsolete input schema version",
        "input_schema_version");
    failures += ExpectParseFailure(
        "gamepad_index = 0\n" + distributed,
        "obsolete SDL input field",
        "obsolete SDL input schema");
    failures += ExpectParseFailure(
        InsertAfterLine(
            distributed,
            "[experimental]",
            "enable_120fps_timer_patches = false"),
        "obsolete framerate boolean",
        "enable_120fps_timer_patches");
    failures += ExpectParseFailure(
        "[experimental]\ntarget_fps = [",
        "malformed TOML syntax",
        "Failed to parse config file");

    struct LogLevelCase {
        std::string_view text;
        LoaderLogLevel value;
    };
    constexpr std::array log_levels{
        LogLevelCase{"Info", LoaderLogLevel::Info},
        LogLevelCase{"Debug", LoaderLogLevel::Debug},
        LogLevelCase{"Verbose", LoaderLogLevel::Verbose},
    };
    for (const auto& test : log_levels) {
        const auto parsed = ParseOrExit(
            ReplaceAssignment(
                distributed,
                "level",
                std::string{"'"} +
                    std::string{test.text} + "'"),
            "supported loader log level");
        failures += Expect(
            parsed.logging().level() == test.value,
            "loader log level maps to the expected enum");
        const auto round_trip = ParseOrExit(
            rfl::toml::write(parsed),
            "loader log level round trip");
        failures += Expect(
            round_trip.logging().level() == test.value,
            "loader log level survives round trip");
    }
    failures += ExpectParseFailure(
        ReplaceAssignment(distributed, "level", "'Trace'"),
        "unsupported loader log level");

    for (const std::uint32_t target :
         {60U, 61U, 144U, 240U, 500U}) {
        const auto parsed = ParseOrExit(
            ReplaceAssignment(
                distributed,
                "target_fps",
                std::to_string(target)),
            "supported target FPS");
        failures += Expect(
            static_cast<std::uint32_t>(
                parsed.experimental().target_fps()) == target,
            "target FPS maps to the requested integer");
    }
    failures += ExpectParseFailure(
        ReplaceAssignment(distributed, "target_fps", "59"),
        "target FPS below range",
        "target_fps");
    failures += ExpectParseFailure(
        ReplaceAssignment(distributed, "target_fps", "501"),
        "target FPS above range",
        "target_fps");
    failures += ExpectParseFailure(
        ReplaceAssignment(distributed, "target_fps", "120.0"),
        "fractional target FPS");

    auto custom_text =
        ReplaceAssignment(distributed, "target_fps", "240");
    custom_text = ReplaceAssignment(
        std::move(custom_text),
        "enable_testmode_storage_redirect",
        "true");
    custom_text = ReplaceAssignment(
        std::move(custom_text),
        "enable_timer_freeze_patches",
        "true");
    custom_text = ReplaceAssignment(
        std::move(custom_text),
        "enable_nesys_service_adapter_patch",
        "false");
    custom_text = ReplaceAssignment(
        std::move(custom_text),
        "enable_wasapi_exclusive_audio",
        "true");
    custom_text = ReplaceAssignment(
        std::move(custom_text),
        "wasapi_exclusive_buffer_ms",
        "20");
    const auto custom =
        ParseOrExit(custom_text, "custom experimental config");
    failures += Expect(
        static_cast<std::uint32_t>(
            custom.experimental().target_fps()) == 240 &&
            custom.experimental()
                .enable_testmode_storage_redirect() &&
            custom.experimental()
                .enable_timer_freeze_patches() &&
            !custom.experimental()
                 .enable_nesys_service_adapter_patch() &&
            custom.experimental()
                .enable_wasapi_exclusive_audio() &&
            custom.experimental()
                .wasapi_exclusive_buffer_ms() == 20,
        "experimental settings parse as one coherent config");

    const auto zero_buffer = ParseOrExit(
        ReplaceAssignment(
            distributed,
            "wasapi_exclusive_buffer_ms",
            "0"),
        "zero WASAPI buffer");
    failures += Expect(
        zero_buffer.experimental()
            .wasapi_exclusive_buffer_ms() == 0,
        "endpoint validation owns the zero-buffer rejection");

    const auto switch_style = ParseOrExit(
        ReplaceAssignment(
            distributed,
            "gameplay_input_style",
            "'Switch'"),
        "Switch gameplay style");
    failures += Expect(
        switch_style.gameplay_input_style() ==
            gc::input::GameplayInputStyle::Switch,
        "Switch gameplay style maps to the expected enum");
    failures += ExpectParseFailure(
        ReplaceAssignment(
            distributed,
            "gameplay_input_style",
            "'Touch'"),
        "unsupported gameplay style");

    const auto custom_server = ParseOrExit(
        ReplaceAssignment(
            distributed,
            "server_ip",
            "'10.23.45.67'"),
        "custom NESYS server");
    failures += Expect(
        custom_server.nesys().server_ip() == "10.23.45.67",
        "custom NESYS server survives validation");
    for (const std::string_view invalid :
         {"'localhost'", "'256.1.2.3'", "'1.2.3'"}) {
        failures += ExpectParseFailure(
            ReplaceAssignment(
                distributed,
                "server_ip",
                invalid),
            "invalid NESYS server",
            "dotted-decimal IPv4");
    }

    struct CountryCase {
        std::string_view text;
        GameCountry value;
        std::uint32_t dword;
    };
    constexpr std::array countries{
        CountryCase{
            "GrooveCoasterJpn",
            GameCountry::GrooveCoasterJpn,
            0},
        CountryCase{
            "Rhythmvaders",
            GameCountry::Rhythmvaders,
            1},
        CountryCase{
            "GrooveCoasterEng",
            GameCountry::GrooveCoasterEng,
            2},
    };
    for (const auto& test : countries) {
        const auto parsed = ParseOrExit(
            ReplaceAssignment(
                distributed,
                "country",
                std::string{"'"} +
                    std::string{test.text} + "'"),
            "supported game country");
        failures += Expect(
            parsed.registry().game().country() == test.value &&
                gc::registry_config::GameCountryRegistryDword(
                    test.value) == test.dword,
            "game country maps to the expected registry DWORD");
    }

    const auto relative_paths =
        gc::registry_config::DeriveNesysPaths(".\\system");
    failures += Expect(
        relative_paths.has_value(),
        "relative registry system path is accepted");
    if (relative_paths) {
        failures += Expect(
            relative_paths->news == ".\\system\\DUA\\news" &&
                relative_paths->event == ".\\system\\DUA\\event" &&
                relative_paths->log == ".\\system\\CmdFile\\log",
            "relative registry system path remains explicitly relative");
    }

    const auto absolute_paths =
        gc::registry_config::DeriveNesysPaths("R:\\cabinet");
    failures += Expect(
        absolute_paths.has_value(),
        "absolute registry system path is accepted");
    if (absolute_paths) {
        failures += Expect(
            absolute_paths->news == "R:\\cabinet\\DUA\\news" &&
                absolute_paths->event == "R:\\cabinet\\DUA\\event" &&
                absolute_paths->log == "R:\\cabinet\\CmdFile\\log",
            "absolute registry service paths are derived from one root");
    }

    const auto empty_paths = gc::registry_config::DeriveNesysPaths("");
    failures += Expect(
        !empty_paths,
        "empty registry system path is rejected");

    const auto ansi_incompatible_paths =
        gc::registry_config::DeriveNesysPaths(
            std::string{"C:\\"} + "\xF0\x9F\x98\x80");
    failures += Expect(
        !ansi_incompatible_paths &&
            ansi_incompatible_paths.error().find("ANSI") !=
                std::string::npos &&
            ansi_incompatible_paths.error().find(".\\system") !=
                std::string::npos,
        "ANSI-incompatible registry system path explains the service limit");

    const auto overlong_derived_paths =
        gc::registry_config::DeriveNesysPaths(std::string(250, 'x'));
    failures += Expect(
        !overlong_derived_paths,
        "registry system root is rejected when a derived path is too long");

    auto invalid_registry = canonical;
    invalid_registry.registry().nesys().game_kind = -1;
    failures += Expect(
        !gc::config::ValidateInputConfig(invalid_registry),
        "negative registry DWORD is rejected");
    invalid_registry = canonical;
    invalid_registry.registry().nesys().log_level = 4;
    failures += Expect(
        !gc::config::ValidateInputConfig(invalid_registry),
        "registry log level above three is rejected");
    invalid_registry = canonical;
    invalid_registry.registry().system_path = "";
    failures += Expect(
        !gc::config::ValidateInputConfig(invalid_registry),
        "empty registry system path is rejected");
    invalid_registry = canonical;
    invalid_registry.registry().system_path =
        std::string{"C:\\"} + "\xF0\x9F\x98\x80";
    const auto invalid_system_path =
        gc::config::ValidateInputConfig(invalid_registry);
    failures += Expect(
        !invalid_system_path &&
            invalid_system_path.error().find("ANSI") !=
                std::string::npos &&
            invalid_system_path.error().find(".\\system") !=
                std::string::npos,
        "config validation reports the ANSI service path limitation");

    return failures == 0 ? 0 : 1;
}
