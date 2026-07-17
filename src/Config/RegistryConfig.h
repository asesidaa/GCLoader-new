#pragma once

#include <rfl.hpp>

#include <cstdint>
#include <string>
#include <string_view>

enum class GameCountry : std::uint32_t {
    GrooveCoasterJpn = 0,
    Rhythmvaders = 1,
    GrooveCoasterEng = 2,
};

struct RegistryGameConfig {
    rfl::Rename<"country", GameCountry> country =
        GameCountry::GrooveCoasterJpn;
};

struct RegistryNesysConfig {
    rfl::Rename<"game_kind", std::int64_t> game_kind = 303801;
    rfl::Rename<"event_next_time", std::int64_t> event_next_time = 900;
    rfl::Rename<"condition_time", std::int64_t> condition_time = 300;
    rfl::Rename<"log_level", std::int64_t> log_level = 3;
    rfl::Rename<"news_path", std::string> news_path =
        "D:\\system\\DUA\\news";
    rfl::Rename<"event_path", std::string> event_path =
        "D:\\system\\DUA\\event";
    rfl::Rename<"log_path", std::string> log_path =
        "D:\\system\\CmdFile\\log";
};

struct RegistryConfig {
    rfl::Rename<"enabled", bool> enabled = false;
    rfl::Rename<"game", RegistryGameConfig> game;
    rfl::Rename<"nesys", RegistryNesysConfig> nesys;
};

namespace gc::registry_config {

struct RegistryValidationResult {
    bool game_kind{false};
    bool event_next_time{false};
    bool condition_time{false};
    bool log_level{false};
    bool news_path{false};
    bool event_path{false};
    bool log_path{false};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return game_kind && event_next_time && condition_time &&
            log_level && news_path && event_path && log_path;
    }
};

constexpr std::uint32_t GameCountryRegistryDword(
    GameCountry country) noexcept {
    return static_cast<std::uint32_t>(country);
}

bool IsRegistryDword(std::int64_t value) noexcept;
bool IsRegistryLogLevel(std::int64_t value) noexcept;
bool IsRegistryPath(std::string_view value) noexcept;
RegistryValidationResult ValidateRegistryConfig(
    const RegistryConfig& config) noexcept;
const char* FirstRegistryValidationError(
    const RegistryValidationResult& validation) noexcept;

} // namespace gc::registry_config
