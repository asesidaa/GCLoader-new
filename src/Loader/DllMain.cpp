#include <WinSock2.h>
#include "Platform/Win32/Utf.h"
#include <windows.h>
#include <atomic>
#include <exception>
#include <filesystem>
#include <format>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include "Locale/JapaneseLocaleCompatibility.h"
#include "Loader/StartupConfiguration.h"
#include "Logging/LoggingSettings.h"
#include "plog/Log.h"
#include "plog/Init.h"
#include "Rfid/Feature.h"
#include "Loader/TransitionalVersionedStartup.h"
#include "Patches/RendererDeviceLoss/RendererDeviceLossPatch.h"
#include "Patches/WindowedWidescreen/WindowedWidescreenPatch.h"
#include "Patches/TestModeTiming/TimingSettingsPatch.h"
#include "Nesys/NesysServicePatch.h"
#include "Nesys/NesysServiceProcess.h"
#include "Logging/SessionLog.h"
#include "Input/Win32/ImeSuppression.h"
#include "Input/Polling/InputPollingRuntime.h"
#include "Audio/AudioPatch.h"
#include "Diagnostics/CrashDumpHandler.h"
#include "Diagnostics/FatalProcess.h"
#include "Loader/NonVersionedHookPlan.h"
#include "SystemPath/TtxInitGuard.h"

#ifndef _M_IX86
#error "Only Win32 version is supported!"
#endif

namespace
{
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

    std::wstring Utf8ToWideOrFallback(std::string_view value)
    {
        constexpr std::wstring_view fallback =
            L"GCLoader startup failed. Check loader-log.txt for details.";
        if (value.empty()) return std::wstring{fallback};
        return gc::platform::win32::Utf8ToWide(value).value_or(std::wstring{fallback});
    }

    void PublishConfigurationStartupFatal(
        const gc::loader::StartupConfigurationError& error) noexcept
    {
        constexpr wchar_t title[] =
            L"GCLoader configuration error";
        try
        {
            const auto log = std::format(
                "Configuration startup failed stage={} error={}",
                gc::loader::StartupConfigurationStageName(error.stage),
                error.message);
            const auto modal = Utf8ToWideOrFallback(error.message);
            gc::diagnostics::AbortProcess({
                log,
                modal,
                title});
            return;
        }
        catch (...)
        {
        }

        gc::diagnostics::AbortProcess({
            "Configuration startup failed while formatting diagnostics",
            L"GCLoader could not load or validate config.toml. Check the "
            L"loader log for details.",
            title});
    }

    void PublishConfigurationRoleMismatchFatal(
        gc::nesys_service::ProcessRole role) noexcept
    {
        try
        {
            const auto log = std::format(
                "Configuration startup role mismatch requested={}",
                gc::nesys_service::ProcessRoleName(role));
            gc::diagnostics::AbortProcess({
                log,
                L"GCLoader prepared configuration for the wrong process role. "
                L"The process was stopped before publishing feature state.",
                L"GCLoader configuration error"});
            return;
        }
        catch (...)
        {
        }

        gc::diagnostics::AbortProcess({
            "Configuration startup role mismatch",
            L"GCLoader prepared configuration for the wrong process role.",
            L"GCLoader configuration error"});
    }

    void PublishInputConfigurationFatal(std::string_view error) noexcept
    {
        constexpr wchar_t title[] =
            L"GCLoader input setup error";
        try
        {
            const auto log = std::format(
                "Input polling configuration failed error={}", error);
            gc::diagnostics::AbortProcess({
                log,
                Utf8ToWideOrFallback(error),
                title});
            return;
        }
        catch (...)
        {
        }

        gc::diagnostics::AbortProcess({
            "Input polling configuration failed",
            L"GCLoader could not publish the validated input settings. Check "
            L"loader-log.txt for details.",
            title});
    }

    void PublishImeSuppressionFatal(
        const gc::input::ImeSuppressionError& error) noexcept
    {
        constexpr wchar_t title[] =
            L"GCLoader input method setup error";

        try
        {
            std::ostringstream log;
            std::wostringstream modal;
            log << "Input IME suppression failed"
                << " api=ImmDisableIME"
                << " thread_selector=all"
                << " win32_error=" << error.win32_error;
            modal
                << L"GCLoader could not disable input method editors for the "
                L"game process.\n\n"
                << L"Windows error: " << error.win32_error
                << L"\n\nThe game was stopped before creating its input window "
                L"because an active IME could consume gameplay keys or show "
                L"a composition window.";
            gc::diagnostics::AbortProcess({
                log.str(),
                modal.str(),
                title});
            return;
        }
        catch (...)
        {
        }

        gc::diagnostics::AbortProcess({
            "Input IME suppression failed",
            L"GCLoader could not disable input method editors for the game "
            L"process. Check loader-log.txt for details.",
            title});
    }
}

// Windows discovers DllMain by its external entry-point linkage.
// ReSharper disable once CppUseInternalLinkage
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        {
            const auto role =
                gc::nesys_service::DetectCurrentProcessRole();
            InitProcessLog(role);

            if (gc::nesys_service::ShouldRunGameOnlyInitialization(role))
            {
                const auto ime_suppression =
                    gc::input::DisableProcessIme();
                if (!ime_suppression)
                {
                    PublishImeSuppressionFatal(ime_suppression.error());
                    return FALSE;
                }
                PLOG_INFO
                    << "Input IME suppression active"
                    << " api=ImmDisableIME"
                    << " thread_selector=all";

                gc::loader::InstallTransitionalGameCompatibility();
            }

            std::error_code current_path_error;
            const auto config_directory =
                std::filesystem::current_path(current_path_error);
            if (current_path_error)
            {
                gc::loader::StartupConfigurationError error{
                    .stage =
                    gc::loader::StartupConfigurationStage::read,
                    .message =
                    "Could not determine the current process directory: " +
                    current_path_error.message(),
                };
                PublishConfigurationStartupFatal(error);
                return FALSE;
            }

            auto prepared = gc::loader::PrepareProcessConfiguration(
                config_directory / "config.toml",
                role);
            if (!prepared)
            {
                PublishConfigurationStartupFatal(prepared.error());
                return FALSE;
            }

            if (!gc::nesys_service::ShouldRunGameOnlyInitialization(role))
            {
                auto* service =
                    std::get_if<gc::loader::NesysProcessConfiguration>(
                        &*prepared);
                if (service == nullptr)
                {
                    PublishConfigurationRoleMismatchFatal(role);
                    return FALSE;
                }

                ApplyConfiguredLogLevel(service->logging);
                PLOG_DEBUG << "DLL attach!" << std::endl;
                PLOG_INFO
                    << "NesysServicePatch: process role="
                    << gc::nesys_service::ProcessRoleName(role);
                gc::loader::InstallNesysNonVersionedHooks(
                    hModule, std::move(service->nesys));

                PLOG_INFO
                    << "NesysServicePatch: service role skipping"
                    << " game-only RFID/input/framerate initialization";
                break;
            }

            auto* game_result =
                std::get_if<gc::loader::GameProcessConfiguration>(
                    &*prepared);
            if (game_result == nullptr)
            {
                PublishConfigurationRoleMismatchFatal(role);
                return FALSE;
            }

            auto game = std::move(*game_result);
            auto settings = std::move(game.settings);
            ApplyConfiguredLogLevel(settings.logging());
            PLOG_DEBUG << "DLL attach!" << std::endl;
            PLOG_INFO
                << "NesysServicePatch: process role="
                << gc::nesys_service::ProcessRoleName(role);
            PLOG_INFO
                << "Configuration startup transaction persisted="
                << game.persisted;
            for (const auto change : game.changes)
            {
                PLOG_INFO
                    << "Configuration startup repair="
                    << gc::loader::StartupConfigChangeName(change);
            }
            PLOG_INFO
                << "System path prepared configured="
                << game.system_root.configured_path
                << " redirect=" << game.system_root.redirect_enabled;

            try
            {
                auto input_settings = settings.input();
                auto configured =
                    gc::input::ConfigureInputPollingRuntime(
                        std::move(input_settings));
                if (!configured)
                {
                    PublishInputConfigurationFatal(configured.error());
                    return FALSE;
                }
            }
            catch (const std::exception& error)
            {
                PublishInputConfigurationFatal(error.what());
                return FALSE;
            }
            catch (...)
            {
                PublishInputConfigurationFatal(
                    "Input settings copy failed unexpectedly");
                return FALSE;
            }

            gc::loader::InstallTransitionalOptionalPatches(settings);

            gc::loader::InstallGameNonVersionedHooks(hModule, settings, game.system_root);

            gc::loader::InstallTransitionalTestModeTiming();
            PLOG_DEBUG
                << "Test-mode timing settings initialization complete!";

            gc::loader::InstallTransitionalRendererDeviceLoss();
            PLOG_DEBUG
                << "Renderer device-loss retry initialization complete!";

            gc::loader::InstallTransitionalWidescreen(settings.windowed_widescreen());
            PLOG_DEBUG
                << "Windowed widescreen initialization complete!";

            gc::loader::InstallTransitionalAbsoluteJudgement(
                settings.judgement());

            gc::loader::InstallTransitionalFramerate(
                settings.framerate(), settings.audio().backend());
            PLOG_DEBUG
                << "Framerate runtime initialization complete!";

            gc::loader::InstallTransitionalSwitchInput(
                settings.switch_input());
            PLOG_DEBUG
                << "Switch gameplay input patch init complete!"
                << std::endl;
            break;
        }
    case DLL_PROCESS_DETACH:
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
        break;
    default:
        break;
    }

    return TRUE;
}
