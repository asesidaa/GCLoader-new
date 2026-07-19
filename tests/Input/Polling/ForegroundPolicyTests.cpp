#include "Input/Polling/ForegroundPolicy.h"

#include <Windows.h>

#include <cstdint>
#include <iostream>

namespace {

HWND g_foreground{};
DWORD g_foreground_process{};

HWND WINAPI FakeGetForegroundWindow()
{
    return g_foreground;
}

DWORD WINAPI FakeGetWindowThreadProcessId(HWND window, LPDWORD process_id)
{
    if (process_id != nullptr)
    {
        *process_id = window == g_foreground ? g_foreground_process : 0;
    }
    return window == nullptr ? 0 : 123;
}

int expect(bool condition, const char* name)
{
    if (condition)
    {
        return 0;
    }
    std::cerr << name << ": expectation failed\n";
    return 1;
}

} // namespace

int main()
{
    int failures = 0;
    constexpr DWORD current_process = 471;
    const gc::input::ForegroundApi api{
        .get_foreground_window = FakeGetForegroundWindow,
        .get_window_thread_process_id = FakeGetWindowThreadProcessId,
        .current_process_id = current_process,
    };

    g_foreground = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(1));
    g_foreground_process = current_process;
    failures += expect(
        gc::input::IsCurrentProcessForeground(api),
        "current process accepted");

    g_foreground_process = 999;
    failures += expect(
        !gc::input::IsCurrentProcessForeground(api),
        "different process rejected");

    g_foreground = nullptr;
    failures += expect(
        !gc::input::IsCurrentProcessForeground(api),
        "null foreground rejected");

    gc::input::ForegroundTransitionTracker tracker;
    const auto first_background = tracker.Update(false);
    failures += expect(
        first_background.changed && first_background.clear_input,
        "first background transition requests one clear");
    const auto repeated_background = tracker.Update(false);
    failures += expect(
        !repeated_background.changed && !repeated_background.clear_input,
        "repeated background state is quiet");
    const auto foreground = tracker.Update(true);
    failures += expect(
        foreground.changed && !foreground.clear_input,
        "foreground transition is reported without clear");
    const auto repeated_foreground = tracker.Update(true);
    failures += expect(
        !repeated_foreground.changed && !repeated_foreground.clear_input,
        "repeated foreground state is quiet");

    return failures == 0 ? 0 : 1;
}
