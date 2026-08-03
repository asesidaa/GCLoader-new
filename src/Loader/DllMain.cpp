#include <WinSock2.h>
#include <windows.h>
#include <atomic>
#include <filesystem>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include "Config/config.h"
#include "Font/FontCharsetCompatibility.h"
#include "Locale/JapaneseLocaleCompatibility.h"
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
#include "SystemPath/TtxInitGuard.h"
#include "TestModeStorage/NativeStorageProbe.h"

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

void PublishFeatureInitializationFatal(
    const gc::rfid::FeatureError& error) noexcept {
    static std::atomic_bool published{false};
    constexpr DWORD exit_code = 23;
    constexpr std::wstring_view title = L"GCLoader hook setup error";

    try {
        std::ostringstream log;
        std::wostringstream modal;
        if (error.stage ==
            gc::rfid::FeatureFailureStage::ttx_guard_installation) {
            const auto stage =
                gc::system_path::TtxGuardInstallStageName(error.ttx.stage);
            log << "Ttx guard setup failed module=TtxUpdateDownloader.dll "
                << "export=?TtxUDLInit@@YAHKKKK@Z"
                << " stage=" << stage
                << " win32_error=" << error.ttx.win32_error
                << " safetyhook_error=" << error.ttx.safetyhook_error;
            modal
                << L"The supported TtxUpdateDownloader initialization guard "
                   L"could not be installed.\n\n"
                << L"Module: TtxUpdateDownloader.dll\n"
                << L"Export: ?TtxUDLInit@@YAHKKKK@Z\n"
                << L"Stage: " << Utf8ToWideOrFallback(stage) << L"\n"
                << L"Windows error: " << error.ttx.win32_error << L"\n"
                << L"SafetyHook error: " << error.ttx.safetyhook_error
                << L"\n\nVerify that the game uses the supported downloader "
                   L"binary and that security software is not blocking hooks.";
        } else if (error.stage ==
                   gc::rfid::FeatureFailureStage::hook_installation) {
            const auto stage = gc::win32_hooks::HookInstallStageName(
                error.hook.stage);
            const std::string_view export_name =
                error.hook.export_name == nullptr
                    ? std::string_view{"<none>"}
                    : std::string_view{error.hook.export_name};
            log << "Game Kernel32 hook setup failed stage=" << stage
                << " export=" << export_name
                << " win32_error=" << error.hook.win32_error
                << " minhook_status="
                << static_cast<int>(error.hook.minhook_status);
            modal
                << L"The game Kernel32 hook layer could not be installed.\n\n"
                << L"Stage: " << Utf8ToWideOrFallback(stage) << L"\n"
                << L"Export: " << Utf8ToWideOrFallback(export_name) << L"\n"
                << L"Windows error: " << error.hook.win32_error << L"\n"
                << L"MinHook status: "
                << static_cast<int>(error.hook.minhook_status)
                << L"\n\nCheck the loader log and verify that security software "
                   L"is not blocking hooks.";
        } else {
            log << "Game feature initialization failed stage="
                << static_cast<int>(error.stage)
                << " win32_error=" << error.win32_error;
            modal
                << L"GCLoader could not initialize the game feature layer.\n\n"
                << L"Stage: " << static_cast<int>(error.stage) << L"\n"
                << L"Windows error: " << error.win32_error
                << L"\n\nCheck the loader log for details.";
        }

        gc::system_path::PublishStartupFatal(
            published,
            log.str(),
            modal.str(),
            title,
            exit_code);
        return;
    } catch (...) {
    }

    gc::system_path::PublishStartupFatal(
        published,
        "GCLoader game hook setup failed",
        L"GCLoader could not install the required game hooks. Check the "
        L"loader log for details.",
        title,
        exit_code);
}

void PublishJapaneseLocaleCompatibilityFatal(
    const gc::win32_hooks::HookInstallError& error) noexcept {
    static std::atomic_bool published{false};
    constexpr DWORD exit_code = 25;
    constexpr std::wstring_view title =
        L"GCLoader Japanese locale setup error";

    try {
        const auto stage =
            gc::win32_hooks::HookInstallStageName(error.stage);
        const std::string_view export_name =
            error.export_name == nullptr
                ? std::string_view{"<none>"}
                : std::string_view{error.export_name};
        std::ostringstream log;
        std::wostringstream modal;
        log << "Japanese locale hook setup failed"
            << " stage=" << stage
            << " export=" << export_name
            << " win32_error=" << error.win32_error
            << " minhook_status="
            << static_cast<int>(error.minhook_status);
        modal
            << L"The required Japanese locale compatibility hooks could not "
               L"be installed.\n\n"
            << L"Stage: " << Utf8ToWideOrFallback(stage) << L"\n"
            << L"Export: " << Utf8ToWideOrFallback(export_name) << L"\n"
            << L"Windows error: " << error.win32_error << L"\n"
            << L"MinHook status: "
            << static_cast<int>(error.minhook_status)
            << L"\n\nThe game cannot safely start under the host locale.";
        gc::system_path::PublishStartupFatal(
            published,
            log.str(),
            modal.str(),
            title,
            exit_code);
        return;
    } catch (...) {
    }

    gc::system_path::PublishStartupFatal(
        published,
        "Japanese locale hook setup failed",
        L"The required Japanese locale compatibility hooks could not be "
        L"installed. Check the loader log for details.",
        title,
        exit_code);
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

            const auto locale =
                gc::locale_compatibility::
                    InstallJapaneseLocaleCompatibility(role);
            if (!locale) {
                PublishJapaneseLocaleCompatibilityFatal(
                    locale.error());
                return FALSE;
            }

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
                const auto native_testmode_storage =
                    gc::testmode_storage::ProbeNativeStorage();
                if (native_testmode_storage.available) {
                    PLOG_INFO
                        << "Native test-mode storage: available";
                } else {
                    PLOG_WARNING
                        << "Native test-mode storage: unavailable stage="
                        << gc::testmode_storage::NativeStorageProbeStageName(
                               native_testmode_storage.failed_stage)
                        << " win32_error="
                        << native_testmode_storage.win32_error
                        << "; enabling persisted redirect";
                }
                if (native_testmode_storage.cleanup_error !=
                    ERROR_SUCCESS) {
                    PLOG_WARNING
                        << "Native test-mode storage: probe cleanup failed "
                           "win32_error="
                        << native_testmode_storage.cleanup_error;
                }

                auto prepared = config.PrepareGameSystemPath(
                    native_testmode_storage.available);
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
                static_cast<void>(
                    gc::font::InstallJapaneseFontCharsetCompatibility());

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
                    PublishFeatureInitializationFatal(
                        rfid_result.error());
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
