#include "Loader/NonVersionedHookPlan.h"
#include "Loader/GameWin32HookComposition.h"
#include "Audio/AudioPatch.h"
#include "Config/ConfigCompiler.h"
#include "Diagnostics/CrashDumpHandler.h"
#include "Input/Win32/RawInputRegistrationGuard.h"
#include "Locale/JapaneseLocaleCompatibility.h"
#include "Nesys/NesysServicePatch.h"
#include "Platform/Win32/Hooking/HookDiagnostics.h"
#include "Platform/Win32/Hooking/HookRegistry.h"
#include "Rfid/Feature.h"
#include <format>
#include "plog/Log.h"

namespace gc::loader {
namespace {
void Require(std::expected<void, hooking::HookError> result) noexcept {
    if (!result) hooking::AbortHookError(result.error());
}
void Install(hooking::HookPlan& plan) noexcept {
    const auto validated = plan.ResolveAndValidate();
    if (!validated) hooking::AbortHookError(validated.error());
    Require(hooking::HookRegistry::ProcessLifetime().Install(*validated));
    try { PLOG_INFO << "NonVersionedHooks: installed requests=" << plan.size(); }
    catch (...) { diagnostics::AbortProcess({}); }
}
}
void InstallGameNonVersionedHooks(
    HMODULE loader_module, const config::ValidatedConfig& settings,
    const system_path::RuntimeRoot& root) noexcept {
    try {
        hooking::HookPlan plan;
        Require(locale_compatibility::AddJapaneseLocaleHooks(plan, nesys_service::ProcessRole::Game));
        Require(crash_dump::AddCrashDumpHook(plan));
        Require(input::AddRawInputRegistrationHook(plan));
        const auto rfid = rfid::AddRfidHooks(plan, settings.rfid());
        if (!rfid) {
            if (rfid.error().stage == rfid::FeatureFailureStage::hook_plan)
                hooking::AbortHookError(rfid.error().hook);
            diagnostics::AbortProcess({
                std::format("RFID: state preparation failed stage={} win32_error={}",
                    static_cast<unsigned>(rfid.error().stage), rfid.error().win32_error),
                L"GCLoader could not prepare RFID and storage state. Check loader-log.txt.",
                L"GCLoader feature setup error"});
        }
        Require(AddGameWin32Hooks(plan, root,
            settings.rfid().testmode_storage_redirect_enabled(), **rfid));
        Require(nesys_service::AddNesysHooks(plan, loader_module,
            nesys_service::ProcessRole::Game, settings.nesys()));
        Require(audio::AddAudioHooks(plan, settings.audio()));
        Install(plan);
    } catch (...) { diagnostics::AbortProcess({}); }
}
void InstallNesysNonVersionedHooks(
    HMODULE loader_module, nesys_service::NesysSettings settings) noexcept {
    try {
        hooking::HookPlan plan;
        Require(locale_compatibility::AddJapaneseLocaleHooks(plan, nesys_service::ProcessRole::Service));
        Require(nesys_service::AddNesysHooks(plan, loader_module,
            nesys_service::ProcessRole::Service, std::move(settings)));
        Install(plan);
        nesys_service::InstallPendingNesysPing();
    } catch (...) { diagnostics::AbortProcess({}); }
}
}
