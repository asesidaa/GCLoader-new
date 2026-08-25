#include "Config/config.h"

#include "Config/ConfigCompiler.h"
#include "Config/ConfigDocument.h"
#include "Nesys/Network/NesysNetworkConfig.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <utility>

#include "rfl/json.hpp"

namespace gc::config
{
    std::expected<void, std::string> ValidateInputConfig(
        const InputConfig& value)
    {
        if (!IsSupportedLoaderLogLevel(value.logging().level()))
        {
            return std::unexpected(
                "Invalid [logging].level; expected Info, Debug, or Verbose");
        }

        const auto target = static_cast<std::uint32_t>(
            value.experimental().target_fps());
        if (!IsTargetFpsInRange(target))
        {
            return std::unexpected(
                "Invalid [experimental].target_fps; expected an integer from 60 through 500");
        }

        if (const auto audio_validation = ValidateAudioBackendSettings(
                value.experimental().audio_backend(),
                value.experimental().asio_driver_name(),
                static_cast<std::uint32_t>(
                    value.experimental().asio_buffer_frames()),
                static_cast<std::uint32_t>(
                    value.experimental().asio_output_base_channel()));
            !audio_validation)
        {
            return std::unexpected(audio_validation.error());
        }

        if (const auto input_validation = ValidateNativeInputFields(
                value.input_schema_version(),
                value.input_poll_hz(),
                value.axis_press_threshold_percent(),
                value.axis_release_threshold_percent(),
                value.keyboard(),
                value.controller());
            !input_validation)
        {
            return std::unexpected(input_validation.error());
        }

        const auto audio_backend = value.experimental().audio_backend();
        if (value.experimental().enable_absolute_time_judgement() &&
            audio_backend != AudioBackend::wasapi_exclusive &&
            audio_backend != AudioBackend::asio)
        {
            return std::unexpected(
                "Absolute-time judgement requires "
                "[experimental].audio_backend = 'wasapi_exclusive' or 'asio'");
        }
        if (value.experimental().enable_absolute_time_judgement() &&
            value.input_poll_hz() != 1000)
        {
            return std::unexpected(
                "Absolute-time judgement requires input_poll_hz = 1000");
        }

        if (!gc::nesys_service::IsDottedDecimalIpv4(
            value.nesys().server_ip()))
        {
            return std::unexpected(
                "Invalid [nesys].server_ip; expected dotted-decimal IPv4");
        }

        const auto registry_validation =
            gc::registry_config::ValidateRegistryConfig(value.registry());
        if (!registry_validation.valid())
        {
            if (!registry_validation.system_path)
            {
                const auto derived = gc::registry_config::DeriveNesysPaths(
                    value.registry().system_path());
                if (!derived)
                {
                    return std::unexpected(derived.error());
                }
            }
            return std::unexpected(
                gc::registry_config::FirstRegistryValidationError(
                    registry_validation));
        }
        return {};
    }

    std::expected<ParsedInputConfigDocument, std::string>
    ParseAndValidateInputConfigDocument(std::string_view text) noexcept
    {
        try
        {
            auto parsed = ParseConfigDocument(text);
            if (!parsed)
            {
                return std::unexpected(std::move(parsed.error().message));
            }
            if (auto validation = ValidateInputConfig(parsed->document);
                !validation)
            {
                return std::unexpected(std::move(validation.error()));
            }
            return ParsedInputConfigDocument{
                .config = std::move(parsed->document),
                .migrations = parsed->migrations,
            };
        }
        catch (const std::exception& error)
        {
            return std::unexpected(
                "Config semantic validation failed: " +
                std::string{error.what()});
        }
        catch (...)
        {
            return std::unexpected(
                "Config semantic validation failed unexpectedly");
        }
    }

    std::expected<InputConfig, std::string> ParseAndValidateInputConfig(
        std::string_view text) noexcept
    {
        auto document = ParseAndValidateInputConfigDocument(text);
        if (!document)
        {
            return std::unexpected(document.error());
        }
        return std::move(document->config);
    }
} // namespace gc::config

ConfigManager::ConfigManager()
{
    config_path_ = std::filesystem::current_path() / "config.toml";
    if (!std::filesystem::exists(config_path_))
    {
        PLOG_ERROR << "Config file not found: " << config_path_.c_str() << std::endl;
        throw std::runtime_error("Config file not found");
    }

    std::ifstream configFile(config_path_);
    if (!configFile.is_open())
    {
        PLOG_ERROR << "Failed to open config file: " << config_path_.c_str() << std::endl;
        throw std::runtime_error("Failed to open config file");
    }

    const std::string text{
        std::istreambuf_iterator<char>{configFile},
        std::istreambuf_iterator<char>{}
    };
    auto result = gc::config::ParseConfigDocument(text);
    if (!result)
    {
        PLOG_ERROR << result.error().message << std::endl;
        throw std::runtime_error(result.error().message);
    }
    auto compiled = gc::config::ConfigCompiler::Compile(result->document);
    if (!compiled)
    {
        const auto message =
            gc::config::FormatConfigErrors(compiled.error());
        PLOG_ERROR << message << std::endl;
        throw std::runtime_error(message);
    }

    document_migrated_ = result->migrations.any();
    config = std::move(result->document);
    validated_ = std::make_unique<gc::config::ValidatedConfig>(
        std::move(*compiled));
    PLOG_DEBUG << "Config file parsed successfully" << std::endl;
    PLOG_DEBUG
        << "Config document migrated="
        << document_migrated_;
    PLOG_DEBUG << "Loaded: " << rfl::json::write(config) << std::endl;
}

ConfigManager::~ConfigManager() = default;

const gc::config::ValidatedConfig&
ConfigManager::validated() const noexcept
{
    return *validated_;
}

std::expected<gc::system_path::RuntimeRoot, std::string>
ConfigManager::PrepareGameSystemPath(
    bool native_testmode_storage_available) noexcept
{
    try
    {
        auto prepared =
            gc::config::PrepareAndPersistGameSystemPathConfiguration(
                config,
                document_migrated_,
                config_path_,
                native_testmode_storage_available);
        if (!prepared)
        {
            return std::unexpected(prepared.error());
        }

        auto compiled =
            gc::config::ConfigCompiler::Compile(prepared->config);
        if (!compiled)
        {
            return std::unexpected(
                "Prepared configuration failed semantic compilation: " +
                gc::config::FormatConfigErrors(compiled.error()));
        }
        config = std::move(prepared->config);
        validated_ = std::make_unique<gc::config::ValidatedConfig>(
            std::move(*compiled));
        document_migrated_ = false;
        return std::move(prepared->runtime);
    }
    catch (const std::exception& error)
    {
        return std::unexpected(
            "ConfigManager system path preparation failed: " +
            std::string{error.what()});
    }
    catch (...)
    {
        return std::unexpected(
            "ConfigManager system path preparation failed unexpectedly");
    }
}
