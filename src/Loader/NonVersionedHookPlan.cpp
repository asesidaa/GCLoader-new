#include <WinSock2.h>
#include "Loader/NonVersionedHookPlan.h"
#include "Loader/GameWin32HookComposition.h"
#include "Audio/AudioFeature.h"
#include "Diagnostics/CrashDumpHandler.h"
#include "Input/Win32/RawInputRegistrationGuard.h"
#include "Locale/JapaneseLocaleCompatibility.h"
#include "Nesys/NesysServicePatch.h"
#include "Rfid/Feature.h"

namespace gc::loader {
std::expected<hooking::ValidatedHookPlan, StartupError> PrepareGameNonVersionedHooks(
    HMODULE loader_module, const config::ValidatedConfig& settings,
    const system_path::RuntimeRoot& root, const audio::PreparedAudioFeature& audio) noexcept {
    const auto failure = [](const hooking::HookError& error) {
        return std::unexpected(StartupError{.hook = error});
    };
    try {
        hooking::HookPlan plan;
        if (auto r = locale_compatibility::AddJapaneseLocaleHooks(plan, nesys_service::ProcessRole::Game); !r) return failure(r.error());
        if (auto r = crash_dump::AddCrashDumpHook(plan); !r) return failure(r.error());
        if (auto r = input::AddRawInputRegistrationHook(plan); !r) return failure(r.error());
        const auto rfid = rfid::AddRfidHooks(plan, settings.rfid());
        if (!rfid) {
            if (rfid.error().stage == rfid::FeatureFailureStage::hook_plan) return failure(rfid.error().hook);
            return std::unexpected(StartupError{.stage = StartupStage::feature,
                .feature = "RFID", .operation = "state_preparation", .win32_error = rfid.error().win32_error});
        }
        if (auto r = AddGameWin32Hooks(plan, root, settings.rfid().testmode_storage_redirect_enabled(), **rfid); !r)
            return std::unexpected(std::move(r.error()));
        if (auto r = nesys_service::AddNesysHooks(plan, loader_module, nesys_service::ProcessRole::Game, settings.nesys()); !r) return failure(r.error());
        if (auto r = audio.AddExportHooks(plan); !r) return failure(r.error());
        auto exports = plan.ResolveAndValidate();
        if (!exports) return failure(exports.error());
        return std::move(*exports);
    } catch (...) { return std::unexpected(StartupError{.stage = StartupStage::exception}); }
}
}
