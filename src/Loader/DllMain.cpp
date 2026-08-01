#include <WinSock2.h>
#include <windows.h>
#include <atomic>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include "Config/config.h"
#include "plog/Log.h"
#include "plog/Init.h"
#include "Rfid/Feature.h"
#include "Patches/Framerate/FrameratePatch.h"
#include "Patches/RendererDeviceLoss/RendererDeviceLossPatch.h"
#include "Patches/TestModeTiming/TimingSettingsPatch.h"
#include "Nesys/NesysServicePatch.h"
#include "Nesys/NesysServiceProcess.h"
#include "Logging/SessionLog.h"
#include "Input/Switch/SwitchInputPatch.h"
#include "Audio/Wasapi/WasapiAudioPatch.h"
#include "Diagnostics/CrashDumpHandler.h"
#include "SystemPath/StartupFatal.h"

#ifndef _M_IX86
 #error "Only Win32 version is supported!"
#endif

namespace {

void InitProcessLog(gc::nesys_service::ProcessRole role) {
    static gc::session_log::SessionLogAppender loader_log_appender(
        gc::session_log::ProcessLogFileName(role));
    plog::init(plog::info, &loader_log_appender);
}

const char* LoaderLogLevelName(gc::config::LoaderLogLevel level) {
    using enum gc::config::LoaderLogLevel;
    switch (level) {
    case Info: return "Info";
    case Debug: return "Debug";
    case Verbose: return "Verbose";
    }
    return "Info";
}

plog::Severity ToPlogSeverity(gc::config::LoaderLogLevel level) {
    using enum gc::config::LoaderLogLevel;
    switch (level) {
    case Debug: return plog::debug;
    case Verbose: return plog::verbose;
    case Info: return plog::info;
    }
    return plog::info;
}

void ApplyConfiguredLogLevel(const ConfigManager& config) {
    const auto level = config.GetLoaderLogLevel();
    plog::get()->setMaxSeverity(ToPlogSeverity(level));
    PLOG_INFO << "Loader log level=" << LoaderLogLevelName(level);
}

std::wstring Utf8ToWideOrFallback(std::string_view value) {
    constexpr std::wstring_view fallback =
        L"System path preparation failed. Check the loader log for details.";
    try {
        if (value.empty() ||
            value.size() > static_cast<std::size_t>(
                std::numeric_limits<int>::max())) {
            return std::wstring{fallback};
        }
        const auto source_size = static_cast<int>(value.size());
        const int required = MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            value.data(),
            source_size,
            nullptr,
            0);
        if (required <= 0) {
            return std::wstring{fallback};
        }
        std::wstring converted(static_cast<std::size_t>(required), L'\0');
        if (MultiByteToWideChar(
                CP_UTF8,
                MB_ERR_INVALID_CHARS,
                value.data(),
                source_size,
                converted.data(),
                required) != required) {
            return std::wstring{fallback};
        }
        return converted;
    } catch (...) {
        return std::wstring{fallback};
    }
}

void PublishSystemPathPreparationFatal(std::string_view error) noexcept {
    static std::atomic_bool published{false};
    try {
        const auto modal = Utf8ToWideOrFallback(error);
        gc::system_path::PublishStartupFatal(
            published,
            error,
            modal,
            21);
    } catch (...) {
        gc::system_path::PublishStartupFatal(
            published,
            error,
            L"System path preparation failed. Check the loader log for details.",
            21);
    }
}

}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        {
            const auto role =
                gc::nesys_service::DetectCurrentProcessRole();
            InitProcessLog(role);

            if (gc::nesys_service::ShouldRunGameOnlyInitialization(role)) {
                const auto crash_dump_status =
                    gc::crash_dump::InstallGameCrashDumpHandler();
                PLOG_INFO
                    << "Game crash dump handler="
                    << gc::crash_dump::InstallStatusName(
                        crash_dump_status);
            }

            auto& config = ConfigManager::instance();
            ApplyConfiguredLogLevel(config);

            PLOG_DEBUG << "DLL attach!" << std::endl;
            PLOG_INFO
                << "NesysServicePatch: process role="
                << gc::nesys_service::ProcessRoleName(role);

            std::optional<gc::system_path::RuntimeRoot> system_root;
            if (gc::nesys_service::ShouldRunGameOnlyInitialization(role)) {
                auto prepared = config.PrepareGameSystemPath();
                if (!prepared) {
                    PublishSystemPathPreparationFatal(prepared.error());
                    return FALSE;
                }
                system_root = std::move(*prepared);
                PLOG_INFO
                    << "System path prepared configured="
                    << system_root->configured_path
                    << " redirect="
                    << system_root->redirect_enabled;
            }

            if (!gc::nesys_service::NesysServicePatchInit(
                    hModule,
                    role)) {
                PLOG_ERROR
                    << "NesysServicePatch: fail-closed DLL attach";
                return FALSE;
            }

            if (gc::nesys_service::ShouldRunGameOnlyInitialization(role)) {
                if (!gc::test_mode_timing::TimingSettingsPatchInit()) {
                    PLOG_ERROR
                        << "TestModeTiming: fail-closed DLL attach";
                    return FALSE;
                }
                PLOG_DEBUG
                    << "Test-mode timing settings initialization complete!";

                if (!gc::renderer_device_loss::RendererDeviceLossPatchInit()) {
                    PLOG_ERROR
                        << "RendererDeviceLossPatch: fail-closed DLL attach";
                    return FALSE;
                }
                PLOG_DEBUG
                    << "Renderer device-loss retry initialization complete!";

                if (!gc::audio::WasapiAudioPatchInit()) {
                    PLOG_ERROR
                        << "WasapiAudioPatch: fail-closed DLL attach";
                    return FALSE;
                }

                if (!system_root) {
                    PLOG_ERROR
                        << "System path: prepared game root unavailable";
                    return FALSE;
                }
                const auto rfid_result =
                    gc::rfid::InitializeFeature(*system_root);
                if (!rfid_result) {
                    PLOG_ERROR
                        << "RFID/JVS feature initialization failed at stage "
                        << static_cast<int>(rfid_result.error().stage);
                    return FALSE;
                }
                PLOG_DEBUG << "RFID/JVS feature init complete!";

                if (!gc::framerate::FrameratePatchInit(
                        gc::audio::IsWasapiAudioHookCommitted())) {
                    PLOG_ERROR
                        << "FrameratePatch: fail-closed DLL attach";
                    return FALSE;
                }
                PLOG_DEBUG
                    << "Framerate runtime initialization complete!";

                gc::switch_input::SwitchInputPatchInit();
                PLOG_DEBUG
                    << "Switch gameplay input patch init complete!"
                    << std::endl;
            } else {
                PLOG_INFO
                    << "NesysServicePatch: service role skipping"
                    << " game-only RFID/input/framerate initialization";
            }
            break;
        }
    case DLL_PROCESS_DETACH:
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
        break;
    }

    return TRUE;
}
