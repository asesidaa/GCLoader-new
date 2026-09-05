#pragma once
#include "Platform/Win32/Hooking/HookError.h"
#include <safetyhook.hpp>
#include <expected>
#include <span>
#include <type_traits>
#include <variant>
#include <vector>

namespace gc::hooking {
struct OriginalPublisher final {
    void* storage{};
    bool (*publish)(void*, const safetyhook::InlineHook&) noexcept{};
    template <class Function>
    [[nodiscard]] static OriginalPublisher To(Function* slot) noexcept {
        static_assert(std::is_pointer_v<Function> && std::is_function_v<std::remove_pointer_t<Function>>);
        return {slot, [](void* storage, const safetyhook::InlineHook& hook) noexcept {
            const auto original = hook.original<Function>();
            if (!storage || !original) return false;
            *static_cast<Function*>(storage) = original;
            return true;
        }};
    }
};
struct InlineDetour final {
    void* detour{};
    OriginalPublisher original;
    bool publishes_original{true};
};
struct MidDetour final { safetyhook::MidHookFn callback{}; };
using HookPayload = std::variant<InlineDetour, MidDetour>;
struct HookRequest final {
    HookIdentity identity;
    std::variant<ExportTarget, std::uintptr_t> target;
    HookPayload payload;
    HookSharing sharing{HookSharing::exclusive};
    std::string_view dispatcher;
};
struct ResolvedHookRequest final {
    ResolvedHookTarget target;
    HookPayload payload;
    HookSharing sharing{HookSharing::exclusive};
    std::string_view dispatcher;
};
class HookPlan;
class ValidatedHookPlan final {
public:
    [[nodiscard]] std::span<const ResolvedHookRequest> requests() const noexcept { return requests_; }
private:
    friend class HookPlan;
    explicit ValidatedHookPlan(std::vector<ResolvedHookRequest> requests) : requests_(std::move(requests)) {}
    std::vector<ResolvedHookRequest> requests_;
};
class HookPlan final {
public:
    [[nodiscard]] std::size_t size() const noexcept { return requests_.size(); }
    template <class Function>
    [[nodiscard]] std::expected<void, HookError> AddInlineReplacementExport(
        HookIdentity identity, ExportTarget target, Function detour) noexcept {
        static_assert(std::is_pointer_v<Function> && std::is_function_v<std::remove_pointer_t<Function>>);
        return Add({identity, target, InlineDetour{reinterpret_cast<void*>(detour), {}, false}});
    }
    template <class Detour, class Function>
    [[nodiscard]] std::expected<void, HookError> AddInlineExport(
        HookIdentity identity, ExportTarget target, Detour detour, Function* original,
        HookSharing sharing = HookSharing::exclusive, std::string_view dispatcher = {}) noexcept {
        static_assert(std::is_convertible_v<Detour, Function>);
        return Add({identity, target,
            InlineDetour{reinterpret_cast<void*>(static_cast<Function>(detour)), OriginalPublisher::To(original)},
            sharing, dispatcher});
    }
    [[nodiscard]] std::expected<void, HookError> AddInlineAddress(
        HookIdentity identity, std::uintptr_t address, void* detour, OriginalPublisher original,
        HookSharing sharing = HookSharing::exclusive, std::string_view dispatcher = {}) noexcept;
    [[nodiscard]] std::expected<void, HookError> AddMidAddress(
        HookIdentity identity, std::uintptr_t address, safetyhook::MidHookFn callback,
        HookSharing sharing = HookSharing::exclusive, std::string_view dispatcher = {}) noexcept;
    [[nodiscard]] std::expected<ValidatedHookPlan, HookError> ResolveAndValidate() const noexcept;
private:
    [[nodiscard]] std::expected<void, HookError> Add(HookRequest) noexcept;
    std::vector<HookRequest> requests_;
};
[[nodiscard]] bool CanShare(const ResolvedHookRequest&, const ResolvedHookRequest&) noexcept;
[[nodiscard]] HookError ErrorFor(HookStage, const ResolvedHookRequest&) noexcept;
}
