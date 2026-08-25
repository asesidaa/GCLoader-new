#pragma once

#include "Config/ConfigDocument.h"

#include <Windows.h>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

#include "plog/Log.h"

using InputConfig = gc::config::ConfigDocument;

inline constexpr bool IsSupportedInputPollHertz(
    std::uint32_t value) noexcept
{
    return value == 125 || value == 250 || value == 500 || value == 1000;
}

inline void ValidateInputPollHertz(std::uint32_t value)
{
    if (!IsSupportedInputPollHertz(value))
    {
        throw std::runtime_error(
            "Invalid input_poll_hz; expected one of 125, 250, 500, or 1000");
    }
}

inline constexpr char kWasapiExclusiveBufferTooltip[] =
    "Fixed exclusive buffer duration for this game launch.\n"
    "Default is 10 ms. Value must be greater than zero.\n"
    "Values below the endpoint minimum fail initialization.\n"
    "Restart the game after changing it.";

namespace gc::config
{
    using LoaderLogLevel = logging::LoaderLogLevel;

    inline constexpr bool IsSupportedLoaderLogLevel(
        LoaderLogLevel level) noexcept
    {
        return level == LoaderLogLevel::Info ||
            level == LoaderLogLevel::Debug ||
            level == LoaderLogLevel::Verbose;
    }

    struct ParsedInputConfigDocument
    {
        InputConfig config;
        ConfigDocumentMigrations migrations;
    };

    [[nodiscard]] std::expected<ParsedInputConfigDocument, std::string>
    ParseAndValidateInputConfigDocument(std::string_view text) noexcept;

    [[nodiscard]] std::expected<void, std::string> ValidateInputConfig(
        const InputConfig& config);

    [[nodiscard]] std::expected<InputConfig, std::string>
    ParseAndValidateInputConfig(std::string_view text) noexcept;
} // namespace gc::config

class ConfigManager
{
public:
    static ConfigManager& instance()
    {
        try
        {
            static ConfigManager instance;
            return instance;
        }
        catch (std::runtime_error& error)
        {
            PLOG_ERROR << "Failed to parse Default Config: "
                << error.what() << '\n';
            MessageBoxA(
                nullptr, error.what(), "Error", MB_OK | MB_ICONERROR);
            ExitProcess(1);
        }
    }

    [[nodiscard]] std::uint32_t GetInputSchemaVersion() const
    {
        return config.input_schema_version();
    }

    [[nodiscard]] std::uint32_t GetInputPollHertz() const
    {
        return config.input_poll_hz();
    }

    [[nodiscard]] gc::input::InputMode GetInputMode() const
    {
        return config.input_mode();
    }

    [[nodiscard]] gc::input::GameplayInputStyle GetGameplayInputStyle() const
    {
        return config.gameplay_input_style();
    }

    [[nodiscard]] std::uint32_t GetAxisPressThresholdPercent() const
    {
        return config.axis_press_threshold_percent();
    }

    [[nodiscard]] std::uint32_t GetAxisReleaseThresholdPercent() const
    {
        return config.axis_release_threshold_percent();
    }

    [[nodiscard]] const gc::config::NativeKeyboardConfig&
    GetKeyboardConfig() const
    {
        return config.keyboard();
    }

    [[nodiscard]] const gc::config::ControllerConfig&
    GetControllerConfig() const
    {
        return config.controller();
    }

    [[nodiscard]] gc::input::PhysicalKey GetCardReadKey() const
    {
        return config.keyboard().card_read();
    }

    [[nodiscard]] std::uint32_t GetTargetFps() const
    {
        return static_cast<std::uint32_t>(
            config.experimental().target_fps());
    }

    [[nodiscard]] bool GetEnableAbsoluteTimeJudgement() const
    {
        return config.experimental().enable_absolute_time_judgement();
    }

    [[nodiscard]] bool GetEnableTestModeStorageRedirect() const
    {
        return config.experimental().enable_testmode_storage_redirect();
    }

    [[nodiscard]] bool GetEnableTimerFreezePatches() const
    {
        return config.experimental().enable_timer_freeze_patches();
    }

    [[nodiscard]] bool GetUnlockAllSongsAndDifficulties() const
    {
        return config.experimental().unlock_all_songs_and_difficulties();
    }

    [[nodiscard]] bool GetEnableNesysServiceAdapterPatch() const
    {
        return config.experimental().enable_nesys_service_adapter_patch();
    }

    [[nodiscard]] gc::config::AudioBackend GetAudioBackend() const
    {
        return config.experimental().audio_backend();
    }

    [[nodiscard]] std::uint32_t GetWasapiExclusiveBufferMs() const
    {
        return static_cast<std::uint32_t>(
            config.experimental().wasapi_exclusive_buffer_ms());
    }

    [[nodiscard]] const std::string& GetAsioDriverName() const
    {
        return config.experimental().asio_driver_name();
    }

    [[nodiscard]] std::uint32_t GetAsioBufferFrames() const
    {
        return static_cast<std::uint32_t>(
            config.experimental().asio_buffer_frames());
    }

    [[nodiscard]] std::uint32_t GetAsioOutputBaseChannel() const
    {
        return static_cast<std::uint32_t>(
            config.experimental().asio_output_base_channel());
    }

    [[nodiscard]] const std::string& GetNesysServerIp() const
    {
        return config.nesys().server_ip();
    }

    [[nodiscard]] bool GetEnableRegistryConfigOverride() const
    {
        return config.registry().enabled();
    }

    [[nodiscard]] const RegistryConfig& GetRegistryConfig() const
    {
        return config.registry();
    }

    [[nodiscard]] gc::config::LoaderLogLevel GetLoaderLogLevel() const
    {
        return config.logging().level();
    }

    [[nodiscard]] std::expected<gc::system_path::RuntimeRoot, std::string>
    PrepareGameSystemPath(
        bool native_testmode_storage_available) noexcept;

    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;

private:
    ConfigManager();
    ~ConfigManager() = default;

    std::filesystem::path config_path_;
    bool document_migrated_{};
    InputConfig config;
};
