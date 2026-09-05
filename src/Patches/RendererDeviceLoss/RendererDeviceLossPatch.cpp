#include "Patches/RendererDeviceLoss/RendererDeviceLossPatch.h"
#include "Patches/RendererDeviceLoss/RendererDeviceLossProfile.h"
#include "Diagnostics/FatalProcess.h"
#include <Windows.h>
#include <Unknwn.h>
#include <plog/Log.h>
#include <algorithm>
#include <format>
#include <limits>
#include <memory>

namespace gc::renderer_device_loss {
namespace {
struct RendererDeviceLossRuntime final {
    const RendererNativeLayout layout;
    const RendererNativeTargets targets;
    RendererResourceLifecycle resource_lifecycle{};
    RendererResetFailureActions widescreen_reset_failure{};
};
std::unique_ptr<RendererDeviceLossRuntime> g_runtime_owner;

bool ClearInitialized(
    std::uintptr_t renderer,
    std::size_t offset) noexcept {
    if (renderer == 0 || offset != g_runtime_owner->layout.initialized_offset ||
        renderer > std::numeric_limits<std::uintptr_t>::max() - offset) {
        return false;
    }

    __try {
        *reinterpret_cast<std::uint8_t*>(renderer + offset) = 0;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool DetachIndexBuffer(
    std::uintptr_t renderer,
    std::size_t offset,
    std::uintptr_t& detached) noexcept {
    detached = 0;
    if (renderer == 0 || offset != g_runtime_owner->layout.index_buffer_holder_offset ||
        renderer > std::numeric_limits<std::uintptr_t>::max() - offset) {
        return false;
    }

    __try {
        const auto holder =
            *reinterpret_cast<std::uintptr_t*>(renderer + offset);
        if (holder == 0) {
            return false;
        }
        auto* const slot = reinterpret_cast<std::uintptr_t*>(holder);
        detached = *slot;
        *slot = 0;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        detached = 0;
        return false;
    }
}

bool ReleaseIndexBuffer(
    std::uintptr_t buffer) noexcept {
    if (buffer == 0) {
        return true;
    }

    __try {
        reinterpret_cast<IUnknown*>(buffer)->Release();
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool ReadPointer(
    std::uintptr_t address,
    std::uintptr_t& value) noexcept {
    __try {
        value = *reinterpret_cast<const std::uintptr_t*>(address);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool ApplyNegativeResultRedirect(
    safetyhook::Context& context,
    std::uintptr_t target) noexcept {
    if (target == 0 ||
        static_cast<std::int32_t>(context.eax) >= 0) {
        return false;
    }

    context.eip = static_cast<std::uint32_t>(target);
    return true;
}

bool ApplyRendererDeviceLostCleanup(
    const safetyhook::Context& context,
    const RendererNativeLayout& layout) noexcept {
    if (context.esi == 0) {
        return false;
    }
    if (!ClearInitialized(
            context.esi,
            layout.initialized_offset)) {
        return false;
    }

    std::uintptr_t detached = 0;
    if (!DetachIndexBuffer(
            context.esi,
            layout.index_buffer_holder_offset,
            detached)) {
        return false;
    }
    return detached == 0 ||
           ReleaseIndexBuffer(detached);
}

bool ApplyRendererDeviceLossRetry(
    safetyhook::Context& context,
    const RendererNativeLayout& layout,
    const RendererNativeTargets& targets) noexcept {
    if (static_cast<std::int32_t>(context.eax) >= 0) {
        return false;
    }
    if (context.esi == 0 ||
        targets.initializer_epilogue == 0 ||
        !ClearInitialized(
            context.esi,
            layout.initialized_offset)) {
        return false;
    }

    context.eip = static_cast<std::uint32_t>(
        targets.initializer_epilogue);
    return true;
}

bool ApplyRendererDeviceLossDrawSkip(
    safetyhook::Context& context,
    const RendererNativeLayout& layout,
    const RendererNativeTargets& targets) noexcept {
    if (targets.vertex_buffer_lock_failure == 0 || context.ecx != 0 ||
        context.edi != 0 || context.ebx != 0 ||
        context.esp > std::numeric_limits<std::uint32_t>::max() -
                          layout.vertex_buffer_lock_output_stack_offset) {
        return false;
    }

    std::uintptr_t output_pair = 0;
    if (!ReadPointer(
            static_cast<std::uintptr_t>(context.esp) +
                layout.vertex_buffer_lock_output_stack_offset,
            output_pair) ||
        output_pair == 0 ||
        output_pair > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }

    context.eax = static_cast<std::uint32_t>(output_pair);
    context.eip = static_cast<std::uint32_t>(
        targets.vertex_buffer_lock_failure);
    return true;
}

bool ApplyRendererDeviceLossDirectLockSkip(
    safetyhook::Context& context,
    const RendererNativeTargets& targets) noexcept {
    return ApplyNegativeResultRedirect(
        context,
        targets.direct_batch_cleanup);
}

bool ApplyRendererDeviceLossUnlockCompletion(
    safetyhook::Context& context,
    const RendererNativeTargets& targets) noexcept {
    return ApplyNegativeResultRedirect(
        context,
        targets.buffered_unlock_continuation);
}


} // namespace

// SafetyHook requires a mutable Context reference in the mid-hook callback ABI.
// ReSharper disable once CppParameterMayBeConstPtrOrRef
void OnDeviceLostTail(safetyhook::Context& context) noexcept {
    try {
        static_cast<void>(ApplyRendererDeviceLostCleanup(
            context,
            g_runtime_owner->layout));
    } catch (...) {
    }
}

void OnVertexBufferCreateResult(
    safetyhook::Context& context) noexcept {
    try {
        static_cast<void>(ApplyRendererDeviceLossRetry(
            context,
            g_runtime_owner->layout,
            g_runtime_owner->targets));
    } catch (...) {
    }
}

void OnIndexBufferCreateResult(
    safetyhook::Context& context) noexcept {
    try {
        static_cast<void>(ApplyRendererDeviceLossRetry(
            context,
            g_runtime_owner->layout,
            g_runtime_owner->targets));
    } catch (...) {
    }
}

void OnVertexBufferLockGuard(
    safetyhook::Context& context) noexcept {
    try {
        static_cast<void>(ApplyRendererDeviceLossDrawSkip(
            context,
            g_runtime_owner->layout,
            g_runtime_owner->targets));
    } catch (...) {
    }
}

void OnDirectLockResult(safetyhook::Context& context) noexcept {
    try {
        static_cast<void>(ApplyRendererDeviceLossDirectLockSkip(
            context,
            g_runtime_owner->targets));
    } catch (...) {
    }
}

void OnBufferedUnlockResult(safetyhook::Context& context) noexcept {
    try {
        static_cast<void>(ApplyRendererDeviceLossUnlockCompletion(
            context,
            g_runtime_owner->targets));
    } catch (...) {
    }
}


std::expected<void, game_version::PlanError> PrepareRendererDeviceLossRuntime(
    const game_version::ApprovedVersionedPlan& plan,
    const runtime_image::RuntimeImage& image) noexcept {
    using namespace game_version;
    const auto invalid = [&](std::string_view site, PlanStage stage = PlanStage::invalid_plan) {
        return std::unexpected(PlanError{.stage = stage, .context = plan.context(),
            .feature = FeatureId::renderer_device_loss, .site = site});
    };
    try {
        if (g_runtime_owner) return invalid("runtime_already_prepared");
        const auto* build = std::get_if<GameBuild>(&plan.context().build);
        const auto* variant = std::get_if<GameImageVariant>(&plan.context().variant);
        const auto* profile = build && variant ? ProfileFor(*build, *variant) : nullptr;
        if (!profile || image.base() != plan.image_base() || image.size() != plan.image_size())
            return invalid("runtime_image_binding");
        RendererNativeTargets targets{};
        for (const auto& operation : profile->operations) {
            const auto& expected = ContractOf(operation);
            const auto site = std::ranges::find_if(plan.sites(), [&](const ApprovedSite& entry) {
                const auto& actual = entry.contract();
                return actual.feature == expected.feature && actual.site == expected.site &&
                    actual.kind == expected.kind && actual.rva == expected.rva;
            });
            if (site == plan.sites().end()) return invalid(expected.site);
            const auto resolved = image.Resolve({"renderer_device_loss", expected.site, expected.rva},
                std::max<std::size_t>(expected.protected_span, expected.original.size));
            if (!resolved) return std::unexpected(PlanError{.stage = PlanStage::address_range,
                .context = plan.context(), .feature = expected.feature, .site = expected.site,
                .rva = expected.rva, .memory = resolved.error()});
            if (*resolved != site->address) return invalid(expected.site);
            if (expected.site == "initializer_epilogue") targets.initializer_epilogue = *resolved;
            else if (expected.site == "vertex_buffer_lock_failure") targets.vertex_buffer_lock_failure = *resolved;
            else if (expected.site == "direct_batch_cleanup") targets.direct_batch_cleanup = *resolved;
            else if (expected.site == "buffered_unlock_continuation") targets.buffered_unlock_continuation = *resolved;
        }
        if (!targets.initializer_epilogue || !targets.vertex_buffer_lock_failure ||
            !targets.direct_batch_cleanup || !targets.buffered_unlock_continuation)
            return invalid("continuations");
        g_runtime_owner = std::make_unique<RendererDeviceLossRuntime>(profile->layout, targets);
        return {};
    } catch (...) { return invalid("runtime_allocation", PlanStage::allocation); }
}
[[noreturn]] void PublishRendererResourceFailure(
    RendererResetLifecycleStage stage, RendererResourceError error) noexcept {
    if (g_runtime_owner) {
        const auto actions = g_runtime_owner->widescreen_reset_failure;
        if (actions.context && actions.failure) actions.failure(actions.context, stage, error);
    }
    // A participant normally supplies the richer D3D diagnostic and aborts.
    // Missing or returning publishers still cannot continue after resource failure.
    try {
        diagnostics::AbortProcess({
            std::format("Renderer resource reset failed stage={} error={}",
                static_cast<unsigned>(stage), static_cast<unsigned>(error)),
            L"GCLoader could not restore renderer resources. Check loader-log.txt.",
            L"GCLoader renderer error"});
    } catch (...) { diagnostics::AbortProcess({}); }
}
std::expected<void, RendererResourceError> RendererDeviceLossSetResetFailureActions(
    RendererResetFailureActions actions) noexcept {
    if (!g_runtime_owner) return std::unexpected(RendererResourceError::runtime_unavailable);
    if (!actions.context || !actions.failure)
        return std::unexpected(RendererResourceError::invalid_participant);
    g_runtime_owner->widescreen_reset_failure = actions;
    return {};
}
void OnWidescreenBeforeReset(safetyhook::Context&) noexcept {
    const auto released = RendererDeviceLossBeforeReset();
    if (!released) PublishRendererResourceFailure(RendererResetLifecycleStage::before_reset, released.error());
}
void OnWidescreenAfterReset(safetyhook::Context& context) noexcept {
    const auto recreated = RendererDeviceLossAfterReset(context.esi);
    if (!recreated) PublishRendererResourceFailure(RendererResetLifecycleStage::after_reset, recreated.error());
}
std::expected<void, RendererResourceError>
RendererDeviceLossAttachResource(
    const RendererResourceParticipant participant) noexcept {
    if (!g_runtime_owner) {
        return std::unexpected(RendererResourceError::runtime_unavailable);
    }
    return g_runtime_owner->resource_lifecycle.Attach(participant);
}

void RendererDeviceLossDetachResource() noexcept {
    if (g_runtime_owner) {
        g_runtime_owner->resource_lifecycle.Detach();
        g_runtime_owner->widescreen_reset_failure = {};
    }
}


std::expected<void, RendererResourceError>
RendererDeviceLossOnDeviceCreated(
    const std::uintptr_t renderer_owner) noexcept {
    if (!g_runtime_owner) {
        return std::unexpected(RendererResourceError::runtime_unavailable);
    }
    return g_runtime_owner->resource_lifecycle.OnDeviceCreated(renderer_owner);
}

std::expected<void, RendererResourceError>
RendererDeviceLossBeforeReset() noexcept {
    if (!g_runtime_owner) {
        return std::unexpected(RendererResourceError::runtime_unavailable);
    }
    return g_runtime_owner->resource_lifecycle.BeforeReset();
}

std::expected<void, RendererResourceError>
RendererDeviceLossAfterReset(
    const std::uintptr_t renderer_owner) noexcept {
    if (!g_runtime_owner) {
        return std::unexpected(RendererResourceError::runtime_unavailable);
    }
    return g_runtime_owner->resource_lifecycle.AfterReset(renderer_owner);
}

} // namespace gc::renderer_device_loss
