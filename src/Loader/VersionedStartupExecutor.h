#pragma once
#include "Patches/GameVersion/VersionedPlan.h"
#include "Platform/Win32/Hooking/HookRegistry.h"
#include "Diagnostics/FatalProcess.h"
namespace gc::loader {
enum class StartupInstallStage { image_binding, operation, exception };
struct StartupInstallError final {
    StartupInstallStage stage{};
    game_version::PlanContext context;
    std::optional<game_version::VersionedOperation> operation;
    std::uintptr_t address{};
    std::optional<runtime_image::RuntimeImageError> memory;
    std::optional<hooking::HookError> hook;
};
[[nodiscard]] std::expected<void, StartupInstallError> InstallApprovedVersionedPlan(
    const game_version::ApprovedVersionedPlan&, const runtime_image::RuntimeImage&,
    hooking::HookRegistry&) noexcept;
[[nodiscard]] diagnostics::FatalProcessReport FormatStartupInstallError(const StartupInstallError&);
}
