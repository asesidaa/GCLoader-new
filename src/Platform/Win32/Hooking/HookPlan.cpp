#include "Platform/Win32/Hooking/HookPlan.h"
#include <string>

namespace gc::hooking {
namespace {
bool Valid(const HookRequest& request) noexcept {
    if (request.identity.feature.empty() || request.identity.site.empty() ||
        (request.sharing != HookSharing::exclusive && request.sharing != HookSharing::named_dispatcher) ||
        (request.sharing == HookSharing::named_dispatcher && request.dispatcher.empty()) ||
        (request.sharing == HookSharing::exclusive && !request.dispatcher.empty())) return false;
    if (const auto* target = std::get_if<ExportTarget>(&request.target)) {
        if (target->module.empty() || target->name.empty() ||
            target->module.find(L'\0') != std::wstring_view::npos ||
            target->name.find('\0') != std::string_view::npos) return false;
    } else if (!std::get<std::uintptr_t>(request.target)) return false;
    if (const auto* detour = std::get_if<InlineDetour>(&request.payload))
        return detour->detour && (detour->publishes_original
            ? detour->original.storage && detour->original.publish
            : !detour->original.storage && !detour->original.publish);
    return std::get<MidDetour>(request.payload).callback != nullptr;
}
}
HookError ErrorFor(HookStage stage, const ResolvedHookRequest& request) noexcept {
    return {.stage = stage, .identity = request.target.identity, .address = request.target.address,
        .export_target = request.target.export_target};
}
bool CanShare(const ResolvedHookRequest& left, const ResolvedHookRequest& right) noexcept {
    if (left.sharing != HookSharing::named_dispatcher || right.sharing != HookSharing::named_dispatcher ||
        left.dispatcher.empty() || left.dispatcher != right.dispatcher || left.payload.index() != right.payload.index())
        return false;
    if (const auto* inline_left = std::get_if<InlineDetour>(&left.payload))
        return inline_left->detour == std::get<InlineDetour>(right.payload).detour;
    return std::get<MidDetour>(left.payload).callback == std::get<MidDetour>(right.payload).callback;
}
std::expected<void, HookError> HookPlan::Add(HookRequest request) noexcept {
    auto error = HookError{.stage = HookStage::invalid_plan, .identity = request.identity};
    if (const auto* target = std::get_if<ExportTarget>(&request.target)) error.export_target = *target;
    else error.address = std::get<std::uintptr_t>(request.target);
    if (!Valid(request)) return std::unexpected(error);
    for (const auto& previous : requests_)
        if (previous.identity == request.identity) return std::unexpected(error);
    try { requests_.push_back(request); return {}; }
    catch (...) { error.win32_error = ERROR_NOT_ENOUGH_MEMORY; return std::unexpected(error); }
}
std::expected<void, HookError> HookPlan::AddInlineAddress(
    HookIdentity identity, std::uintptr_t address, void* detour, OriginalPublisher original,
    HookSharing sharing, std::string_view dispatcher) noexcept {
    return Add({identity, address, InlineDetour{detour, original}, sharing, dispatcher});
}
std::expected<void, HookError> HookPlan::AddMidAddress(
    HookIdentity identity, std::uintptr_t address, safetyhook::MidHookFn callback,
    HookSharing sharing, std::string_view dispatcher) noexcept {
    return Add({identity, address, MidDetour{callback}, sharing, dispatcher});
}
std::expected<ValidatedHookPlan, HookError> HookPlan::ResolveAndValidate() const noexcept {
    HookError current{.stage = HookStage::invalid_plan};
    try {
        std::vector<ResolvedHookRequest> resolved;
        resolved.reserve(requests_.size());
        for (const auto& request : requests_) {
            ResolvedHookRequest result{{request.identity}, request.payload, request.sharing, request.dispatcher};
            current = ErrorFor(HookStage::invalid_plan, result);
            if (const auto* target = std::get_if<ExportTarget>(&request.target)) {
                result.target.export_target = *target;
                current.export_target = *target;
                const auto module_name = std::wstring{target->module};
                const auto export_name = std::string{target->name};
                const auto module = GetModuleHandleW(module_name.c_str());
                if (!module) {
                    current.win32_error = GetLastError();
                    current.stage = HookStage::resolve_module;
                    return std::unexpected(current);
                }
                const auto procedure = GetProcAddress(module, export_name.c_str());
                if (!procedure) {
                    current.win32_error = GetLastError();
                    current.stage = HookStage::resolve_export;
                    return std::unexpected(current);
                }
                result.target.address = reinterpret_cast<std::uintptr_t>(procedure);
            } else {
                result.target.address = std::get<std::uintptr_t>(request.target);
            }
            for (const auto& previous : resolved) {
                if (previous.target.address != result.target.address || CanShare(previous, result)) continue;
                auto error = ErrorFor(HookStage::collision, result);
                error.collision_peer = previous.target.identity;
                return std::unexpected(error);
            }
            resolved.push_back(result);
        }
        return ValidatedHookPlan{std::move(resolved)};
    } catch (...) {
        current.win32_error = ERROR_NOT_ENOUGH_MEMORY;
        return std::unexpected(current);
    }
}
}
