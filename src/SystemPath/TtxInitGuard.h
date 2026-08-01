#pragma once

#include "SystemPath/SystemRoot.h"

#include <Windows.h>
#include <safetyhook.hpp>

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

enum class TtxGuardInstallStage {
    invalid_actions,
    resolve_module,
    resolve_export,
    create_hook,
    enable_hook,
};

[[nodiscard]] constexpr const char* TtxGuardInstallStageName(
    TtxGuardInstallStage stage) noexcept
{
    switch (stage) {
    case TtxGuardInstallStage::invalid_actions:
        return "invalid_actions";
    case TtxGuardInstallStage::resolve_module:
        return "resolve_module";
    case TtxGuardInstallStage::resolve_export:
        return "resolve_export";
    case TtxGuardInstallStage::create_hook:
        return "create_hook";
    case TtxGuardInstallStage::enable_hook:
        return "enable_hook";
    }
    return "unknown";
}

struct TtxGuardInstallError {
    TtxGuardInstallStage stage{};
    DWORD win32_error{ERROR_SUCCESS};
    std::uint32_t safetyhook_error{};
};

struct TtxGuardRuntimeActions {
    void* context{};
    int (*call_original)(
        void*,
        unsigned int,
        unsigned int,
        unsigned int,
        unsigned int) noexcept{};
    DWORD (*get_last_error)(void*) noexcept{};
    void (*publish_failure)(
        void*,
        DWORD,
        const RuntimeRoot&) noexcept{};
};

[[nodiscard]] int InvokeTtxUdlInitGuard(
    unsigned int priority,
    unsigned int game_version,
    unsigned int update_step,
    unsigned int update_options,
    const RuntimeRoot& root,
    TtxGuardRuntimeActions actions) noexcept;

struct TtxGuardInstallActions {
    void* context{};
    void* detour{};
    HMODULE (*get_module)(void*, LPCWSTR) noexcept{};
    FARPROC (*get_export)(void*, HMODULE, LPCSTR) noexcept{};
    DWORD (*get_last_error)(void*) noexcept{};
    std::expected<void, std::uint32_t> (*create_disabled)(
        void*, void* target, void* detour) noexcept{};
    std::expected<void, std::uint32_t> (*enable)(void*) noexcept{};
    void (*reset)(void*) noexcept{};
};

[[nodiscard]] std::expected<void, TtxGuardInstallError>
InstallTtxInitGuard(TtxGuardInstallActions actions) noexcept;

class TtxInitGuard {
public:
    explicit TtxInitGuard(RuntimeRoot root);
    ~TtxInitGuard() noexcept;
    TtxInitGuard(const TtxInitGuard&) = delete;
    TtxInitGuard& operator=(const TtxInitGuard&) = delete;

    [[nodiscard]] std::expected<void, TtxGuardInstallError>
    Install() noexcept;
    void Reset() noexcept;

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
    void PublishFailure(DWORD error) noexcept;

    static std::atomic<TtxInitGuard*> active_;
    RuntimeRoot root_;
    safetyhook::InlineHook hook_;
    std::atomic_bool failure_published_{};
};

} // namespace gc::system_path
