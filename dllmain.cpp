#include <windows.h>
#include <filesystem>
#include "InputManager.h"
#include "plog/Log.h"
#include "plog/Initializers/RollingFileInitializer.h"
#include "RfidEmu.h"
#include "SDL3/SDL.h"

#ifndef _M_IX86
 #error "Only Win32 version is supported!"
#endif
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        {
#ifdef _DEBUG
        
            // plog::init(plog::debug, "loader-log.txt");
#else
            plog::init(plog::info, "loader-log.txt");
#endif
        
            PLOG_DEBUG << "DLL attach!" << std::endl;
        
            RfidEmuInit();
            PLOG_DEBUG << "Rfid init complete!" << std::endl;

            break;
        }
    case DLL_PROCESS_DETACH:
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
        break;
    }

    return TRUE;
}