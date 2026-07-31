#pragma once

#include <safetyhook.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>

namespace gc::renderer_device_loss {

inline constexpr std::uintptr_t kPreferredImageBase = 0x00400000U;
inline constexpr std::uint32_t kVertexBufferResultRva = 0x000E79F7U;
inline constexpr std::uint32_t kRendererInitializerEpilogueRva =
    0x000E7EE9U;
inline constexpr std::size_t kRendererInitializedOffset = 0x484U;

inline constexpr std::array<std::byte, 7>
kVertexBufferResultPattern{
    std::byte{0x85},
    std::byte{0xC0},
    std::byte{0x7C},
    std::byte{0x59},
    std::byte{0x8B},
    std::byte{0x4F},
    std::byte{0x0C},
};

inline constexpr std::array<std::byte, 7> kRendererEpiloguePattern{
    std::byte{0x5F},
    std::byte{0x5E},
    std::byte{0x5B},
    std::byte{0x8B},
    std::byte{0xE5},
    std::byte{0x5D},
    std::byte{0xC3},
};

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

enum class RendererContractSite {
    None,
    VertexBufferResult,
    InitializerEpilogue,
};

enum class RendererInstallStage {
    None,
    InvalidActions,
    UnexpectedImageBase,
    PreflightRead,
    PreflightMismatch,
    HookInstall,
};

struct RendererInstallError {
    RendererInstallStage stage{RendererInstallStage::None};
    RendererContractSite site{RendererContractSite::None};
};

struct RendererInstallActions {
    void* context{};
    bool (*read)(
        void*,
        std::uintptr_t,
        std::span<std::byte>) noexcept{};
    bool (*install_hook)(void*, std::uintptr_t) noexcept{};
    void (*reset_hook)(void*) noexcept{};
};

[[nodiscard]] std::expected<void, RendererInstallError>
InstallRendererDeviceLossPatch(
    std::uintptr_t image_base,
    RendererInstallActions actions) noexcept;

[[nodiscard]] bool RendererDeviceLossPatchInit() noexcept;

} // namespace gc::renderer_device_loss
