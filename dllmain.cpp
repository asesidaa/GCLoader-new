#include <windows.h>
#include <filesystem>
#include <string>
#include "InputManager.h"
#include "plog/Log.h"
#include "plog/Appenders/IAppender.h"
#include "plog/Converters/NativeEOLConverter.h"
#include "plog/Converters/UTF8Converter.h"
#include "plog/Formatters/TxtFormatter.h"
#include "plog/Init.h"
#include "RfidEmu.h"
#include "SDL3/SDL.h"
#include "FrameratePatch.h"
#include "NesysServicePatch.h"
#include "NesysServiceProcess.h"
#include "SwitchInputPatch.h"

#ifndef _M_IX86
 #error "Only Win32 version is supported!"
#endif

namespace {

class SharedWin32LogAppender final : public plog::IAppender
{
public:
    explicit SharedWin32LogAppender(LPCWSTR fileName)
        : file_(CreateFileW(
              fileName,
              FILE_APPEND_DATA,
              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
              nullptr,
              OPEN_ALWAYS,
              FILE_ATTRIBUTE_NORMAL,
              nullptr))
    {
    }

    ~SharedWin32LogAppender() override
    {
        if (file_ != INVALID_HANDLE_VALUE)
        {
            CloseHandle(file_);
        }
    }

    void write(const plog::Record& record) override
    {
        if (file_ == INVALID_HANDLE_VALUE)
        {
            return;
        }

        const auto message = plog::NativeEOLConverter<plog::UTF8Converter>::convert(
            plog::TxtFormatter::format(record));
        plog::util::MutexLock lock(mutex_);
        DWORD written = 0;
        WriteFile(file_, message.data(), static_cast<DWORD>(message.size()), &written, nullptr);
    }

private:
    HANDLE file_;
    plog::util::Mutex mutex_;
};

void InitSharedLog()
{
    static SharedWin32LogAppender loaderLogAppender(L"loader-log.txt");
    plog::init(plog::info, &loaderLogAppender);
}

}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        {
            DisableThreadLibraryCalls(hModule);
            InitSharedLog();
        
            PLOG_DEBUG << "DLL attach!" << std::endl;

            const auto role = gc::nesys_service::DetectCurrentProcessRole();
            PLOG_INFO << "NesysServicePatch: process role=" << gc::nesys_service::ProcessRoleName(role);

            if (gc::nesys_service::ShouldRunGameOnlyInitialization(role)) {
                RfidEmuInit();
                PLOG_DEBUG << "Rfid init complete!" << std::endl;

                FrameratePatchInit();
                PLOG_DEBUG << "120 FPS runtime patch init complete!" << std::endl;

                gc::switch_input::SwitchInputPatchInit();
                PLOG_DEBUG << "Switch gameplay input patch init complete!" << std::endl;
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
