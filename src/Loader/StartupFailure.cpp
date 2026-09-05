#include "Loader/StartupFailure.h"
#include "Patches/GameVersion/VersionedPlanDiagnostics.h"
#include "Platform/Win32/Hooking/HookDiagnostics.h"
#include "Platform/Win32/Utf.h"
#include <format>

namespace gc::loader {
diagnostics::FatalProcessReport FormatStartupError(const StartupError& error) {
    if (error.installation) return FormatStartupInstallError(*error.installation);
    if (error.hook) return hooking::FormatHookError(*error.hook);
    if (error.versioned && error.versioned->plan)
        return game_version::FormatPlanError(*error.versioned->plan);
    const auto role = nesys_service::ProcessRoleName(error.role);
    diagnostics::FatalProcessReport report{
        std::format("Startup failed role={} stage={} feature={} operation={} win32_error={} detail={}",
            role, static_cast<unsigned>(error.stage), error.feature, error.operation,
            error.win32_error, error.detail),
        L"GCLoader could not initialize this process. Check its loader log for details.",
        L"GCLoader startup error"};
    if (error.configuration) {
        report.log = std::format("Configuration startup failed role={} stage={} error={}",
            role, StartupConfigurationStageName(error.configuration->stage), error.configuration->message);
        report.modal = platform::win32::Utf8ToWide(error.configuration->message)
            .value_or(L"GCLoader could not load or validate config.toml. Check the loader log.");
        report.title = L"GCLoader configuration error";
    } else if (error.versioned && error.versioned->detection) {
        const auto& detection = *error.versioned->detection;
        report.log = std::format(
            "Build detection failed role={} stage={} identity_stage={} win32_error={} cng_status={}",
            role, static_cast<unsigned>(detection.stage),
            detection.identity ? static_cast<unsigned>(detection.identity->stage) : 0,
            detection.identity ? detection.identity->win32_error : 0,
            detection.identity ? detection.identity->cng_status : 0);
        report.modal = L"GCLoader could not identify this executable. Check the process loader log.";
        report.title = L"GCLoader executable validation error";
    } else if (error.versioned && error.versioned->memory) {
        const auto& memory = *error.versioned->memory;
        report.log = std::format(
            "Image validation failed role={} stage={} feature={} site={} rva=0x{:08X} address=0x{:08X} size={} win32_error={} expected={} observed={}",
            role, runtime_image::MemoryStageName(memory.stage), memory.identity.feature, memory.identity.site,
            memory.identity.rva, memory.address, memory.size, memory.win32_error,
            game_version::FormatContractBytes(memory.expected), game_version::FormatContractBytes(memory.observed));
        report.title = L"GCLoader executable validation error";
    } else if (error.registration) {
        const auto& registration = *error.registration;
        report.log = std::format("Win32 handler registration failed role={} stage={} feature={} site={}",
            role, static_cast<unsigned>(registration.stage),
            registration.identity.feature, registration.identity.site);
        report.modal = L"GCLoader could not compose its Win32 handlers. Check loader-log.txt.";
        report.title = L"GCLoader hook setup error";
    } else if (error.stage == StartupStage::ime) {
        report.log = std::format(
            "Input IME suppression failed api=ImmDisableIME thread_selector=all win32_error={}", error.win32_error);
        report.modal = std::format(
            L"GCLoader could not disable input method editors for the game process.\n\nWindows error: {}\n\nThe game was stopped before creating its input window because an active IME could consume gameplay keys or show a composition window.",
            error.win32_error);
        report.title = L"GCLoader input method setup error";
    } else if (error.stage == StartupStage::input) {
        report.log = "Input polling configuration failed error=" + error.detail;
        report.modal = platform::win32::Utf8ToWide(error.detail)
            .value_or(L"GCLoader could not publish the validated input settings. Check loader-log.txt.");
        report.title = L"GCLoader input setup error";
    } else if (error.stage == StartupStage::role) {
        report.modal = L"GCLoader prepared configuration for the wrong process role.";
        report.title = L"GCLoader configuration error";
    }
    return report;
}
[[noreturn]] void AbortForStartupError(const StartupError& error) noexcept {
    try { diagnostics::AbortProcess(FormatStartupError(error)); }
    catch (...) { diagnostics::AbortProcess({}); }
}
}
