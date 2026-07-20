#include <WinSock2.h>
#include <windows.h>
#include <filesystem>
#include <string>
#include "Config/config.h"
#include "plog/Log.h"
#include "plog/Init.h"
#include "Rfid/Feature.h"
#include "Patches/Framerate/FrameratePatch.h"
#include "Patches/TestModeTiming/TimingSettingsPatch.h"
#include "Nesys/NesysServicePatch.h"
#include "Nesys/NesysServiceProcess.h"
#include "Logging/SessionLog.h"
#include "Input/Switch/SwitchInputPatch.h"
#include "Audio/Wasapi/WasapiAudioPatch.h"

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

void ApplyConfiguredLogLevel() {
    const auto level = ConfigManager::instance().GetLoaderLogLevel();
    plog::get()->setMaxSeverity(ToPlogSeverity(level));
    PLOG_INFO << "Loader log level=" << LoaderLogLevelName(level);
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
            ApplyConfiguredLogLevel();

            PLOG_DEBUG << "DLL attach!" << std::endl;
            PLOG_INFO
                << "NesysServicePatch: process role="
                << gc::nesys_service::ProcessRoleName(role);

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

                if (!gc::audio::WasapiAudioPatchInit()) {
                    PLOG_ERROR
                        << "WasapiAudioPatch: fail-closed DLL attach";
                    return FALSE;
                }

                const auto rfid_result = gc::rfid::InitializeFeature();
                if (!rfid_result) {
                    PLOG_ERROR
                        << "RFID/JVS feature initialization failed at stage "
                        << static_cast<int>(rfid_result.error().stage);
                    return FALSE;
                }
                PLOG_DEBUG << "RFID/JVS feature init complete!";

                if (!gc::framerate::FrameratePatchInit()) {
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
