#pragma once

#include "Platform/Win32/Hooking/MinHookTransaction.h"
#include "SystemPath/SystemRoot.h"
#include "SystemPath/TtxInitGuard.h"

#include <Windows.h>

#include <expected>

namespace gc::rfid {

enum class FeatureFailureStage {
    configuration,
    allocation,
    hook_installation,
    ttx_guard_installation,
};

struct FeatureError {
    FeatureFailureStage stage{};
    DWORD win32_error{ERROR_SUCCESS};
    gc::win32_hooks::HookInstallError hook{};
    gc::system_path::TtxGuardInstallError ttx{};
};

struct FeatureHookLayerActions {
    void* context{};
    std::expected<void, gc::win32_hooks::HookInstallError>
        (*install_kernel32)(void*) noexcept{};
    std::expected<void, gc::system_path::TtxGuardInstallError>
        (*install_ttx)(void*) noexcept{};
    void (*rollback_kernel32)(void*) noexcept{};
    void (*deactivate_kernel32)(void*) noexcept{};
};

[[nodiscard]] std::expected<void, FeatureError>
InstallFeatureHookLayers(FeatureHookLayerActions actions) noexcept;

[[nodiscard]] std::expected<void, FeatureError>
InitializeFeature(
    const gc::system_path::RuntimeRoot& system_root) noexcept;

} // namespace gc::rfid
