#pragma once
#include "Patches/RendererDeviceLoss/RendererResourceLifecycle.h"
#include "Patches/GameVersion/VersionedPlan.h"
#include <cstddef>
#include <cstdint>

namespace gc::renderer_device_loss {
enum class RendererResetLifecycleStage : std::uint8_t {
    before_reset,
    after_reset,
};

struct RendererResetFailureActions {
    void* context{};
    void (*failure)(
        void*, RendererResetLifecycleStage, RendererResourceError) noexcept{};
};

struct RendererNativeLayout final {
    std::size_t initialized_offset{};
    std::size_t index_buffer_holder_offset{};
    std::size_t vertex_buffer_lock_output_stack_offset{};
};
struct RendererNativeTargets final {
    std::uintptr_t initializer_epilogue{};
    std::uintptr_t vertex_buffer_lock_failure{};
    std::uintptr_t direct_batch_cleanup{};
    std::uintptr_t buffered_unlock_continuation{};
};

void OnDeviceLostTail(safetyhook::Context&) noexcept;
void OnVertexBufferCreateResult(safetyhook::Context&) noexcept;
void OnIndexBufferCreateResult(safetyhook::Context&) noexcept;
void OnVertexBufferLockGuard(safetyhook::Context&) noexcept;
void OnDirectLockResult(safetyhook::Context&) noexcept;
void OnBufferedUnlockResult(safetyhook::Context&) noexcept;
void OnWidescreenBeforeReset(safetyhook::Context&) noexcept;
void OnWidescreenAfterReset(safetyhook::Context&) noexcept;
[[nodiscard]] std::expected<void, RendererResourceError>
RendererDeviceLossSetResetFailureActions(RendererResetFailureActions) noexcept;
[[noreturn]] void PublishRendererResourceFailure(
    RendererResetLifecycleStage, RendererResourceError) noexcept;
[[nodiscard]] std::expected<void, game_version::PlanError> PrepareRendererDeviceLossRuntime(
    const game_version::ApprovedVersionedPlan&, const runtime_image::RuntimeImage&) noexcept;

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
