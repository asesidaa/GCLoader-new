#pragma once

#include "Audio/AudioSettings.h"
#include "Config/NativeInputConfig.h"
#include "Config/RegistryConfig.h"
#include "Logging/LoggingSettings.h"
#include "Patches/Framerate/FrameratePolicy.h"
#include "Patches/WindowedWidescreen/WindowedWidescreenSettings.h"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>

namespace gc::config
{
    using TargetFpsConfigValue = unsigned long;
    static_assert(sizeof(TargetFpsConfigValue) == sizeof(std::uint32_t));

    struct NesysConfig
    {
        rfl::Rename<"server_ip", std::string> server_ip{"127.0.0.1"};
    };

    struct LoggingConfig
    {
        rfl::Rename<"level", logging::LoaderLogLevel>
        level{logging::LoaderLogLevel::Info};
    };

    using WasapiBufferMillisecondsConfigValue = unsigned long;
    static_assert(
        sizeof(WasapiBufferMillisecondsConfigValue) == sizeof(std::uint32_t));

    struct ExperimentalConfig
    {
        rfl::Rename<"target_fps", TargetFpsConfigValue>
        target_fps{framerate::kMinimumTargetFps};
        rfl::Rename<"enable_absolute_time_judgement", bool>
        enable_absolute_time_judgement{false};
        rfl::Rename<"enable_auto_play", bool>
        enable_auto_play{false};
        rfl::Rename<"enable_testmode_storage_redirect", bool>
        enable_testmode_storage_redirect{false};
        rfl::Rename<"enable_timer_freeze_patches", bool>
        enable_timer_freeze_patches{false};
        rfl::Rename<"unlock_all_songs_and_difficulties", bool>
        unlock_all_songs_and_difficulties{false};
        rfl::Rename<"enable_nesys_service_adapter_patch", bool>
        enable_nesys_service_adapter_patch{true};
        rfl::Rename<"enable_windowed_widescreen_stage", bool>
        enable_windowed_widescreen_stage{false};
        rfl::Rename<"widescreen_window_width", unsigned long>
        widescreen_window_width{1920};
        rfl::Rename<"widescreen_window_height", unsigned long>
        widescreen_window_height{1280};
        rfl::Rename<
            "widescreen_hud_placement",
            windowed_widescreen::GameplayHudPlacement>
        widescreen_hud_placement{
            windowed_widescreen::GameplayHudPlacement::center};
        rfl::Rename<"audio_backend", audio::AudioBackend>
        audio_backend{audio::AudioBackend::directsound};
        rfl::Rename<
            "wasapi_exclusive_buffer_ms",
            WasapiBufferMillisecondsConfigValue>
        wasapi_exclusive_buffer_ms{10};
        rfl::Rename<"asio_driver_name", std::string> asio_driver_name;
        rfl::Rename<"asio_buffer_frames", unsigned long>
        asio_buffer_frames{0};
        rfl::Rename<"asio_output_base_channel", unsigned long>
        asio_output_base_channel{0};
    };

    struct ConfigDocument
    {
        rfl::Rename<"input_schema_version", std::uint32_t>
        input_schema_version{kInputSchemaVersion};
        rfl::Rename<"input_poll_hz", std::uint32_t> input_poll_hz{1000};
        rfl::Rename<"input_mode", input::InputMode>
        input_mode{input::InputMode::Keyboard};
        rfl::Rename<"gameplay_input_style", input::GameplayInputStyle>
        gameplay_input_style{input::GameplayInputStyle::Arcade};
        rfl::Rename<"axis_press_threshold_percent", std::uint32_t>
        axis_press_threshold_percent{50};
        rfl::Rename<"axis_release_threshold_percent", std::uint32_t>
        axis_release_threshold_percent{40};
        rfl::Rename<"keyboard", NativeKeyboardConfig> keyboard;
        rfl::Rename<"controller", ControllerConfig> controller;
        rfl::Rename<"nesys", NesysConfig> nesys;
        rfl::Rename<"registry", ::RegistryConfig> registry;
        rfl::Rename<"logging", LoggingConfig> logging;
        rfl::Rename<"experimental", ExperimentalConfig> experimental;
    };

    enum class ConfigDocumentLoadErrorCode : std::uint8_t
    {
        toml_syntax,
        obsolete_schema,
        unsupported_schema,
        strict_shape,
        serialization,
    };

    struct ConfigDocumentLoadError
    {
        ConfigDocumentLoadErrorCode code{};
        std::string message;
    };

    struct ConfigDocumentMigrations
    {
        bool registry_paths{};
        bool audio_backend{};

        [[nodiscard]] bool any() const noexcept
        {
            return registry_paths || audio_backend;
        }
    };

    struct ParsedConfigDocument
    {
        ConfigDocument document;
        ConfigDocumentMigrations migrations;
    };

    [[nodiscard]] std::expected<ParsedConfigDocument, ConfigDocumentLoadError>
    ParseConfigDocument(std::string_view text) noexcept;

    [[nodiscard]] std::expected<std::string, ConfigDocumentLoadError>
    SerializeConfigDocument(const ConfigDocument& document) noexcept;

    enum class ConfigPersistenceStage : std::uint8_t
    {
        serialize,
        temporary_write,
        atomic_replace,
    };

    struct ConfigPersistenceError
    {
        ConfigPersistenceStage stage{};
        std::string message;
    };

    [[nodiscard]] std::expected<void, ConfigPersistenceError>
    WriteConfigDocumentAtomically(
        const std::filesystem::path& path,
        const ConfigDocument& document) noexcept;
} // namespace gc::config
