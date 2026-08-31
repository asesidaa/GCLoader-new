#pragma once

#include "Patches/RendererDeviceLoss/RendererResourceLifecycle.h"

#include <safetyhook.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>

namespace gc::renderer_device_loss {

enum class RendererResetHookSite : std::uint8_t {
    pre_reset,
    post_reset,
};

enum class RendererResetHookPairState : std::uint8_t {
    empty,
    disabled,
    active,
};

enum class RendererResetHookPairStage : std::uint8_t {
    invalid_actions,
    invalid_state,
    create_disabled,
    enable,
};

struct RendererResetHookPairError {
    RendererResetHookPairStage stage{
        RendererResetHookPairStage::invalid_actions};
    RendererResetHookSite site{RendererResetHookSite::pre_reset};
};

struct RendererResetHookPairActions {
    void* context{};
    bool (*create_disabled)(
        void*, RendererResetHookSite, std::uintptr_t) noexcept{};
    bool (*enable)(void*, RendererResetHookSite) noexcept{};
    void (*reset)(void*, RendererResetHookSite) noexcept{};
};

class RendererResetHookPair final {
public:
    RendererResetHookPair() = default;
    ~RendererResetHookPair();

    RendererResetHookPair(const RendererResetHookPair&) = delete;
    RendererResetHookPair& operator=(const RendererResetHookPair&) = delete;

    [[nodiscard]] std::expected<void, RendererResetHookPairError>
    PrepareDisabled(
        std::uintptr_t pre_reset_address,
        std::uintptr_t post_reset_address,
        RendererResetHookPairActions actions) noexcept;

    [[nodiscard]] std::expected<void, RendererResetHookPairError>
    Enable() noexcept;

    void Reset() noexcept;

    [[nodiscard]] RendererResetHookPairState state() const noexcept {
        return state_;
    }

private:
    RendererResetHookPairActions actions_{};
    RendererResetHookPairState state_{RendererResetHookPairState::empty};
    bool pre_attempted_{};
    bool post_attempted_{};
};

enum class RendererResetLifecycleStage : std::uint8_t {
    before_reset,
    after_reset,
};

struct RendererResetFailureActions {
    void* context{};
    void (*failure)(
        void*, RendererResetLifecycleStage, RendererResourceError) noexcept{};
};

inline constexpr std::uintptr_t kPreferredImageBase = 0x00400000U;
inline constexpr std::uint32_t kDeviceLostTailRva = 0x000E67D8U;
inline constexpr std::uint32_t kVertexBufferResultRva = 0x000E79F7U;
inline constexpr std::uint32_t kIndexBufferResultRva = 0x000E7A84U;
inline constexpr std::uint32_t kRendererInitializerEpilogueRva =
    0x000E7EE9U;
inline constexpr std::uint32_t kVertexBufferLockGuardRva =
    0x000E5578U;
inline constexpr std::uint32_t kVertexBufferLockFailureRva =
    0x000E55E2U;
inline constexpr std::uint32_t kDirectLockResultRva = 0x000E691EU;
inline constexpr std::uint32_t kDirectBatchCleanupRva = 0x000E6AD6U;
inline constexpr std::uint32_t kBufferedUnlockResultRva = 0x000E5662U;
inline constexpr std::uint32_t kBufferedUnlockContinuationRva =
    0x000E5679U;
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

inline constexpr std::array<std::byte, 9> kIndexBufferResultPattern{
    std::byte{0x85},
    std::byte{0xC0},
    std::byte{0x7D},
    std::byte{0x13},
    std::byte{0x68},
    std::byte{0xE4},
    std::byte{0xA5},
    std::byte{0x71},
    std::byte{0x00},
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

inline constexpr std::array<std::byte, 11> kDirectLockResultPattern{
    std::byte{0x8B},
    std::byte{0x4C},
    std::byte{0x24},
    std::byte{0x14},
    std::byte{0x51},
    std::byte{0x8B},
    std::byte{0x8E},
    std::byte{0xE4},
    std::byte{0x01},
    std::byte{0x00},
    std::byte{0x00},
};

inline constexpr std::array<std::byte, 12> kDirectBatchCleanupPattern{
    std::byte{0x8B},
    std::byte{0xB6},
    std::byte{0xE4},
    std::byte{0x01},
    std::byte{0x00},
    std::byte{0x00},
    std::byte{0x8B},
    std::byte{0x5E},
    std::byte{0x10},
    std::byte{0x39},
    std::byte{0x5E},
    std::byte{0x0C},
};

inline constexpr std::array<std::byte, 9> kBufferedUnlockResultPattern{
    std::byte{0x85},
    std::byte{0xC0},
    std::byte{0x7D},
    std::byte{0x13},
    std::byte{0x68},
    std::byte{0xE4},
    std::byte{0xA5},
    std::byte{0x71},
    std::byte{0x00},
};

inline constexpr std::array<std::byte, 12>
kBufferedUnlockContinuationPattern{
    std::byte{0x8B},
    std::byte{0x86},
    std::byte{0x80},
    std::byte{0x04},
    std::byte{0x00},
    std::byte{0x00},
    std::byte{0x8B},
    std::byte{0x8E},
    std::byte{0x44},
    std::byte{0x07},
    std::byte{0x00},
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
    const safetyhook::Context& context,
    const RendererDeviceLostActions& actions) noexcept;

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

[[nodiscard]] bool ApplyRendererDeviceLossDirectLockSkip(
    safetyhook::Context& context,
    std::uintptr_t image_base) noexcept;

[[nodiscard]] bool ApplyRendererDeviceLossUnlockCompletion(
    safetyhook::Context& context,
    std::uintptr_t image_base) noexcept;

enum class RendererContractSite {
    None,
    DeviceLostTail,
    VertexBufferResult,
    IndexBufferResult,
    InitializerEpilogue,
    VertexBufferLockGuard,
    VertexBufferLockFailure,
    DirectLockResult,
    DirectBatchCleanup,
    BufferedUnlockResult,
    BufferedUnlockContinuation,
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
    const RendererInstallActions& actions) noexcept;

[[nodiscard]] bool RendererDeviceLossPatchInit() noexcept;

[[nodiscard]] std::expected<void, RendererResetHookPairError>
RendererDeviceLossPrepareResetHooksDisabled(
    std::uintptr_t pre_reset_address,
    std::uintptr_t post_reset_address,
    RendererResetFailureActions failure_actions) noexcept;

[[nodiscard]] std::expected<void, RendererResetHookPairError>
RendererDeviceLossEnableResetHooks() noexcept;

void RendererDeviceLossResetHooks() noexcept;

[[nodiscard]] RendererResetHookPairState
RendererDeviceLossResetHookPairState() noexcept;

[[nodiscard]] std::expected<void, RendererResourceError>
RendererDeviceLossAttachResource(
    RendererResourceParticipant participant) noexcept;

void RendererDeviceLossDetachResource() noexcept;

[[nodiscard]] std::expected<void, RendererResourceError>
RendererDeviceLossOnDeviceCreated(std::uintptr_t renderer_owner) noexcept;

[[nodiscard]] std::expected<void, RendererResourceError>
RendererDeviceLossBeforeReset() noexcept;

[[nodiscard]] std::expected<void, RendererResourceError>
RendererDeviceLossAfterReset(std::uintptr_t renderer_owner) noexcept;

} // namespace gc::renderer_device_loss
