#include <WinSock2.h>
#include "Loader/NesysStartup.h"
#include "Loader/NesysVersionedStartupPlan.h"
#include "Locale/JapaneseLocaleCompatibility.h"
#include "Nesys/NesysServicePatch.h"
#include "Patches/GameVersion/VersionedPlanDiagnostics.h"
#include <plog/Log.h>

namespace gc::loader {
namespace {
std::expected<hooking::ValidatedHookPlan, StartupError> PrepareNesysNonVersionedHooks(
    HMODULE loader_module, nesys_service::NesysSettings settings) noexcept {
    constexpr auto role = nesys_service::ProcessRole::Service;
    const auto failure = [](const hooking::HookError& error) {
        return std::unexpected(StartupError{.role = role, .hook = error});
    };
    try {
        hooking::HookPlan plan;
        if (auto r = locale_compatibility::AddJapaneseLocaleHooks(plan, role); !r) return failure(r.error());
        if (auto r = nesys_service::AddNesysHooks(plan, loader_module, role, std::move(settings)); !r) return failure(r.error());
        auto exports = plan.ResolveAndValidate();
        if (!exports) return failure(exports.error());
        return std::move(*exports);
    } catch (...) { return std::unexpected(StartupError{.role = role, .stage = StartupStage::exception}); }
}
}
std::expected<void, StartupError> StartNesys(HMODULE loader_module, NesysProcessConfiguration configuration) noexcept {
    constexpr auto role = nesys_service::ProcessRole::Service;
    bool mutation_started{};
    try {
        auto versioned = PrepareNesysVersionedStartup(GetModuleHandleW(nullptr), configuration.nesys);
        if (!versioned) return std::unexpected(StartupError{.role = role, .versioned = std::move(versioned.error())});
        auto exports = PrepareNesysNonVersionedHooks(loader_module, std::move(configuration.nesys));
        if (!exports) return std::unexpected(std::move(exports.error()));
        auto& registry = hooking::HookRegistry::ProcessLifetime();
        mutation_started = true;
        if (*versioned) {
            const auto& startup = **versioned;
            if (auto r = InstallApprovedVersionedPlan(startup.plan, startup.image, registry); !r)
                AbortForStartupError({.role = role, .installation = std::move(r.error())});
            PLOG_INFO << "NesysPing: installed " << game_version::FormatPlanContext(startup.plan.context());
        }
        if (auto r = registry.Install(*exports); !r) AbortForStartupError({.role = role, .hook = r.error()});
        PLOG_INFO << "NonVersionedHooks: installed requests=" << exports->requests().size();
        PLOG_INFO << "NesysServicePatch: service role skipping game-only RFID/input/framerate initialization";
        return {};
    } catch (...) {
        StartupError error{.role = role, .stage = StartupStage::exception, .feature = "NesysStartup"};
        if (mutation_started) AbortForStartupError(error);
        return std::unexpected(std::move(error));
    }
}
}
