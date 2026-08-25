#pragma once

#include <Windows.h>

#include <atomic>
#include <string_view>

namespace gc::system_path {

struct StartupFatalActions {
    void* context{};
    void (*log_error)(void*, const char*) noexcept{};
    void (*show_error)(
        void*,
        const wchar_t*,
        const wchar_t*) noexcept{};
    void (*terminate_process)(void*, DWORD) noexcept{};
    void (*fail_fast)(void*) noexcept{};
};

[[nodiscard]] StartupFatalActions
ProductionStartupFatalActions() noexcept;

void PublishStartupFatal(
    std::atomic_bool& latch,
    std::string_view log,
    std::wstring_view modal,
    DWORD exit_code,
    const StartupFatalActions& actions =
        ProductionStartupFatalActions()) noexcept;

void PublishStartupFatal(
    std::atomic_bool& latch,
    std::string_view log,
    std::wstring_view modal,
    std::wstring_view title,
    DWORD exit_code,
    const StartupFatalActions& actions =
        ProductionStartupFatalActions()) noexcept;

} // namespace gc::system_path
