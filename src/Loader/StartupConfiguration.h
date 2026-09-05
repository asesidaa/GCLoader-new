#pragma once

#include "Config/ConfigCompiler.h"
#include "Nesys/NesysServiceProcess.h"
#include "SystemPath/SystemRoot.h"
#include "TestModeStorage/NativeStorageProbe.h"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace gc::loader
{
    struct StartupConfigurationActions
    {
        void* context{};
        std::expected<std::string, std::string> (*read_config)(
            void*, const std::filesystem::path&) noexcept{};
        testmode_storage::NativeStorageProbeResult (*probe_native_storage)(
            void*) noexcept{};
        std::expected<system_path::PreparedRoot, system_path::RootPrepareError>
            (*prepare_system_root)(void*, system_path::RootPrepareRequest) noexcept{};
        std::expected<void, config::ConfigPersistenceError> (*persist_config)(
            void*, const std::filesystem::path&, const config::ConfigDocument&) noexcept{};
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

    [[nodiscard]] std::string_view StartupConfigurationStageName(
        StartupConfigurationStage stage) noexcept;
    [[nodiscard]] std::string_view StartupConfigChangeName(
        StartupConfigChange change) noexcept;

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
