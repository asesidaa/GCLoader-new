#pragma once

#include <Windows.h>
#include <MinHook.h>

#include <array>
// ReSharper disable once CppUnusedIncludeDirective
#include <cstddef>
#include <expected>
#include <span>

namespace gc::win32_hooks {

inline constexpr std::size_t kMaxOwnedKernel32Hooks = 32;

struct HookRequest {
    LPCWSTR module_name{};
    LPCSTR export_name{};
    LPVOID detour{};
    LPVOID* original{};
};

struct ResolverApi {
    HMODULE(WINAPI* get_module_handle_w)(LPCWSTR);
    FARPROC(WINAPI* get_proc_address)(HMODULE, LPCSTR);
};

struct MinHookApi {
    decltype(&MH_Initialize) initialize;
    decltype(&MH_CreateHook) create;
    decltype(&MH_EnableHook) enable;
    decltype(&MH_DisableHook) disable;
    decltype(&MH_RemoveHook) remove;
};

enum class HookInstallStage {
    none,
    too_many_hooks,
    resolve_module,
    resolve_export,
    initialize,
    create,
    enable,
};

[[nodiscard]] constexpr const char* HookInstallStageName(
    HookInstallStage stage) noexcept
{
    switch (stage) {
    case HookInstallStage::none:
        return "none";
    case HookInstallStage::too_many_hooks:
        return "too_many_hooks";
    case HookInstallStage::resolve_module:
        return "resolve_module";
    case HookInstallStage::resolve_export:
        return "resolve_export";
    case HookInstallStage::initialize:
        return "initialize";
    case HookInstallStage::create:
        return "create";
    case HookInstallStage::enable:
        return "enable";
    }
    return "unknown";
}

struct HookInstallError {
    HookInstallStage stage{};
    LPCSTR export_name{};
    LPVOID target{};
    DWORD win32_error{ERROR_SUCCESS};
    MH_STATUS minhook_status{MH_OK};
};

[[nodiscard]] ResolverApi ProductionResolverApi() noexcept;
[[nodiscard]] MinHookApi ProductionMinHookApi() noexcept;

class MinHookTransaction {
public:
    MinHookTransaction(
        ResolverApi resolver = ProductionResolverApi(),
        const MinHookApi& minhook = ProductionMinHookApi()) noexcept;

    [[nodiscard]] std::expected<void, HookInstallError> Install(
        std::span<const HookRequest> requests) noexcept;
    void Rollback() noexcept;

private:
    ResolverApi resolver_;
    MinHookApi minhook_;
    std::array<LPVOID, kMaxOwnedKernel32Hooks> created_{};
    std::array<LPVOID, kMaxOwnedKernel32Hooks> enabled_{};
    std::size_t created_count_{};
    std::size_t enabled_count_{};
    bool committed_{};
};

} // namespace gc::win32_hooks
