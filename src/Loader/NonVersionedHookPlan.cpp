#include "Loader/NonVersionedHookPlan.h"
#include "Loader/TransitionalVersionedStartup.h"
#include "Loader/NesysVersionedStartupPlan.h"
#include "Loader/VersionedStartupExecutor.h"
#include "Patches/GameVersion/VersionedPlanDiagnostics.h"
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
void Install(const hooking::ValidatedHookPlan& plan) noexcept {
    Require(hooking::HookRegistry::ProcessLifetime().Install(plan));
    try { PLOG_INFO << "NonVersionedHooks: installed requests=" << plan.requests().size(); }
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
        const auto exports = plan.ResolveAndValidate();
        if (!exports) hooking::AbortHookError(exports.error());
        // The temporary game adapter is removed at Plan09's complete cutover.
        InstallTransitionalAsioClose(settings.audio().backend());
        Install(*exports);
    } catch (...) { diagnostics::AbortProcess({}); }
}
void InstallNesysNonVersionedHooks(
    HMODULE loader_module, nesys_service::NesysSettings settings) noexcept {
    try {
        hooking::HookPlan plan;
        Require(locale_compatibility::AddJapaneseLocaleHooks(plan, nesys_service::ProcessRole::Service));
        Require(nesys_service::AddNesysHooks(plan, loader_module,
            nesys_service::ProcessRole::Service, settings));
        const auto versioned = PrepareNesysVersionedStartup(GetModuleHandleW(nullptr), settings);
        if (!versioned) {
            const auto& error = versioned.error();
            if (error.plan) diagnostics::AbortProcess(game_version::FormatPlanError(*error.plan));
            if (error.memory) diagnostics::AbortProcess({
                std::format("NESYS image validation failed stage={} win32_error={} address=0x{:08X} size={}",
                    runtime_image::MemoryStageName(error.memory->stage), error.memory->win32_error,
                    error.memory->address, error.memory->size),
                L"GCLoader could not validate the loaded NESYS image. Check the service loader log.",
                L"GCLoader NESYS validation error"});
            const auto detection = error.detection.value_or(game_version::DetectionError{});
            diagnostics::AbortProcess({
                std::format("NESYS build detection failed stage={} identity_stage={} win32_error={} cng_status={}",
                    static_cast<unsigned>(detection.stage),
                    detection.identity ? static_cast<unsigned>(detection.identity->stage) : 0,
                    detection.identity ? detection.identity->win32_error : 0,
                    detection.identity ? detection.identity->cng_status : 0),
                L"GCLoader could not identify this NESYS executable. Check the service loader log.",
                L"GCLoader NESYS validation error"});
        }
        const auto exports = plan.ResolveAndValidate();
        if (!exports) hooking::AbortHookError(exports.error());
        if (*versioned) {
            const auto& startup = **versioned;
            if (const auto installed = InstallApprovedVersionedPlan(
                    startup.plan, startup.image, hooking::HookRegistry::ProcessLifetime()); !installed)
                diagnostics::AbortProcess(FormatStartupInstallError(installed.error()));
            PLOG_INFO << "NesysPing: installed "
                << game_version::FormatPlanContext(startup.plan.context());
        }
        Install(*exports);
    } catch (...) { diagnostics::AbortProcess({}); }
}
}
