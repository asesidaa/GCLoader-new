#pragma once
#include "Platform/Win32/Hooking/HookPlan.h"
#include <deque>
#include <mutex>

namespace gc::hooking {
class HookRegistry final {
public:
    [[nodiscard]] static HookRegistry& ProcessLifetime() noexcept;
    [[nodiscard]] std::expected<void, HookError> Install(const ValidatedHookPlan&) noexcept;
    [[nodiscard]] bool IsInstalled(HookIdentity) noexcept;
    HookRegistry(const HookRegistry&) = delete;
    HookRegistry& operator=(const HookRegistry&) = delete;
private:
    HookRegistry() = default;
    struct Record final {
        ResolvedHookRequest request;
        std::variant<std::monostate, safetyhook::InlineHook, safetyhook::MidHook> hook;
        bool enabled{};
    };
    std::mutex mutex_;
    std::deque<Record> records_;
    struct Registration final {
        HookIdentity identity;
        Record* owner{};
    };
    std::vector<Registration> registrations_;
};
}
