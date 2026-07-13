#include <WinSock2.h>
#include <windows.h>
#include <filesystem>
#include <string>
#include "InputManager.h"
#include "plog/Log.h"
#include "plog/Init.h"
#include "RfidEmu.h"
#include "SDL3/SDL.h"
#include "FrameratePatch.h"
#include "NesysServicePatch.h"
#include "NesysServiceProcess.h"
#include "SessionLog.h"
#include "SwitchInputPatch.h"
#include "WasapiAudioPatch.h"

#ifndef _M_IX86
 #error "Only Win32 version is supported!"
#endif

namespace {

void InitProcessLog(gc::nesys_service::ProcessRole role) {
    static gc::session_log::SessionLogAppender loader_log_appender(
        gc::session_log::ProcessLogFileName(role));
    plog::init(plog::info, &loader_log_appender);
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
                if (!gc::audio::WasapiAudioPatchInit()) {
                    PLOG_ERROR
                        << "WasapiAudioPatch: fail-closed DLL attach";
                    return FALSE;
                }

                RfidEmuInit();
                PLOG_DEBUG << "Rfid init complete!" << std::endl;

                FrameratePatchInit();
                PLOG_DEBUG
                    << "120 FPS runtime patch init complete!"
                    << std::endl;

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
