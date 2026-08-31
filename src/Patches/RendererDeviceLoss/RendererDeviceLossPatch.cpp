#include "Patches/RendererDeviceLoss/RendererDeviceLossPatch.h"

#include <Windows.h>
#include <Unknwn.h>

#include <plog/Log.h>

#include <algorithm>
#include <atomic>
// ReSharper disable once CppUnusedIncludeDirective
#include <cstring>
#include <iomanip>
#include <limits>
#include <memory>

namespace gc::renderer_device_loss {

RendererResetHookPair::~RendererResetHookPair() {
    Reset();
}

std::expected<void, RendererResetHookPairError>
RendererResetHookPair::PrepareDisabled(
    const std::uintptr_t pre_reset_address,
    const std::uintptr_t post_reset_address,
    const RendererResetHookPairActions actions) noexcept {
    if (actions.context == nullptr || actions.create_disabled == nullptr ||
        actions.enable == nullptr || actions.reset == nullptr ||
        pre_reset_address == 0 || post_reset_address == 0) {
        return std::unexpected(RendererResetHookPairError{
            .stage = RendererResetHookPairStage::invalid_actions,
        });
    }
    if (state_ != RendererResetHookPairState::empty) {
        return std::unexpected(RendererResetHookPairError{
            .stage = RendererResetHookPairStage::invalid_state,
        });
    }

    actions_ = actions;
    pre_attempted_ = true;
    if (!actions_.create_disabled(
            actions_.context,
            RendererResetHookSite::pre_reset,
            pre_reset_address)) {
        Reset();
        return std::unexpected(RendererResetHookPairError{
            .stage = RendererResetHookPairStage::create_disabled,
            .site = RendererResetHookSite::pre_reset,
        });
    }

    post_attempted_ = true;
    if (!actions_.create_disabled(
            actions_.context,
            RendererResetHookSite::post_reset,
            post_reset_address)) {
        Reset();
        return std::unexpected(RendererResetHookPairError{
            .stage = RendererResetHookPairStage::create_disabled,
            .site = RendererResetHookSite::post_reset,
        });
    }

    state_ = RendererResetHookPairState::disabled;
    return {};
}

std::expected<void, RendererResetHookPairError>
RendererResetHookPair::Enable() noexcept {
    if (state_ != RendererResetHookPairState::disabled) {
        return std::unexpected(RendererResetHookPairError{
            .stage = RendererResetHookPairStage::invalid_state,
        });
    }
    if (!actions_.enable(
            actions_.context,
            RendererResetHookSite::pre_reset)) {
        Reset();
        return std::unexpected(RendererResetHookPairError{
            .stage = RendererResetHookPairStage::enable,
            .site = RendererResetHookSite::pre_reset,
        });
    }
    if (!actions_.enable(
            actions_.context,
            RendererResetHookSite::post_reset)) {
        Reset();
        return std::unexpected(RendererResetHookPairError{
            .stage = RendererResetHookPairStage::enable,
            .site = RendererResetHookSite::post_reset,
        });
    }
    state_ = RendererResetHookPairState::active;
    return {};
}

void RendererResetHookPair::Reset() noexcept {
    if (actions_.reset != nullptr) {
        if (post_attempted_) {
            actions_.reset(
                actions_.context,
                RendererResetHookSite::post_reset);
        }
        if (pre_attempted_) {
            actions_.reset(
                actions_.context,
                RendererResetHookSite::pre_reset);
        }
    }
    actions_ = {};
    pre_attempted_ = false;
    post_attempted_ = false;
    state_ = RendererResetHookPairState::empty;
}

namespace {

struct RendererDeviceLossRuntime {
    safetyhook::MidHook device_lost_tail_hook{};
    safetyhook::MidHook vertex_buffer_result_hook{};
    safetyhook::MidHook index_buffer_result_hook{};
    safetyhook::MidHook vertex_buffer_lock_guard_hook{};
    safetyhook::MidHook direct_lock_result_hook{};
    safetyhook::MidHook buffered_unlock_result_hook{};
    RendererResourceLifecycle resource_lifecycle{};
    safetyhook::MidHook widescreen_pre_reset_hook{};
    safetyhook::MidHook widescreen_post_reset_hook{};
    RendererResetHookPair widescreen_reset_pair{};
    RendererResetFailureActions widescreen_reset_failure{};
};

std::unique_ptr<RendererDeviceLossRuntime> g_runtime_owner;

void NotifyWidescreenResetFailure(
    const RendererResetLifecycleStage stage,
    const RendererResourceError error) noexcept {
    if (!g_runtime_owner) {
        return;
    }
    const auto actions = g_runtime_owner->widescreen_reset_failure;
    if (actions.context != nullptr && actions.failure != nullptr) {
        actions.failure(actions.context, stage, error);
    }
}

void OnWidescreenBeforeReset(safetyhook::Context&) noexcept {
    if (!g_runtime_owner) {
        return;
    }
    const auto released =
        g_runtime_owner->resource_lifecycle.BeforeReset();
    if (!released) {
        NotifyWidescreenResetFailure(
            RendererResetLifecycleStage::before_reset,
            released.error());
    }
}

void OnWidescreenAfterReset(safetyhook::Context& context) noexcept {
    if (!g_runtime_owner) {
        return;
    }
    const auto recreated =
        g_runtime_owner->resource_lifecycle.AfterReset(context.esi);
    if (!recreated) {
        NotifyWidescreenResetFailure(
            RendererResetLifecycleStage::after_reset,
            recreated.error());
    }
}

bool CreateWidescreenResetHookDisabled(
    void* opaque,
    const RendererResetHookSite site,
    const std::uintptr_t address) noexcept {
    if (opaque == nullptr || address == 0) {
        return false;
    }
    auto& runtime = *static_cast<RendererDeviceLossRuntime*>(opaque);
    try {
        const auto callback = site == RendererResetHookSite::pre_reset
            ? &OnWidescreenBeforeReset
            : &OnWidescreenAfterReset;
        auto created = safetyhook::MidHook::create(
            reinterpret_cast<void*>(address),
            callback,
            safetyhook::MidHook::StartDisabled);
        if (!created) {
            return false;
        }
        auto& destination = site == RendererResetHookSite::pre_reset
            ? runtime.widescreen_pre_reset_hook
            : runtime.widescreen_post_reset_hook;
        destination = std::move(*created);
        return true;
    } catch (...) {
        return false;
    }
}

bool EnableWidescreenResetHook(
    void* opaque,
    const RendererResetHookSite site) noexcept {
    if (opaque == nullptr) {
        return false;
    }
    auto& runtime = *static_cast<RendererDeviceLossRuntime*>(opaque);
    try {
        auto& hook = site == RendererResetHookSite::pre_reset
            ? runtime.widescreen_pre_reset_hook
            : runtime.widescreen_post_reset_hook;
        return hook.enable().has_value();
    } catch (...) {
        return false;
    }
}

void ResetWidescreenResetHook(
    void* opaque,
    const RendererResetHookSite site) noexcept {
    if (opaque == nullptr) {
        return;
    }
    auto& runtime = *static_cast<RendererDeviceLossRuntime*>(opaque);
    try {
        auto& hook = site == RendererResetHookSite::pre_reset
            ? runtime.widescreen_pre_reset_hook
            : runtime.widescreen_post_reset_hook;
        hook.reset();
    } catch (...) {
    }
}

bool ProductionRead(
    void*,
    std::uintptr_t address,
    std::span<std::byte> output) noexcept {
    if (address == 0 || output.empty()) {
        return false;
    }

    __try {
        std::memcpy(
            output.data(),
            reinterpret_cast<const void*>(address),
            output.size());
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool ProductionClearInitialized(
    void*,
    std::uintptr_t renderer,
    std::size_t offset) noexcept {
    if (renderer == 0 || offset != kRendererInitializedOffset ||
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

bool ProductionDetachIndexBuffer(
    void*,
    std::uintptr_t renderer,
    std::size_t offset,
    std::uintptr_t& detached) noexcept {
    detached = 0;
    if (renderer == 0 || offset != kRendererIndexBufferHolderOffset ||
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

bool ProductionReleaseIndexBuffer(
    void*,
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

bool ProductionReadPointer(
    void*,
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
    std::uintptr_t image_base,
    std::uint32_t target_rva) noexcept {
    if (image_base != kPreferredImageBase ||
        static_cast<std::int32_t>(context.eax) >= 0) {
        return false;
    }

    context.eip = static_cast<std::uint32_t>(image_base + target_rva);
    return true;
}

// SafetyHook requires a mutable Context reference in the mid-hook callback ABI.
// ReSharper disable once CppParameterMayBeConstPtrOrRef
void OnDeviceLostTail(safetyhook::Context& context) noexcept {
    try {
        static_cast<void>(ApplyRendererDeviceLostCleanup(
            context,
            {
                .clear_initialized = ProductionClearInitialized,
                .detach_index_buffer = ProductionDetachIndexBuffer,
                .release_index_buffer = ProductionReleaseIndexBuffer,
            }));
    } catch (...) {
    }
}

void OnVertexBufferCreateResult(
    safetyhook::Context& context) noexcept {
    try {
        static_cast<void>(ApplyRendererDeviceLossRetry(
            context,
            kPreferredImageBase,
            {
                .clear_initialized = ProductionClearInitialized,
            }));
    } catch (...) {
    }
}

void OnIndexBufferCreateResult(
    safetyhook::Context& context) noexcept {
    try {
        static_cast<void>(ApplyRendererDeviceLossRetry(
            context,
            kPreferredImageBase,
            {
                .clear_initialized = ProductionClearInitialized,
            }));
    } catch (...) {
    }
}

void OnVertexBufferLockGuard(
    safetyhook::Context& context) noexcept {
    try {
        static_cast<void>(ApplyRendererDeviceLossDrawSkip(
            context,
            kPreferredImageBase,
            {
                .read_pointer = ProductionReadPointer,
            }));
    } catch (...) {
    }
}

void OnDirectLockResult(safetyhook::Context& context) noexcept {
    try {
        static_cast<void>(ApplyRendererDeviceLossDirectLockSkip(
            context,
            kPreferredImageBase));
    } catch (...) {
    }
}

void OnBufferedUnlockResult(safetyhook::Context& context) noexcept {
    try {
        static_cast<void>(ApplyRendererDeviceLossUnlockCompletion(
            context,
            kPreferredImageBase));
    } catch (...) {
    }
}

bool ProductionInstallHook(
    void* opaque,
    RendererContractSite site,
    std::uintptr_t address) noexcept {
    if (opaque == nullptr || address == 0) {
        return false;
    }

    try {
        auto& runtime =
            *static_cast<RendererDeviceLossRuntime*>(opaque);
        switch (site) {
        case RendererContractSite::DeviceLostTail:
            runtime.device_lost_tail_hook = safetyhook::create_mid(
                reinterpret_cast<void*>(address),
                OnDeviceLostTail);
            return static_cast<bool>(runtime.device_lost_tail_hook);
        case RendererContractSite::VertexBufferResult:
            runtime.vertex_buffer_result_hook = safetyhook::create_mid(
                reinterpret_cast<void*>(address),
                OnVertexBufferCreateResult);
            return static_cast<bool>(runtime.vertex_buffer_result_hook);
        case RendererContractSite::IndexBufferResult:
            runtime.index_buffer_result_hook = safetyhook::create_mid(
                reinterpret_cast<void*>(address),
                OnIndexBufferCreateResult);
            return static_cast<bool>(runtime.index_buffer_result_hook);
        case RendererContractSite::VertexBufferLockGuard:
            runtime.vertex_buffer_lock_guard_hook = safetyhook::create_mid(
                reinterpret_cast<void*>(address),
                OnVertexBufferLockGuard);
            return static_cast<bool>(
                runtime.vertex_buffer_lock_guard_hook);
        case RendererContractSite::DirectLockResult:
            runtime.direct_lock_result_hook = safetyhook::create_mid(
                reinterpret_cast<void*>(address),
                OnDirectLockResult);
            return static_cast<bool>(runtime.direct_lock_result_hook);
        case RendererContractSite::BufferedUnlockResult:
            runtime.buffered_unlock_result_hook = safetyhook::create_mid(
                reinterpret_cast<void*>(address),
                OnBufferedUnlockResult);
            return static_cast<bool>(runtime.buffered_unlock_result_hook);
        case RendererContractSite::None:
        case RendererContractSite::InitializerEpilogue:
        case RendererContractSite::VertexBufferLockFailure:
        case RendererContractSite::DirectBatchCleanup:
        case RendererContractSite::BufferedUnlockContinuation:
            return false;
        }
    } catch (...) {
        return false;
    }
    return false;
}

void ProductionResetHook(void* opaque) noexcept {
    if (opaque == nullptr) {
        return;
    }

    try {
        auto& runtime =
            *static_cast<RendererDeviceLossRuntime*>(opaque);
        runtime.buffered_unlock_result_hook.reset();
        runtime.direct_lock_result_hook.reset();
        runtime.vertex_buffer_lock_guard_hook.reset();
        runtime.index_buffer_result_hook.reset();
        runtime.vertex_buffer_result_hook.reset();
        runtime.device_lost_tail_hook.reset();
    } catch (...) {
    }
}

const char* InstallStageName(RendererInstallStage stage) noexcept {
    switch (stage) {
    case RendererInstallStage::None: return "none";
    case RendererInstallStage::InvalidActions: return "invalid_actions";
    case RendererInstallStage::UnexpectedImageBase:
        return "unexpected_image_base";
    case RendererInstallStage::PreflightRead: return "preflight_read";
    case RendererInstallStage::PreflightMismatch:
        return "preflight_mismatch";
    case RendererInstallStage::HookInstall: return "hook_install";
    }
    return "unknown";
}

const char* ContractSiteName(RendererContractSite site) noexcept {
    switch (site) {
    case RendererContractSite::None: return "none";
    case RendererContractSite::DeviceLostTail:
        return "device_lost_tail";
    case RendererContractSite::VertexBufferResult:
        return "vertex_buffer_result";
    case RendererContractSite::IndexBufferResult:
        return "index_buffer_result";
    case RendererContractSite::InitializerEpilogue:
        return "initializer_epilogue";
    case RendererContractSite::VertexBufferLockGuard:
        return "vertex_buffer_lock_guard";
    case RendererContractSite::VertexBufferLockFailure:
        return "vertex_buffer_lock_failure";
    case RendererContractSite::DirectLockResult:
        return "direct_lock_result";
    case RendererContractSite::DirectBatchCleanup:
        return "direct_batch_cleanup";
    case RendererContractSite::BufferedUnlockResult:
        return "buffered_unlock_result";
    case RendererContractSite::BufferedUnlockContinuation:
        return "buffered_unlock_continuation";
    }
    return "unknown";
}

void LogInstallFailure(const RendererInstallError& error) noexcept {
    try {
        PLOG_ERROR << "RendererDeviceLossPatch: install failed stage="
                   << InstallStageName(error.stage)
                   << " site=" << ContractSiteName(error.site);
    } catch (...) {
    }
}

} // namespace

bool ApplyRendererDeviceLostCleanup(
    const safetyhook::Context& context,
    const RendererDeviceLostActions& actions) noexcept {
    if (context.esi == 0 || actions.clear_initialized == nullptr ||
        actions.detach_index_buffer == nullptr ||
        actions.release_index_buffer == nullptr) {
        return false;
    }
    if (!actions.clear_initialized(
            actions.context,
            context.esi,
            kRendererInitializedOffset)) {
        return false;
    }

    std::uintptr_t detached = 0;
    if (!actions.detach_index_buffer(
            actions.context,
            context.esi,
            kRendererIndexBufferHolderOffset,
            detached)) {
        return false;
    }
    return detached == 0 ||
           actions.release_index_buffer(actions.context, detached);
}

bool ApplyRendererDeviceLossRetry(
    safetyhook::Context& context,
    std::uintptr_t image_base,
    RendererInitializedWriter writer) noexcept {
    if (static_cast<std::int32_t>(context.eax) >= 0) {
        return false;
    }
    if (context.esi == 0 || writer.clear_initialized == nullptr ||
        image_base != kPreferredImageBase ||
        !writer.clear_initialized(
            writer.context,
            context.esi,
            kRendererInitializedOffset)) {
        return false;
    }

    context.eip = static_cast<std::uint32_t>(
        image_base + kRendererInitializerEpilogueRva);
    return true;
}

bool ApplyRendererDeviceLossDrawSkip(
    safetyhook::Context& context,
    std::uintptr_t image_base,
    RendererStackPointerReader reader) noexcept {
    if (image_base != kPreferredImageBase || context.ecx != 0 ||
        context.edi != 0 || context.ebx != 0 ||
        reader.read_pointer == nullptr ||
        context.esp > std::numeric_limits<std::uint32_t>::max() -
                          kVertexBufferLockOutputStackOffset) {
        return false;
    }

    std::uintptr_t output_pair = 0;
    if (!reader.read_pointer(
            reader.context,
            static_cast<std::uintptr_t>(context.esp) +
                kVertexBufferLockOutputStackOffset,
            output_pair) ||
        output_pair == 0 ||
        output_pair > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }

    context.eax = static_cast<std::uint32_t>(output_pair);
    context.eip = static_cast<std::uint32_t>(
        image_base + kVertexBufferLockFailureRva);
    return true;
}

bool ApplyRendererDeviceLossDirectLockSkip(
    safetyhook::Context& context,
    std::uintptr_t image_base) noexcept {
    return ApplyNegativeResultRedirect(
        context,
        image_base,
        kDirectBatchCleanupRva);
}

bool ApplyRendererDeviceLossUnlockCompletion(
    safetyhook::Context& context,
    std::uintptr_t image_base) noexcept {
    return ApplyNegativeResultRedirect(
        context,
        image_base,
        kBufferedUnlockContinuationRva);
}

std::expected<void, RendererInstallError>
InstallRendererDeviceLossPatch(
    std::uintptr_t image_base,
    const RendererInstallActions& actions) noexcept {
    if (actions.context == nullptr || actions.read == nullptr ||
        actions.install_hook == nullptr || actions.reset_hook == nullptr) {
        return std::unexpected(RendererInstallError{
            .stage = RendererInstallStage::InvalidActions,
        });
    }
    if (image_base != kPreferredImageBase) {
        return std::unexpected(RendererInstallError{
            .stage = RendererInstallStage::UnexpectedImageBase,
        });
    }

    const auto preflight = [&](RendererContractSite site,
                               std::uint32_t rva,
                               std::span<const std::byte> expected)
        -> std::expected<void, RendererInstallError> {
        std::array<std::byte, 12> actual{};
        if (expected.empty() || expected.size() > actual.size() ||
            !actions.read(
                actions.context,
                image_base + rva,
                std::span{actual}.first(expected.size()))) {
            return std::unexpected(RendererInstallError{
                .stage = RendererInstallStage::PreflightRead,
                .site = site,
            });
        }
        if (!std::ranges::equal(
                std::span{actual}.first(expected.size()),
                expected)) {
            return std::unexpected(RendererInstallError{
                .stage = RendererInstallStage::PreflightMismatch,
                .site = site,
            });
        }
        return {};
    };

    auto device_lost_tail = preflight(
        RendererContractSite::DeviceLostTail,
        kDeviceLostTailRva,
        kDeviceLostTailPattern);
    if (!device_lost_tail) {
        return device_lost_tail;
    }

    auto result_site = preflight(
        RendererContractSite::VertexBufferResult,
        kVertexBufferResultRva,
        kVertexBufferResultPattern);
    if (!result_site) {
        return result_site;
    }

    auto index_result_site = preflight(
        RendererContractSite::IndexBufferResult,
        kIndexBufferResultRva,
        kIndexBufferResultPattern);
    if (!index_result_site) {
        return index_result_site;
    }

    auto epilogue = preflight(
        RendererContractSite::InitializerEpilogue,
        kRendererInitializerEpilogueRva,
        kRendererEpiloguePattern);
    if (!epilogue) {
        return epilogue;
    }

    auto lock_guard = preflight(
        RendererContractSite::VertexBufferLockGuard,
        kVertexBufferLockGuardRva,
        kVertexBufferLockGuardPattern);
    if (!lock_guard) {
        return lock_guard;
    }

    auto lock_failure = preflight(
        RendererContractSite::VertexBufferLockFailure,
        kVertexBufferLockFailureRva,
        kVertexBufferLockFailurePattern);
    if (!lock_failure) {
        return lock_failure;
    }

    auto direct_lock_result = preflight(
        RendererContractSite::DirectLockResult,
        kDirectLockResultRva,
        kDirectLockResultPattern);
    if (!direct_lock_result) {
        return direct_lock_result;
    }

    auto direct_batch_cleanup = preflight(
        RendererContractSite::DirectBatchCleanup,
        kDirectBatchCleanupRva,
        kDirectBatchCleanupPattern);
    if (!direct_batch_cleanup) {
        return direct_batch_cleanup;
    }

    auto buffered_unlock_result = preflight(
        RendererContractSite::BufferedUnlockResult,
        kBufferedUnlockResultRva,
        kBufferedUnlockResultPattern);
    if (!buffered_unlock_result) {
        return buffered_unlock_result;
    }

    auto buffered_unlock_continuation = preflight(
        RendererContractSite::BufferedUnlockContinuation,
        kBufferedUnlockContinuationRva,
        kBufferedUnlockContinuationPattern);
    if (!buffered_unlock_continuation) {
        return buffered_unlock_continuation;
    }

    if (!actions.install_hook(
            actions.context,
            RendererContractSite::DeviceLostTail,
            image_base + kDeviceLostTailRva)) {
        actions.reset_hook(actions.context);
        return std::unexpected(RendererInstallError{
            .stage = RendererInstallStage::HookInstall,
            .site = RendererContractSite::DeviceLostTail,
        });
    }

    if (!actions.install_hook(
            actions.context,
            RendererContractSite::VertexBufferResult,
            image_base + kVertexBufferResultRva)) {
        actions.reset_hook(actions.context);
        return std::unexpected(RendererInstallError{
            .stage = RendererInstallStage::HookInstall,
            .site = RendererContractSite::VertexBufferResult,
        });
    }
    if (!actions.install_hook(
            actions.context,
            RendererContractSite::IndexBufferResult,
            image_base + kIndexBufferResultRva)) {
        actions.reset_hook(actions.context);
        return std::unexpected(RendererInstallError{
            .stage = RendererInstallStage::HookInstall,
            .site = RendererContractSite::IndexBufferResult,
        });
    }
    if (!actions.install_hook(
            actions.context,
            RendererContractSite::VertexBufferLockGuard,
            image_base + kVertexBufferLockGuardRva)) {
        actions.reset_hook(actions.context);
        return std::unexpected(RendererInstallError{
            .stage = RendererInstallStage::HookInstall,
            .site = RendererContractSite::VertexBufferLockGuard,
        });
    }
    if (!actions.install_hook(
            actions.context,
            RendererContractSite::DirectLockResult,
            image_base + kDirectLockResultRva)) {
        actions.reset_hook(actions.context);
        return std::unexpected(RendererInstallError{
            .stage = RendererInstallStage::HookInstall,
            .site = RendererContractSite::DirectLockResult,
        });
    }
    if (!actions.install_hook(
            actions.context,
            RendererContractSite::BufferedUnlockResult,
            image_base + kBufferedUnlockResultRva)) {
        actions.reset_hook(actions.context);
        return std::unexpected(RendererInstallError{
            .stage = RendererInstallStage::HookInstall,
            .site = RendererContractSite::BufferedUnlockResult,
        });
    }
    return {};
}

bool RendererDeviceLossPatchInit() noexcept {
    static std::atomic_bool attempted{false};
    static std::atomic_bool stored_result{false};
    bool expected = false;
    if (!attempted.compare_exchange_strong(expected, true)) {
        return stored_result.load(std::memory_order_acquire);
    }

    try {
        const auto module = GetModuleHandleW(nullptr);
        const auto image_base = reinterpret_cast<std::uintptr_t>(module);
        auto candidate = std::make_unique<RendererDeviceLossRuntime>();
        auto installed = InstallRendererDeviceLossPatch(
            image_base,
            {
                .context = candidate.get(),
                .read = ProductionRead,
                .install_hook = ProductionInstallHook,
                .reset_hook = ProductionResetHook,
            });
        if (!installed) {
            LogInstallFailure(installed.error());
            stored_result.store(false, std::memory_order_release);
            return false;
        }

        g_runtime_owner = std::move(candidate);
        stored_result.store(true, std::memory_order_release);
        try {
            PLOG_INFO << "RendererDeviceLossPatch: hook committed"
                      << " lost_tail_rva=0x" << std::hex
                      << kDeviceLostTailRva
                      << " result_rva=0x"
                      << kVertexBufferResultRva
                      << " index_result_rva=0x"
                      << kIndexBufferResultRva
                      << " retry_rva=0x"
                      << kRendererInitializerEpilogueRva
                      << " lock_guard_rva=0x"
                      << kVertexBufferLockGuardRva
                      << " lock_failure_rva=0x"
                      << kVertexBufferLockFailureRva
                      << " direct_lock_result_rva=0x"
                      << kDirectLockResultRva
                      << " direct_batch_cleanup_rva=0x"
                      << kDirectBatchCleanupRva
                      << " buffered_unlock_result_rva=0x"
                      << kBufferedUnlockResultRva
                      << " buffered_unlock_continuation_rva=0x"
                      << kBufferedUnlockContinuationRva << std::dec;
        } catch (...) {
        }
        return true;
    } catch (...) {
        try {
            PLOG_ERROR
                << "RendererDeviceLossPatch: initialization exception"
                << " before publish";
        } catch (...) {
        }
        stored_result.store(false, std::memory_order_release);
        return false;
    }
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
    }
}

std::expected<void, RendererResetHookPairError>
RendererDeviceLossPrepareResetHooksDisabled(
    const std::uintptr_t pre_reset_address,
    const std::uintptr_t post_reset_address,
    const RendererResetFailureActions failure_actions) noexcept {
    if (!g_runtime_owner) {
        return std::unexpected(RendererResetHookPairError{
            .stage = RendererResetHookPairStage::invalid_state,
        });
    }
    if (failure_actions.context == nullptr ||
        failure_actions.failure == nullptr) {
        return std::unexpected(RendererResetHookPairError{
            .stage = RendererResetHookPairStage::invalid_actions,
        });
    }

    auto& runtime = *g_runtime_owner;
    if (runtime.widescreen_reset_pair.state() !=
        RendererResetHookPairState::empty) {
        return std::unexpected(RendererResetHookPairError{
            .stage = RendererResetHookPairStage::invalid_state,
        });
    }
    runtime.widescreen_reset_failure = failure_actions;
    const auto prepared = runtime.widescreen_reset_pair.PrepareDisabled(
        pre_reset_address,
        post_reset_address,
        RendererResetHookPairActions{
            .context = &runtime,
            .create_disabled = &CreateWidescreenResetHookDisabled,
            .enable = &EnableWidescreenResetHook,
            .reset = &ResetWidescreenResetHook,
        });
    if (!prepared) {
        runtime.widescreen_reset_failure = {};
    }
    return prepared;
}

std::expected<void, RendererResetHookPairError>
RendererDeviceLossEnableResetHooks() noexcept {
    if (!g_runtime_owner) {
        return std::unexpected(RendererResetHookPairError{
            .stage = RendererResetHookPairStage::invalid_state,
        });
    }
    const auto enabled =
        g_runtime_owner->widescreen_reset_pair.Enable();
    if (!enabled && g_runtime_owner->widescreen_reset_pair.state() ==
                        RendererResetHookPairState::empty) {
        g_runtime_owner->widescreen_reset_failure = {};
    }
    return enabled;
}

void RendererDeviceLossResetHooks() noexcept {
    if (!g_runtime_owner) {
        return;
    }
    g_runtime_owner->widescreen_reset_pair.Reset();
    g_runtime_owner->widescreen_reset_failure = {};
}

RendererResetHookPairState
RendererDeviceLossResetHookPairState() noexcept {
    return g_runtime_owner
        ? g_runtime_owner->widescreen_reset_pair.state()
        : RendererResetHookPairState::empty;
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
