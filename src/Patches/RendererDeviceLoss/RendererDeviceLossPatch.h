#pragma once

#include <safetyhook.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>

namespace gc::renderer_device_loss {

inline constexpr std::uintptr_t kPreferredImageBase = 0x00400000U;
inline constexpr std::uint32_t kDeviceLostTailRva = 0x000E67D8U;
inline constexpr std::uint32_t kVertexBufferResultRva = 0x000E79F7U;
inline constexpr std::uint32_t kRendererInitializerEpilogueRva =
    0x000E7EE9U;
inline constexpr std::uint32_t kVertexBufferLockGuardRva =
    0x000E5578U;
inline constexpr std::uint32_t kVertexBufferLockFailureRva =
    0x000E55E2U;
inline constexpr std::size_t kRendererInitializedOffset = 0x484U;
inline constexpr std::size_t kRendererIndexBufferHolderOffset = 0x778U;
inline constexpr std::size_t kVertexBufferLockOutputStackOffset = 0x14U;

inline constexpr std::array<std::byte, 12> kDeviceLostTailPattern{
    std::byte{0x89},
    std::byte{0xBE},
    std::byte{0x18},
    std::byte{0x01},
    std::byte{0x00},
    std::byte{0x00},
    std::byte{0x89},
    std::byte{0xBE},
    std::byte{0x1C},
    std::byte{0x01},
    std::byte{0x00},
    std::byte{0x00},
};

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

inline constexpr std::array<std::byte, 9>
kVertexBufferLockGuardPattern{
    std::byte{0x3B},
    std::byte{0xF9},
    std::byte{0x72},
    std::byte{0x05},
    std::byte{0xE8},
    std::byte{0x66},
    std::byte{0x00},
    std::byte{0x02},
    std::byte{0x00},
};

inline constexpr std::array<std::byte, 12>
kVertexBufferLockFailurePattern{
    std::byte{0x5F},
    std::byte{0x5E},
    std::byte{0x89},
    std::byte{0x18},
    std::byte{0x89},
    std::byte{0x58},
    std::byte{0x04},
    std::byte{0x5B},
    std::byte{0x59},
    std::byte{0xC2},
    std::byte{0x08},
    std::byte{0x00},
};

struct RendererInitializedWriter {
    void* context{};
    bool (*clear_initialized)(
        void*,
        std::uintptr_t,
        std::size_t) noexcept{};
};

struct RendererDeviceLostActions {
    void* context{};
    bool (*clear_initialized)(
        void*,
        std::uintptr_t,
        std::size_t) noexcept{};
    bool (*detach_index_buffer)(
        void*,
        std::uintptr_t,
        std::size_t,
        std::uintptr_t&) noexcept{};
    bool (*release_index_buffer)(
        void*,
        std::uintptr_t) noexcept{};
};

[[nodiscard]] bool ApplyRendererDeviceLostCleanup(
    safetyhook::Context& context,
    RendererDeviceLostActions actions) noexcept;

[[nodiscard]] bool ApplyRendererDeviceLossRetry(
    safetyhook::Context& context,
    std::uintptr_t image_base,
    RendererInitializedWriter writer) noexcept;

struct RendererStackPointerReader {
    void* context{};
    bool (*read_pointer)(
        void*,
        std::uintptr_t,
        std::uintptr_t&) noexcept{};
};

[[nodiscard]] bool ApplyRendererDeviceLossDrawSkip(
    safetyhook::Context& context,
    std::uintptr_t image_base,
    RendererStackPointerReader reader) noexcept;

enum class RendererContractSite {
    None,
    DeviceLostTail,
    VertexBufferResult,
    InitializerEpilogue,
    VertexBufferLockGuard,
    VertexBufferLockFailure,
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
    bool (*install_hook)(
        void*,
        RendererContractSite,
        std::uintptr_t) noexcept{};
    void (*reset_hook)(void*) noexcept{};
};

[[nodiscard]] std::expected<void, RendererInstallError>
InstallRendererDeviceLossPatch(
    std::uintptr_t image_base,
    RendererInstallActions actions) noexcept;

[[nodiscard]] bool RendererDeviceLossPatchInit() noexcept;

} // namespace gc::renderer_device_loss
