#include "Loader/ProcessStartup.h"
#include "Logging/SessionLog.h"
#include <plog/Log.h>
#include <plog/Init.h>
#include <filesystem>

namespace gc::loader {
namespace {
    void InitProcessLog(gc::nesys_service::ProcessRole role)
    {
        static gc::session_log::SessionLogAppender loader_log_appender(
            gc::session_log::ProcessLogFileName(role));
        plog::init(plog::info, &loader_log_appender);
        gc::session_log::RegisterActiveProcessLogAppender(
            &loader_log_appender);
    }

    plog::Severity ToPlogSeverity(gc::logging::LoaderLogLevel level)
    {
        using enum gc::logging::LoaderLogLevel;
        switch (level)
        {
        case Debug: return plog::debug;
        case Verbose: return plog::verbose;
        case Info: return plog::info;
        }
        return plog::info;
    }

    void ApplyConfiguredLogLevel(
        const gc::logging::LoggingSettings& settings)
    {
        const auto level = settings.level();
        plog::get()->setMaxSeverity(ToPlogSeverity(level));
        PLOG_INFO
            << "Loader log level="
            << gc::logging::LoaderLogLevelName(level);
    }


}
std::expected<PreparedProcessConfiguration, StartupError> PrepareProcessStartup(HMODULE loader_module) noexcept {
    auto role = nesys_service::ProcessRole::Game;
    try {
        role = nesys_service::DetectCurrentProcessRole();
        InitProcessLog(role);
        if (!loader_module) return std::unexpected(StartupError{
            .role = role, .stage = StartupStage::preparation, .win32_error = ERROR_INVALID_HANDLE});
        std::error_code directory_error;
        const auto directory = std::filesystem::current_path(directory_error);
        if (directory_error) return std::unexpected(StartupError{
            .role = role, .stage = StartupStage::configuration,
            .configuration = StartupConfigurationError{.stage = StartupConfigurationStage::read,
                .message = "Could not determine the current process directory: " + directory_error.message()}});
        auto prepared = PrepareProcessConfiguration(directory / "config.toml", role);
        if (!prepared) return std::unexpected(StartupError{
            .role = role, .stage = StartupStage::configuration, .configuration = std::move(prepared.error())});
        if (role == nesys_service::ProcessRole::Service) {
            const auto* service = std::get_if<NesysProcessConfiguration>(&*prepared);
            if (!service) return std::unexpected(StartupError{.role = role, .stage = StartupStage::role});
            ApplyConfiguredLogLevel(service->logging);
        } else {
            const auto* game = std::get_if<GameProcessConfiguration>(&*prepared);
            if (!game) return std::unexpected(StartupError{.role = role, .stage = StartupStage::role});
            ApplyConfiguredLogLevel(game->settings.logging());
        }
        PLOG_DEBUG << "DLL attach!";
        PLOG_INFO << "NesysServicePatch: process role=" << nesys_service::ProcessRoleName(role);
        return std::move(*prepared);
    } catch (...) { return std::unexpected(StartupError{.role = role, .stage = StartupStage::exception}); }
}
}
