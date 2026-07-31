#pragma once

#include <safetyhook.hpp>

#include <cstddef>
#include <cstdint>

namespace gc::renderer_device_loss {

inline constexpr std::uintptr_t kPreferredImageBase = 0x00400000U;
inline constexpr std::uint32_t kVertexBufferResultRva = 0x000E79F7U;
inline constexpr std::uint32_t kRendererInitializerEpilogueRva =
    0x000E7EE9U;
inline constexpr std::size_t kRendererInitializedOffset = 0x484U;

struct RendererInitializedWriter {
    void* context{};
    bool (*clear_initialized)(
        void*,
        std::uintptr_t,
        std::size_t) noexcept{};
};

[[nodiscard]] bool ApplyRendererDeviceLossRetry(
    safetyhook::Context& context,
    std::uintptr_t image_base,
    RendererInitializedWriter writer) noexcept;

} // namespace gc::renderer_device_loss
