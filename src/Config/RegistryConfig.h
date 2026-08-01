#pragma once

#include <rfl.hpp>

#include <cstdint>
#include <expected>
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
};

struct RegistryConfig {
    rfl::Rename<"enabled", bool> enabled = false;
    rfl::Rename<"system_path", std::string> system_path = "D:\\system";
    rfl::Rename<"game", RegistryGameConfig> game;
    rfl::Rename<"nesys", RegistryNesysConfig> nesys;
};

namespace gc::registry_config {

struct DerivedNesysPaths {
    std::string news;
    std::string event;
    std::string log;
};

struct RegistryValidationResult {
    bool game_kind{false};
    bool event_next_time{false};
    bool condition_time{false};
    bool log_level{false};
    bool system_path{false};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return game_kind && event_next_time && condition_time &&
            log_level && system_path;
    }
};

constexpr std::uint32_t GameCountryRegistryDword(
    GameCountry country) noexcept {
    return static_cast<std::uint32_t>(country);
}

bool IsRegistryDword(std::int64_t value) noexcept;
bool IsRegistryLogLevel(std::int64_t value) noexcept;
[[nodiscard]] std::expected<DerivedNesysPaths, std::string>
DeriveNesysPaths(std::string_view system_path) noexcept;
RegistryValidationResult ValidateRegistryConfig(
    const RegistryConfig& config) noexcept;
const char* FirstRegistryValidationError(
    const RegistryValidationResult& validation) noexcept;

} // namespace gc::registry_config
