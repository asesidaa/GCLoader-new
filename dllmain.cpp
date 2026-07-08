#include <windows.h>
#include <filesystem>
#include "InputManager.h"
#include "plog/Log.h"
#include "plog/Initializers/RollingFileInitializer.h"
#include "RfidEmu.h"
#include "SDL3/SDL.h"
#include "FrameratePatch.h"
#include "NesysServicePatch.h"
#include "NesysServiceProcess.h"

#ifndef _M_IX86
 #error "Only Win32 version is supported!"
#endif
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        {
            DisableThreadLibraryCalls(hModule);
#ifdef _DEBUG
        
            // plog::init(plog::debug, "loader-log.txt");
#else
            plog::init(plog::info, "loader-log.txt");
#endif
        
            PLOG_DEBUG << "DLL attach!" << std::endl;

            const auto role = gc::nesys_service::DetectCurrentProcessRole();
            PLOG_INFO << "NesysServicePatch: process role=" << gc::nesys_service::ProcessRoleName(role);

            if (gc::nesys_service::ShouldRunGameOnlyInitialization(role)) {
                RfidEmuInit();
                PLOG_DEBUG << "Rfid init complete!" << std::endl;

                FrameratePatchInit();
                PLOG_DEBUG << "120 FPS runtime patch init complete!" << std::endl;
            } else {
                PLOG_INFO << "NesysServicePatch: service role skipping game-only RFID/input/framerate initialization";
            }

            gc::nesys_service::NesysServicePatchInit(hModule);

            break;
        }
    case DLL_PROCESS_DETACH:
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
        break;
    }

    return TRUE;
}
