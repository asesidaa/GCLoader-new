#pragma once
#include "Loader/StartupConfiguration.h"
#include "Loader/StartupPlanError.h"
#include "Loader/VersionedStartupExecutor.h"
#include "Platform/Win32/Hooking/HookError.h"
#include "Win32Hooks/HookDecision.h"

namespace gc::loader {
enum class StartupStage { preparation, configuration, role, ime, input, feature, exception };
struct StartupError final {
    nesys_service::ProcessRole role{nesys_service::ProcessRole::Game};
    StartupStage stage{StartupStage::preparation};
    std::optional<StartupConfigurationError> configuration;
    std::optional<StartupPlanError> versioned;
    std::optional<StartupInstallError> installation;
    std::optional<hooking::HookError> hook;
    std::optional<win32_hooks::RegistrationError> registration;
    std::string_view feature;
    std::string_view operation;
    DWORD win32_error{};
    std::string detail;
};
[[nodiscard]] diagnostics::FatalProcessReport FormatStartupError(const StartupError&);
[[noreturn]] void AbortForStartupError(const StartupError&) noexcept;
}
