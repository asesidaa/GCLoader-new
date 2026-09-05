#include "Patches/RuntimeImage/VtableSlotHook.h"
#include <deque>
#include <mutex>
#include <optional>

namespace gc::runtime_image {
namespace {
struct SlotRecord final {
    VtableSlotHook hook;
    std::uintptr_t address{};
    bool installed{};
    std::optional<RuntimeImageError> failure;
};
struct SlotRegistry final {
    std::mutex mutex;
    std::deque<SlotRecord> records;
};
SlotRegistry& ProcessLifetimeSlots() {
    // Retain identity and outcome for process diagnostics. No detach reversal.
    static auto* registry = new SlotRegistry;
    return *registry;
}
}
std::expected<void, RuntimeImageError> PublishVtableSlotOriginal(
    const RuntimeImage& image, const VtableSlotHook& hook) noexcept {
    static_assert(sizeof(void*) == 4);
    if (!hook.expected_original || !hook.replacement || !hook.original.storage || !hook.original.publish)
        return std::unexpected(RuntimeImageError{.stage = MemoryStage::publish_original,
            .identity = hook.identity, .size = sizeof(void*), .win32_error = ERROR_INVALID_PARAMETER});
    const auto address = image.Resolve(hook.identity, sizeof(void*));
    if (!address) return std::unexpected(address.error());
    if (*address % alignof(void*) != 0)
        return std::unexpected(RuntimeImageError{.stage = MemoryStage::publish_original,
            .identity = hook.identity, .address = *address, .size = sizeof(void*),
            .win32_error = ERROR_INVALID_ADDRESS});
    hook.original.publish(hook.original.storage, hook.expected_original);
    return {};
}
std::expected<void, RuntimeImageError> InstallVtableSlotHook(
    const RuntimeImage& image, const VtableSlotHook& hook) noexcept {
    if (const auto published = PublishVtableSlotOriginal(image, hook); !published) return published;
    const auto address = image.Resolve(hook.identity, sizeof(void*));
    if (!address) return std::unexpected(address.error());
    try {
        auto& registry = ProcessLifetimeSlots();
        const std::lock_guard lock{registry.mutex};
        for (const auto& record : registry.records)
            if (record.address == *address)
                return std::unexpected(RuntimeImageError{.stage = MemoryStage::register_vtable_slot,
                    .identity = hook.identity, .address = *address, .size = sizeof(void*),
                    .win32_error = ERROR_ALREADY_EXISTS});
        auto& record = registry.records.emplace_back(SlotRecord{hook, *address});
        // Allocate the lifetime record before mutation; exact protection, CAS,
        // restoration and read-back remain RuntimeImage's responsibility.
        const auto result = image.ExchangePointer(hook.identity, hook.expected_original, hook.replacement);
        record.installed = result.has_value();
        if (!result) record.failure = result.error();
        return result;
    } catch (...) {
        return std::unexpected(RuntimeImageError{.stage = MemoryStage::register_vtable_slot,
            .identity = hook.identity, .address = *address, .size = sizeof(void*),
            .win32_error = ERROR_NOT_ENOUGH_MEMORY});
    }
}
}
