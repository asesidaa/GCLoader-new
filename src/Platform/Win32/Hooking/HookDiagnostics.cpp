#include "Platform/Win32/Hooking/HookDiagnostics.h"
#include <format>

namespace gc::hooking {
const char* HookStageName(HookStage stage) noexcept {
    switch (stage) {
    case HookStage::invalid_plan: return "invalid_plan";
    case HookStage::resolve_module: return "resolve_module";
    case HookStage::resolve_export: return "resolve_export";
    case HookStage::collision: return "collision";
    case HookStage::create: return "create";
    case HookStage::publish_original: return "publish_original";
    case HookStage::enable: return "enable";
    }
    return "unknown";
}
diagnostics::FatalProcessReport FormatHookError(const HookError& error) {
    auto log = std::format(
        "HookRegistry: failed stage={} feature={} site={} address=0x{:08X} "
        "win32_error={} safetyhook_error={}",
        HookStageName(error.stage), error.identity.feature, error.identity.site,
        error.address, error.win32_error, error.safetyhook_error);
    if (error.export_target) {
        // Module/export identifiers in the compiled plan are ASCII Windows names.
        std::string module;
        for (const auto value : error.export_target->module)
            module.push_back(value <= 0x7F ? static_cast<char>(value) : '?');
        log += std::format(" module={} export={}", module, error.export_target->name);
    }
    if (!error.collision_peer.feature.empty())
        log += std::format(" peer_feature={} peer_site={}",
            error.collision_peer.feature, error.collision_peer.site);
    if (error.inline_error) log += std::format(" inline_error={}", *error.inline_error);
    return {std::move(log),
        L"GCLoader could not install a required hook. Check the process loader log "
        L"for the feature, export or address, and failure details.",
        L"GCLoader hook setup error"};
}
[[noreturn]] void AbortHookError(const HookError& error) noexcept {
    try { diagnostics::AbortProcess(FormatHookError(error)); }
    catch (...) { diagnostics::AbortProcess({}); }
}
}
