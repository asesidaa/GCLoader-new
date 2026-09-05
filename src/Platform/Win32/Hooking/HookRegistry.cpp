#include "Platform/Win32/Hooking/HookRegistry.h"
#include "Platform/Win32/Hooking/HookDiagnostics.h"

namespace gc::hooking {
namespace {
HookError LibraryError(HookStage stage, const ResolvedHookRequest& request,
                       const safetyhook::InlineHook::Error& cause) noexcept {
    auto error = ErrorFor(stage, request);
    error.safetyhook_error = static_cast<std::uint32_t>(cause.type);
    return error;
}
HookError LibraryError(HookStage stage, const ResolvedHookRequest& request,
                       const safetyhook::MidHook::Error& cause) noexcept {
    auto error = ErrorFor(stage, request);
    error.safetyhook_error = static_cast<std::uint32_t>(cause.type);
    if (cause.type == safetyhook::MidHook::Error::BAD_INLINE_HOOK)
        error.inline_error = static_cast<std::uint32_t>(cause.inline_hook_error.type);
    return error;
}
}
HookRegistry& HookRegistry::ProcessLifetime() noexcept {
    try {
        // Live DLL unload is unsupported; no hook is destroyed during detach.
        static auto* registry = new HookRegistry;
        return *registry;
    } catch (...) {
        AbortHookError({.stage = HookStage::create, .win32_error = ERROR_NOT_ENOUGH_MEMORY});
    }
}
std::expected<void, HookError> HookRegistry::Install(const ValidatedHookPlan& plan) noexcept {
    HookError current{.stage = HookStage::invalid_plan};
    try {
        std::lock_guard lock(mutex_);
        // Check the whole request set against existing ownership before mutation.
        for (const auto& request : plan.requests()) {
            for (const auto& registration : registrations_) {
                if (registration.identity == request.target.identity) {
                    auto error = ErrorFor(HookStage::collision, request);
                    error.collision_peer = registration.identity;
                    return std::unexpected(error);
                }
            }
            for (const auto& record : records_) {
                if (record.request.target.address == request.target.address && !CanShare(record.request, request)) {
                    auto error = ErrorFor(HookStage::collision, request);
                    error.collision_peer = record.request.target.identity;
                    return std::unexpected(error);
                }
            }
        }
        for (const auto& request : plan.requests()) {
            current = ErrorFor(HookStage::create, request);
            Record* shared{};
            for (auto& record : records_)
                if (record.request.target.address == request.target.address) { shared = &record; break; }
            if (shared) {
                registrations_.push_back({request.target.identity, shared});
                if (const auto* detour = std::get_if<InlineDetour>(&request.payload);
                    detour && detour->publishes_original) {
                    current.stage = HookStage::publish_original;
                    if (!detour->original.publish(detour->original.storage,
                            std::get<safetyhook::InlineHook>(shared->hook))) return std::unexpected(current);
                }
                continue;
            }
            // Allocate the record before creating a disabled candidate. Moving a
            // successful candidate into it cannot invalidate an original slot.
            records_.push_back({request, std::monostate{}});
            auto& record = records_.back();
            registrations_.push_back({request.target.identity, &record});
            if (const auto* detour = std::get_if<InlineDetour>(&request.payload)) {
                auto created = safetyhook::InlineHook::create(
                    reinterpret_cast<void*>(request.target.address), detour->detour,
                    safetyhook::InlineHook::StartDisabled);
                if (!created) return std::unexpected(LibraryError(HookStage::create, request, created.error()));
                auto& hook = record.hook.emplace<safetyhook::InlineHook>(std::move(*created));
                current.stage = HookStage::publish_original;
                if (detour->publishes_original &&
                    !detour->original.publish(detour->original.storage, hook)) return std::unexpected(current);
                current.stage = HookStage::enable;
                const auto enabled = hook.enable();
                if (!enabled) return std::unexpected(LibraryError(HookStage::enable, request, enabled.error()));
            } else {
                auto created = safetyhook::MidHook::create(
                    reinterpret_cast<void*>(request.target.address), std::get<MidDetour>(request.payload).callback,
                    safetyhook::MidHook::StartDisabled);
                if (!created) return std::unexpected(LibraryError(HookStage::create, request, created.error()));
                auto& hook = record.hook.emplace<safetyhook::MidHook>(std::move(*created));
                current.stage = HookStage::enable;
                const auto enabled = hook.enable();
                if (!enabled) return std::unexpected(LibraryError(HookStage::enable, request, enabled.error()));
            }
            record.enabled = true;
        }
        return {};
    } catch (...) {
        current.win32_error = ERROR_NOT_ENOUGH_MEMORY;
        return std::unexpected(current);
    }
}
bool HookRegistry::IsInstalled(HookIdentity identity) noexcept {
    try {
        std::lock_guard lock(mutex_);
        for (const auto& registration : registrations_)
            if (registration.identity == identity) return registration.owner->enabled;
    } catch (...) {
        AbortHookError({.stage = HookStage::invalid_plan, .identity = identity,
            .win32_error = ERROR_UNHANDLED_EXCEPTION});
    }
    return false;
}
}
