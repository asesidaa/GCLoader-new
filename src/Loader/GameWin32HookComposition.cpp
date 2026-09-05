#include "Loader/GameWin32HookComposition.h"
#include "Diagnostics/FatalProcess.h"
#include "Nesys/Diagnostics/GamePipeWin32Observers.h"
#include "Rfid/Win32FileHandlers.h"
#include "SystemPath/TtxInitGuard.h"
#include "SystemPath/Win32PathHandlers.h"
#include "TestModeStorage/Win32PathHandlers.h"
#include "Win32Hooks/Kernel32Dispatcher.h"
#include <format>

namespace gc::loader {
namespace {
struct GamePathState final {
    GamePathState(const system_path::RuntimeRoot& root, bool storage_enabled)
        : system(root), storage(storage_enabled), ttx(root) {}
    system_path::SystemPathRouter system;
    testmode_storage::Hooks storage;
    system_path::TtxInitGuard ttx;
};
[[noreturn]] void AbortRegistration(const win32_hooks::RegistrationError& error) noexcept {
    try {
        diagnostics::AbortProcess({
            std::format("Win32 handler registration failed stage={} feature={} site={}",
                static_cast<unsigned>(error.stage), error.identity.feature, error.identity.site),
            L"GCLoader could not compose its Win32 handlers. Check loader-log.txt.",
            L"GCLoader hook setup error"});
    } catch (...) { diagnostics::AbortProcess({}); }
}
}
std::expected<void, win32_hooks::RegistrationError> ComposeGameWin32Handlers(
    win32_hooks::Kernel32Dispatcher& dispatcher, rfid::Runtime& rfid,
    system_path::SystemPathRouter& system, testmode_storage::Hooks& storage) noexcept {
    // Explicit policy order: RFID -> system roots -> test-mode storage.
    // Each contributor registers only its baseline APIs and enabled routes.
    if (const auto result = rfid::AddWin32FileHandlers(dispatcher, rfid); !result) return result;
    if (const auto result = system_path::AddWin32PathHandlers(dispatcher, system); !result) return result;
    if (const auto result = testmode_storage::AddWin32PathHandlers(dispatcher, storage); !result) return result;
    if (const auto result = nesys_service::diagnostics::AddGamePipeWin32Observers(dispatcher); !result)
        return result;
    return dispatcher.Publish();
}
std::expected<void, hooking::HookError> AddGameWin32Hooks(
    hooking::HookPlan& plan, const system_path::RuntimeRoot& root,
    bool storage_enabled, rfid::Runtime& rfid) noexcept {
    try {
        // Handler state and TTX original storage live for the entire process.
        auto* state = new GamePathState(root, storage_enabled);
        auto& dispatcher = win32_hooks::Kernel32Dispatcher::ProcessLifetime();
        if (const auto result = ComposeGameWin32Handlers(dispatcher, rfid, state->system, state->storage); !result)
            AbortRegistration(result.error());
        if (const auto result = AddSharedKernel32Hooks(plan, dispatcher); !result) return result;
        return state->ttx.AddHook(plan);
    } catch (...) { diagnostics::AbortProcess({}); }
}
}
