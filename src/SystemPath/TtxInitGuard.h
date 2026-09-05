#pragma once

#include "SystemPath/SystemRoot.h"

#include <Windows.h>
#include "Platform/Win32/Hooking/HookPlan.h"

#include <atomic>
#include <cstdint>
#include <expected>

namespace gc::system_path {

inline constexpr wchar_t kTtxModuleName[] = L"TtxUpdateDownloader.dll";
inline constexpr char kTtxUdlInitExport[] = "?TtxUDLInit@@YAHKKKK@Z";
using TtxUdlInitFn = int(__cdecl*)(
    unsigned int,
    unsigned int,
    unsigned int,
    unsigned int);

class TtxInitGuard {
public:
    explicit TtxInitGuard(RuntimeRoot root);
    TtxInitGuard(const TtxInitGuard&) = delete;
    TtxInitGuard& operator=(const TtxInitGuard&) = delete;

    [[nodiscard]] std::expected<void, hooking::HookError>
    AddHook(hooking::HookPlan&) noexcept;

private:
    static int __cdecl Detour(
        unsigned int priority,
        unsigned int game_version,
        unsigned int update_step,
        unsigned int update_options) noexcept;
    int Invoke(
        unsigned int priority,
        unsigned int game_version,
        unsigned int update_step,
        unsigned int update_options) noexcept;
    [[noreturn]] void PublishFailure(DWORD error) noexcept;

    static std::atomic<TtxInitGuard*> active_;
    RuntimeRoot root_;
    TtxUdlInitFn original_{};
};

} // namespace gc::system_path
