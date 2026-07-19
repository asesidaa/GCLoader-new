#include "Input/Polling/InputWorkerWait.h"

#include <array>
#include <string>

namespace gc::input {
namespace {

std::string Win32Failure(const char* operation)
{
    return std::string(operation) + " failed with Win32 error " +
        std::to_string(GetLastError());
}

} // namespace

std::expected<InputWorkerWake, std::string> WaitForInputWorkerWake(
    HANDLE stop_event,
    HANDLE timer)
{
    const std::array<HANDLE, 2> handles{stop_event, timer};
    for (;;)
    {
        if (WaitForSingleObject(stop_event, 0) == WAIT_OBJECT_0)
        {
            return InputWorkerWake::Stop;
        }

        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        {
            if (message.message == WM_QUIT)
            {
                return InputWorkerWake::Quit;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        const DWORD wait = MsgWaitForMultipleObjectsEx(
            static_cast<DWORD>(handles.size()),
            handles.data(),
            INFINITE,
            QS_ALLINPUT,
            MWMO_INPUTAVAILABLE);
        if (wait == WAIT_OBJECT_0)
        {
            return InputWorkerWake::Stop;
        }
        if (wait == WAIT_OBJECT_0 + 1)
        {
            return InputWorkerWake::Timer;
        }
        if (wait == WAIT_OBJECT_0 + handles.size())
        {
            continue;
        }
        if (wait == WAIT_FAILED)
        {
            return std::unexpected(
                Win32Failure("MsgWaitForMultipleObjectsEx"));
        }
        return std::unexpected(
            "Input worker wait returned an invalid result");
    }
}

} // namespace gc::input
