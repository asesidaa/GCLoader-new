#include "Config/config.h"

#include "Nesys/Network/NesysNetworkConfig.h"

#include <filesystem>
#include <fstream>
#include <iterator>

#include "rfl/json.hpp"
#include "rfl/toml.hpp"
#include <toml++/toml.hpp>

namespace gc::config {

namespace {

constexpr std::string_view kObsoleteFramerateBoolean =
    "enable_120fps_" "timer_patches";

std::expected<toml::table, std::string> ParseTomlSyntax(
    std::string_view text) {
#if TOML_EXCEPTIONS
    try {
        return toml::parse(text);
    } catch (const toml::parse_error& error) {
        return std::unexpected(
            "Failed to parse config file: " +
            std::string{error.description()});
    }
#else
    auto result = toml::parse(text);
    if (!result) {
        return std::unexpected(
            "Failed to parse config file: " +
            std::string{result.error().description()});
    }
    return std::move(result).table();
#endif
}

} // namespace

std::expected<void, std::string> ValidateInputConfig(
    const InputConfig& value) {
    const auto target = static_cast<std::uint32_t>(
        value.experimental().target_fps());
    if (!IsTargetFpsInRange(target)) {
        return std::unexpected(
            "Invalid [experimental].target_fps; expected an integer from 60 through 500");
    }

    try {
        ValidateInputPollHertz(value.input_poll_hz());
    } catch (const std::exception& error) {
        return std::unexpected(error.what());
    }

    if (!gc::nesys_service::IsDottedDecimalIpv4(
            value.nesys().server_ip())) {
        return std::unexpected(
            "Invalid [nesys].server_ip; expected dotted-decimal IPv4");
    }

    const auto registry_validation =
        gc::registry_config::ValidateRegistryConfig(value.registry());
    if (!registry_validation.valid()) {
        return std::unexpected(
            gc::registry_config::FirstRegistryValidationError(
                registry_validation));
    }
    return {};
}

std::expected<InputConfig, std::string> ParseAndValidateInputConfig(
    std::string_view text) {
    const auto syntax_result = ParseTomlSyntax(text);
    if (!syntax_result) {
        return std::unexpected(syntax_result.error());
    }

    const auto& syntax = syntax_result.value();
    if (const auto* experimental = syntax["experimental"].as_table();
        experimental != nullptr &&
        experimental->contains(kObsoleteFramerateBoolean)) {
        return std::unexpected(
            "Obsolete [experimental].enable_120fps_"
            "timer_patches is not supported; replace it with target_fps = 60 through 500");
    }

    auto parsed = rfl::toml::read<InputConfig>(std::string{text});
    if (!parsed) {
        return std::unexpected(
            "Failed to parse config file: " + parsed.error().what());
    }
    if (auto validation = ValidateInputConfig(parsed.value());
        !validation) {
        return std::unexpected(validation.error());
    }
    return std::move(parsed.value());
}

} // namespace gc::config

ConfigManager::ConfigManager()
{
    const auto configPath = std::filesystem::current_path() / "config.toml";
    if (!std::filesystem::exists(configPath))
    {
        PLOG_ERROR << "Config file not found: " << configPath.c_str() << std::endl;
        throw std::runtime_error("Config file not found");
    }

    std::ifstream configFile(configPath);
    if (!configFile.is_open())
    {
        PLOG_ERROR << "Failed to open config file: " << configPath.c_str() << std::endl;
        throw std::runtime_error("Failed to open config file");
    }

    const std::string text{
        std::istreambuf_iterator<char>{configFile},
        std::istreambuf_iterator<char>{}};
    auto result = gc::config::ParseAndValidateInputConfig(text);
    if (!result) {
        PLOG_ERROR << result.error() << std::endl;
        throw std::runtime_error(result.error());
    }

    config = std::move(result.value());
    PLOG_DEBUG << "Config file parsed successfully" << std::endl;
    PLOG_DEBUG << "Loaded: " << rfl::json::write(config) << std::endl;
}
