#pragma once
#include "Patches/RuntimeImage/RuntimeImage.h"
#include <type_traits>

namespace gc::runtime_image {
struct VtableOriginalPublisher final {
    void* storage{};
    void (*publish)(void*, void*) noexcept{};
    template <class Function>
    [[nodiscard]] static VtableOriginalPublisher To(Function* slot) noexcept {
        static_assert(std::is_pointer_v<Function> && std::is_function_v<std::remove_pointer_t<Function>>);
        return {slot, [](void* storage, void* original) noexcept {
            *static_cast<Function*>(storage) = reinterpret_cast<Function>(original);
        }};
    }
};
struct VtableSlotHook final {
    SiteIdentity identity;
    void* expected_original{};
    void* replacement{};
    VtableOriginalPublisher original;
};
// Used by composition to publish all aliases before the first shared slot enables.
[[nodiscard]] std::expected<void, RuntimeImageError> PublishVtableSlotOriginal(
    const RuntimeImage&, const VtableSlotHook&) noexcept;
[[nodiscard]] std::expected<void, RuntimeImageError> InstallVtableSlotHook(
    const RuntimeImage&, const VtableSlotHook&) noexcept;
}
