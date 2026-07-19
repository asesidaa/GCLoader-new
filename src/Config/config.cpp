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

constexpr std::string_view kObsoleteInputSchemaError =
    "obsolete SDL input schema is not supported; remove gamepad_index, "
    "axis_threshold, and [gamepad]";

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
    if (!IsSupportedLoaderLogLevel(value.logging().level())) {
        return std::unexpected(
            "Invalid [logging].level; expected Info, Debug, or Verbose");
    }

    const auto target = static_cast<std::uint32_t>(
        value.experimental().target_fps());
    if (!IsTargetFpsInRange(target)) {
        return std::unexpected(
            "Invalid [experimental].target_fps; expected an integer from 60 through 500");
    }

    if (const auto input_validation = ValidateNativeInputFields(
            value.input_schema_version(),
            value.input_poll_hz(),
            value.axis_press_threshold_percent(),
            value.axis_release_threshold_percent(),
            value.keyboard(),
            value.controller());
        !input_validation) {
        return std::unexpected(input_validation.error());
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
    if (syntax.contains("gamepad_index") ||
        syntax.contains("axis_threshold") ||
        syntax.contains("gamepad")) {
        return std::unexpected(std::string{kObsoleteInputSchemaError});
    }

    const auto* schema_version =
        syntax["input_schema_version"].as_integer();
    if (schema_version == nullptr) {
        return std::unexpected(
            "Missing or non-integer input_schema_version; expected 2");
    }
    if (schema_version->get() != kInputSchemaVersion) {
        return std::unexpected(
            "Unsupported input_schema_version; expected 2");
    }

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
