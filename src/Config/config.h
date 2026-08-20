#pragma once

#include "Config/AudioConfig.h"
#include "Config/NativeInputConfig.h"
#include "Config/RegistryConfig.h"
#include "Config/TargetFps.h"
#include "SystemPath/SystemRoot.h"

#include <Windows.h>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

#include "plog/Log.h"

struct NesysConfig
{
    rfl::Rename<"server_ip", std::string> server_ip = "127.0.0.1";
};

namespace gc::config {

enum class LoaderLogLevel : std::uint8_t {
    Info,
    Debug,
    Verbose,
};

struct LoggingConfig {
    rfl::Rename<"level", LoaderLogLevel> level{LoaderLogLevel::Info};
};

inline constexpr bool IsSupportedLoaderLogLevel(
    LoaderLogLevel level) noexcept
{
    return level == LoaderLogLevel::Info ||
        level == LoaderLogLevel::Debug ||
        level == LoaderLogLevel::Verbose;
}

} // namespace gc::config

// Windows unsigned long is a distinct 32-bit numeric type.
using WasapiBufferMillisecondsConfigValue = unsigned long;
static_assert(
    sizeof(WasapiBufferMillisecondsConfigValue) == sizeof(std::uint32_t));

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

struct ExperimentalConfig
{
    rfl::Rename<"target_fps", gc::config::TargetFpsConfigValue>
        target_fps = gc::config::kMinimumTargetFps;
    rfl::Rename<"enable_absolute_time_judgement", bool>
        enable_absolute_time_judgement = false;
    rfl::Rename<"enable_testmode_storage_redirect", bool>
        enable_testmode_storage_redirect = false;
    rfl::Rename<"enable_timer_freeze_patches", bool>
        enable_timer_freeze_patches = false;
    rfl::Rename<"enable_nesys_service_adapter_patch", bool>
        enable_nesys_service_adapter_patch = true;
    rfl::Rename<"audio_backend", gc::config::AudioBackend>
        audio_backend = gc::config::AudioBackend::directsound;
    rfl::Rename<
        "wasapi_exclusive_buffer_ms",
        WasapiBufferMillisecondsConfigValue>
        wasapi_exclusive_buffer_ms = 10;
    rfl::Rename<"asio_driver_name", std::string> asio_driver_name;
    rfl::Rename<"asio_buffer_frames", unsigned long>
        asio_buffer_frames = 0;
    rfl::Rename<"asio_output_base_channel", unsigned long>
        asio_output_base_channel = 0;
};

struct InputConfig
{
    rfl::Rename<"input_schema_version", std::uint32_t>
        input_schema_version{gc::config::kInputSchemaVersion};
    rfl::Rename<"input_poll_hz", std::uint32_t> input_poll_hz{1000};
    rfl::Rename<"input_mode", gc::input::InputMode>
        input_mode{gc::input::InputMode::Keyboard};
    rfl::Rename<"gameplay_input_style", gc::input::GameplayInputStyle>
        gameplay_input_style{gc::input::GameplayInputStyle::Arcade};
    rfl::Rename<"axis_press_threshold_percent", std::uint32_t>
        axis_press_threshold_percent{50};
    rfl::Rename<"axis_release_threshold_percent", std::uint32_t>
        axis_release_threshold_percent{40};
    rfl::Rename<"keyboard", gc::config::NativeKeyboardConfig> keyboard;
    rfl::Rename<"controller", gc::config::ControllerConfig> controller;
    rfl::Rename<"nesys", NesysConfig> nesys;
    rfl::Rename<"registry", RegistryConfig> registry;
    rfl::Rename<"logging", gc::config::LoggingConfig> logging;
    rfl::Rename<"experimental", ExperimentalConfig> experimental;
};

namespace gc::config {

[[nodiscard]] std::expected<void, std::string> ValidateInputConfig(
    const InputConfig& config);
[[nodiscard]] std::expected<InputConfig, std::string>
ParseAndValidateInputConfig(std::string_view text);

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
