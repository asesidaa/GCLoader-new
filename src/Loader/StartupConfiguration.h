#pragma once

#include "Config/ConfigCompiler.h"
#include "Nesys/NesysServiceProcess.h"
#include "SystemPath/SystemRoot.h"
#include "TestModeStorage/NativeStorageProbe.h"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <variant>
#include <vector>

namespace gc::loader
{
    struct ConfigReadActions
    {
        void* context{};
        std::expected<std::string, std::string> (*read)(
            void*,
            const std::filesystem::path&) noexcept{};
    };

    struct StartupConfigurationActions
    {
        ConfigReadActions config_read;
        testmode_storage::NativeStorageProbeResult (*probe_native_storage)(
            void*) noexcept{};
        void* probe_context{};
        system_path::DirectoryActions directories;
        config::AtomicConfigWriteActions config_write;
    };

    enum class StartupConfigurationStage : std::uint8_t
    {
        read,
        document,
        semantic,
        system_path,
        persistence,
    };

    struct StartupConfigurationError
    {
        StartupConfigurationStage stage{};
        std::string message;
        config::ConfigErrors semantic_errors;
    };

    enum class StartupConfigChange : std::uint8_t
    {
        recognized_migration,
        system_path_fallback,
        native_storage_redirect,
    };

    struct GameProcessConfiguration
    {
        config::ValidatedConfig settings;
        system_path::RuntimeRoot system_root;
        std::vector<StartupConfigChange> changes;
        bool persisted{};
    };

    struct NesysProcessConfiguration
    {
        logging::LoggingSettings logging;
        nesys_service::NesysSettings nesys;
    };

    using PreparedProcessConfiguration = std::variant<
        GameProcessConfiguration,
        NesysProcessConfiguration>;

    [[nodiscard]] StartupConfigurationActions
    ProductionStartupConfigurationActions() noexcept;

    [[nodiscard]] std::expected<
        PreparedProcessConfiguration,
        StartupConfigurationError>
    PrepareProcessConfiguration(
        const std::filesystem::path& config_path,
        nesys_service::ProcessRole role,
        const StartupConfigurationActions& actions =
            ProductionStartupConfigurationActions()) noexcept;
} // namespace gc::loader
