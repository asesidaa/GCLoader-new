#include "Patches/RendererDeviceLoss/RendererDeviceLossPatch.h"

#include <safetyhook.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <span>
#include <utility>

namespace {

int Expect(bool condition, const char* name) {
    if (condition) {
        return 0;
    }
    std::cerr << "Expectation failed: " << name << "\n";
    return 1;
}

safetyhook::Context CanaryContext() {
    safetyhook::Context context{};
    auto* bytes = reinterpret_cast<unsigned char*>(&context);
    for (std::size_t index = 0; index < sizeof(context); ++index) {
        bytes[index] = static_cast<unsigned char>((index % 251U) + 1U);
    }
    return context;
}

bool ContextEquals(
    const safetyhook::Context& left,
    const safetyhook::Context& right) {
    return std::memcmp(&left, &right, sizeof(left)) == 0;
}

bool ContextEqualsExceptEip(
    const safetyhook::Context& actual,
    const safetyhook::Context& original,
    std::uint32_t expected_eip) {
    if (actual.eip != expected_eip) {
        return false;
    }
    auto restored = actual;
    restored.eip = original.eip;
    return ContextEquals(restored, original);
}

bool ContextEqualsExceptEaxEip(
    const safetyhook::Context& actual,
    const safetyhook::Context& original,
    std::uint32_t expected_eax,
    std::uint32_t expected_eip) {
    if (actual.eax != expected_eax || actual.eip != expected_eip) {
        return false;
    }
    auto restored = actual;
    restored.eax = original.eax;
    restored.eip = original.eip;
    return ContextEquals(restored, original);
}

struct WriterState {
    std::uintptr_t renderer{};
    std::size_t offset{};
    int calls{};
    bool succeed{true};
    std::uint8_t initialized{1};
};

bool ClearInitialized(
    void* opaque,
    std::uintptr_t renderer,
    std::size_t offset) noexcept {
    auto& state = *static_cast<WriterState*>(opaque);
    ++state.calls;
    state.renderer = renderer;
    state.offset = offset;
    if (!state.succeed) {
        return false;
    }
    state.initialized = 0;
    return true;
}

struct LostResourceState {
    std::uintptr_t renderer{};
    std::size_t initialized_offset{};
    std::size_t holder_offset{};
    std::uintptr_t detached_buffer{};
    int phase{};
    int clear_calls{};
    int detach_calls{};
    int release_calls{};
    bool clear_succeeds{true};
    bool detach_succeeds{true};
    bool release_succeeds{true};
    bool release_saw_detached_state{};
};

bool ClearLostInitialized(
    void* opaque,
    std::uintptr_t renderer,
    std::size_t offset) noexcept {
    auto& state = *static_cast<LostResourceState*>(opaque);
    state.renderer = renderer;
    state.initialized_offset = offset;
    ++state.clear_calls;
    state.phase = 1;
    return state.clear_succeeds;
}

bool DetachLostIndexBuffer(
    void* opaque,
    std::uintptr_t renderer,
    std::size_t offset,
    std::uintptr_t& detached) noexcept {
    auto& state = *static_cast<LostResourceState*>(opaque);
    state.renderer = renderer;
    state.holder_offset = offset;
    ++state.detach_calls;
    if (!state.detach_succeeds || state.phase != 1) {
        return false;
    }
    state.phase = 2;
    detached = state.detached_buffer;
    return true;
}

bool ReleaseLostIndexBuffer(
    void* opaque,
    std::uintptr_t buffer) noexcept {
    auto& state = *static_cast<LostResourceState*>(opaque);
    ++state.release_calls;
    state.release_saw_detached_state =
        state.phase == 2 && buffer == state.detached_buffer;
    state.phase = 3;
    return state.release_succeeds;
}

struct StackReaderState {
    std::uintptr_t expected_address{};
    std::uintptr_t output_pair{};
    int calls{};
    bool succeed{true};
};

bool ReadStackPointer(
    void* opaque,
    std::uintptr_t address,
    std::uintptr_t& value) noexcept {
    auto& state = *static_cast<StackReaderState*>(opaque);
    ++state.calls;
    if (!state.succeed || address != state.expected_address) {
        return false;
    }
    value = state.output_pair;
    return true;
}

using gc::renderer_device_loss::RendererContractSite;
using gc::renderer_device_loss::RendererInstallActions;

struct FakeInstallState {
    std::uintptr_t image_base{
        gc::renderer_device_loss::kPreferredImageBase};
    RendererContractSite read_failure{RendererContractSite::None};
    RendererContractSite mismatch{RendererContractSite::None};
    RendererContractSite hook_failure{RendererContractSite::None};
    int reads{};
    int install_calls{};
    int reset_calls{};
    std::array<RendererContractSite, 6> hook_sites{};
    std::array<std::uintptr_t, 6> hook_addresses{};
};

RendererContractSite SiteForAddress(
    const FakeInstallState& state,
    std::uintptr_t address) noexcept {
    using namespace gc::renderer_device_loss;
    if (address == state.image_base + kDeviceLostTailRva) {
        return RendererContractSite::DeviceLostTail;
    }
    if (address == state.image_base + kVertexBufferResultRva) {
        return RendererContractSite::VertexBufferResult;
    }
    if (address == state.image_base + kIndexBufferResultRva) {
        return RendererContractSite::IndexBufferResult;
    }
    if (address ==
        state.image_base + kRendererInitializerEpilogueRva) {
        return RendererContractSite::InitializerEpilogue;
    }
    if (address == state.image_base + kVertexBufferLockGuardRva) {
        return RendererContractSite::VertexBufferLockGuard;
    }
    if (address == state.image_base + kVertexBufferLockFailureRva) {
        return RendererContractSite::VertexBufferLockFailure;
    }
    if (address == state.image_base + kDirectLockResultRva) {
        return RendererContractSite::DirectLockResult;
    }
    if (address == state.image_base + kDirectBatchCleanupRva) {
        return RendererContractSite::DirectBatchCleanup;
    }
    if (address == state.image_base + kBufferedUnlockResultRva) {
        return RendererContractSite::BufferedUnlockResult;
    }
    if (address ==
        state.image_base + kBufferedUnlockContinuationRva) {
        return RendererContractSite::BufferedUnlockContinuation;
    }
    return RendererContractSite::None;
}

std::span<const std::byte> ExpectedContractBytes(
    RendererContractSite site) noexcept {
    using namespace gc::renderer_device_loss;
    switch (site) {
    case RendererContractSite::DeviceLostTail:
        return kDeviceLostTailPattern;
    case RendererContractSite::VertexBufferResult:
        return kVertexBufferResultPattern;
    case RendererContractSite::IndexBufferResult:
        return kIndexBufferResultPattern;
    case RendererContractSite::InitializerEpilogue:
        return kRendererEpiloguePattern;
    case RendererContractSite::VertexBufferLockGuard:
        return kVertexBufferLockGuardPattern;
    case RendererContractSite::VertexBufferLockFailure:
        return kVertexBufferLockFailurePattern;
    case RendererContractSite::DirectLockResult:
        return kDirectLockResultPattern;
    case RendererContractSite::DirectBatchCleanup:
        return kDirectBatchCleanupPattern;
    case RendererContractSite::BufferedUnlockResult:
        return kBufferedUnlockResultPattern;
    case RendererContractSite::BufferedUnlockContinuation:
        return kBufferedUnlockContinuationPattern;
    case RendererContractSite::None:
        return {};
    }
    return {};
}

bool ReadInstallMemory(
    void* opaque,
    std::uintptr_t address,
    std::span<std::byte> output) noexcept {
    auto& state = *static_cast<FakeInstallState*>(opaque);
    const auto site = SiteForAddress(state, address);
    ++state.reads;
    if (site == RendererContractSite::None ||
        site == state.read_failure) {
        return false;
    }

    const auto source = ExpectedContractBytes(site);
    if (source.empty() || output.size() != source.size()) {
        return false;
    }
    std::copy(source.begin(), source.end(), output.begin());
    if (site == state.mismatch) {
        output.front() ^= std::byte{0xFF};
    }
    return true;
}

bool InstallHook(
    void* opaque,
    RendererContractSite site,
    std::uintptr_t address) noexcept {
    auto& state = *static_cast<FakeInstallState*>(opaque);
    const auto index = state.install_calls++;
    if (index < static_cast<int>(state.hook_sites.size())) {
        state.hook_sites[static_cast<std::size_t>(index)] = site;
        state.hook_addresses[static_cast<std::size_t>(index)] = address;
    }
    return site != state.hook_failure;
}

void ResetHook(void* opaque) noexcept {
    ++static_cast<FakeInstallState*>(opaque)->reset_calls;
}

RendererInstallActions InstallActions(
    FakeInstallState& state) noexcept {
    return {
        .context = &state,
        .read = ReadInstallMemory,
        .install_hook = InstallHook,
        .reset_hook = ResetHook,
    };
}

} // namespace

int main() {
    using namespace gc::renderer_device_loss;
    int failures = 0;

    auto failed = CanaryContext();
    failed.eax = 0x88760868U;
    failed.esi = 0x12345000U;
    const auto failed_before = failed;
    WriterState writer{};
    const bool deferred = ApplyRendererDeviceLossRetry(
        failed,
        kPreferredImageBase,
        {
            .context = &writer,
            .clear_initialized = ClearInitialized,
        });
    failures += Expect(
        deferred && writer.calls == 1 && writer.initialized == 0 &&
            writer.renderer == failed_before.esi &&
            writer.offset == kRendererInitializedOffset &&
            failed.eip ==
                kPreferredImageBase + kRendererInitializerEpilogueRva &&
            ContextEqualsExceptEip(
                failed,
                failed_before,
                kPreferredImageBase + kRendererInitializerEpilogueRva),
        "failed vertex-buffer creation defers initialization");

    auto lost = CanaryContext();
    lost.esi = 0x15B61190U;
    const auto lost_before = lost;
    LostResourceState resources{.detached_buffer = 0x12345678U};
    failures += Expect(
        ApplyRendererDeviceLostCleanup(
            lost,
            {
                .context = &resources,
                .clear_initialized = ClearLostInitialized,
                .detach_index_buffer = DetachLostIndexBuffer,
                .release_index_buffer = ReleaseLostIndexBuffer,
            }) &&
            resources.clear_calls == 1 &&
            resources.detach_calls == 1 &&
            resources.release_calls == 1 &&
            resources.renderer == lost.esi &&
            resources.initialized_offset ==
                kRendererInitializedOffset &&
            resources.holder_offset ==
                kRendererIndexBufferHolderOffset &&
            resources.release_saw_detached_state &&
            ContextEquals(lost, lost_before),
        "OnLost detaches before releasing the default-pool index buffer");

    auto already_empty = lost_before;
    LostResourceState no_buffer{};
    failures += Expect(
        ApplyRendererDeviceLostCleanup(
            already_empty,
            {
                .context = &no_buffer,
                .clear_initialized = ClearLostInitialized,
                .detach_index_buffer = DetachLostIndexBuffer,
                .release_index_buffer = ReleaseLostIndexBuffer,
            }) &&
            no_buffer.clear_calls == 1 &&
            no_buffer.detach_calls == 1 &&
            no_buffer.release_calls == 0 &&
            ContextEquals(already_empty, lost_before),
        "OnLost accepts an already-null index buffer");

    auto clear_rejected = lost_before;
    LostResourceState failed_clear{
        .detached_buffer = 0x12345678U,
        .clear_succeeds = false,
    };
    failures += Expect(
        !ApplyRendererDeviceLostCleanup(
            clear_rejected,
            {
                .context = &failed_clear,
                .clear_initialized = ClearLostInitialized,
                .detach_index_buffer = DetachLostIndexBuffer,
                .release_index_buffer = ReleaseLostIndexBuffer,
            }) &&
            failed_clear.clear_calls == 1 &&
            failed_clear.detach_calls == 0 &&
            failed_clear.release_calls == 0 &&
            ContextEquals(clear_rejected, lost_before),
        "failed initialized-state write prevents unsafe teardown");

    auto detach_rejected = lost_before;
    LostResourceState failed_detach{
        .detached_buffer = 0x12345678U,
        .detach_succeeds = false,
    };
    failures += Expect(
        !ApplyRendererDeviceLostCleanup(
            detach_rejected,
            {
                .context = &failed_detach,
                .clear_initialized = ClearLostInitialized,
                .detach_index_buffer = DetachLostIndexBuffer,
                .release_index_buffer = ReleaseLostIndexBuffer,
            }) &&
            failed_detach.clear_calls == 1 &&
            failed_detach.detach_calls == 1 &&
            failed_detach.release_calls == 0 &&
            ContextEquals(detach_rejected, lost_before),
        "failed holder access never releases an unknown pointer");

    auto release_rejected = lost_before;
    LostResourceState failed_release{
        .detached_buffer = 0x12345678U,
        .release_succeeds = false,
    };
    failures += Expect(
        !ApplyRendererDeviceLostCleanup(
            release_rejected,
            {
                .context = &failed_release,
                .clear_initialized = ClearLostInitialized,
                .detach_index_buffer = DetachLostIndexBuffer,
                .release_index_buffer = ReleaseLostIndexBuffer,
            }) &&
            failed_release.clear_calls == 1 &&
            failed_release.detach_calls == 1 &&
            failed_release.release_calls == 1 &&
            failed_release.release_saw_detached_state &&
            ContextEquals(release_rejected, lost_before),
        "release failure cannot restore a detached index-buffer pointer");

    auto null_renderer = lost_before;
    null_renderer.esi = 0;
    const auto null_renderer_before = null_renderer;
    LostResourceState untouched_resources{};
    failures += Expect(
        !ApplyRendererDeviceLostCleanup(
            null_renderer,
            {
                .context = &untouched_resources,
                .clear_initialized = ClearLostInitialized,
                .detach_index_buffer = DetachLostIndexBuffer,
                .release_index_buffer = ReleaseLostIndexBuffer,
            }) &&
            untouched_resources.clear_calls == 0 &&
            untouched_resources.detach_calls == 0 &&
            untouched_resources.release_calls == 0 &&
            ContextEquals(null_renderer, null_renderer_before),
        "null renderer state is rejected without side effects");

    auto empty_draw = CanaryContext();
    empty_draw.ecx = 0;
    empty_draw.edi = 0;
    empty_draw.ebx = 0;
    empty_draw.esp = 0x001AF4B4U;
    const auto empty_draw_before = empty_draw;
    StackReaderState stack_reader{
        .expected_address =
            empty_draw.esp + kVertexBufferLockOutputStackOffset,
        .output_pair = 0x001AF500U,
    };
    failures += Expect(
        ApplyRendererDeviceLossDrawSkip(
            empty_draw,
            kPreferredImageBase,
            {
                .context = &stack_reader,
                .read_pointer = ReadStackPointer,
            }) &&
            stack_reader.calls == 1 &&
            ContextEqualsExceptEaxEip(
                empty_draw,
                empty_draw_before,
                static_cast<std::uint32_t>(stack_reader.output_pair),
                kPreferredImageBase + kVertexBufferLockFailureRva),
        "empty vertex-buffer draw uses the native lock-failure return");

    for (const auto [buffer_count, buffer_index] :
         std::array{
             std::pair{1U, 0U},
             std::pair{1U, 1U},
             std::pair{0U, 1U},
         }) {
        auto other_draw = CanaryContext();
        other_draw.ecx = buffer_count;
        other_draw.edi = buffer_index;
        other_draw.ebx = 0;
        const auto before = other_draw;
        StackReaderState untouched{};
        failures += Expect(
            !ApplyRendererDeviceLossDrawSkip(
                other_draw,
                kPreferredImageBase,
                {
                    .context = &untouched,
                    .read_pointer = ReadStackPointer,
                }) &&
                untouched.calls == 0 &&
                ContextEquals(other_draw, before),
            "non-empty or nonzero-index draw preserves native context");
    }

    auto unreadable_draw = empty_draw_before;
    const auto unreadable_draw_before = unreadable_draw;
    StackReaderState unreadable_stack{
        .expected_address =
            unreadable_draw.esp + kVertexBufferLockOutputStackOffset,
        .output_pair = 0x001AF500U,
        .succeed = false,
    };
    failures += Expect(
        !ApplyRendererDeviceLossDrawSkip(
            unreadable_draw,
            kPreferredImageBase,
            {
                .context = &unreadable_stack,
                .read_pointer = ReadStackPointer,
            }) &&
            unreadable_stack.calls == 1 &&
            ContextEquals(unreadable_draw, unreadable_draw_before),
        "unreadable lock output preserves native context");

    constexpr std::array<std::uint32_t, 3> nonnegative_results{
        0U,
        1U,
        0x7FFFFFFFU,
    };
    for (const auto result : nonnegative_results) {
        auto success = CanaryContext();
        success.eax = result;
        const auto before = success;
        WriterState untouched{};
        failures += Expect(
            !ApplyRendererDeviceLossRetry(
                success,
                kPreferredImageBase,
                {
                    .context = &untouched,
                    .clear_initialized = ClearInitialized,
                }) &&
                untouched.calls == 0 && ContextEquals(success, before),
            "nonnegative HRESULT preserves native context");
    }

    auto rejected = CanaryContext();
    rejected.eax = 0x80004005U;
    const auto rejected_before = rejected;
    WriterState failing_writer{.succeed = false};
    failures += Expect(
        !ApplyRendererDeviceLossRetry(
            rejected,
            kPreferredImageBase,
            {
                .context = &failing_writer,
                .clear_initialized = ClearInitialized,
            }) &&
            failing_writer.calls == 1 &&
            ContextEquals(rejected, rejected_before),
        "failed guarded write preserves native path");

    auto missing_renderer = CanaryContext();
    missing_renderer.eax = 0x88760868U;
    missing_renderer.esi = 0;
    const auto missing_renderer_before = missing_renderer;
    WriterState unused_writer{};
    failures += Expect(
        !ApplyRendererDeviceLossRetry(
            missing_renderer,
            kPreferredImageBase,
            {
                .context = &unused_writer,
                .clear_initialized = ClearInitialized,
            }) &&
            unused_writer.calls == 0 &&
            ContextEquals(missing_renderer, missing_renderer_before),
        "missing renderer preserves native path");

    constexpr std::uint32_t kExpectedDirectBatchCleanup =
        0x004E6AD6U;
    constexpr std::uint32_t kExpectedBufferedUnlockContinuation =
        0x004E5679U;

    auto direct_lock_failure = CanaryContext();
    direct_lock_failure.eax = 0x88760868U;
    const auto direct_before = direct_lock_failure;
    failures += Expect(
        ApplyRendererDeviceLossDirectLockSkip(
            direct_lock_failure,
            kPreferredImageBase) &&
            ContextEqualsExceptEip(
                direct_lock_failure,
                direct_before,
                kExpectedDirectBatchCleanup),
        "failed direct Lock skips unchecked geometry copy");

    auto unlock_failure = CanaryContext();
    unlock_failure.eax = 0x88760868U;
    const auto unlock_before = unlock_failure;
    failures += Expect(
        ApplyRendererDeviceLossUnlockCompletion(
            unlock_failure,
            kPreferredImageBase) &&
            ContextEqualsExceptEip(
                unlock_failure,
                unlock_before,
                kExpectedBufferedUnlockContinuation),
        "failed buffered Unlock completes native batch state");

    for (const auto result : nonnegative_results) {
        auto direct_success = CanaryContext();
        direct_success.eax = result;
        const auto direct_success_before = direct_success;
        failures += Expect(
            !ApplyRendererDeviceLossDirectLockSkip(
                direct_success,
                kPreferredImageBase) &&
                ContextEquals(
                    direct_success,
                    direct_success_before),
            "nonnegative direct Lock preserves native context");

        auto unlock_success = CanaryContext();
        unlock_success.eax = result;
        const auto unlock_success_before = unlock_success;
        failures += Expect(
            !ApplyRendererDeviceLossUnlockCompletion(
                unlock_success,
                kPreferredImageBase) &&
                ContextEquals(
                    unlock_success,
                    unlock_success_before),
            "nonnegative buffered Unlock preserves native context");
    }

    auto wrong_base_direct = direct_before;
    failures += Expect(
        !ApplyRendererDeviceLossDirectLockSkip(
            wrong_base_direct,
            kPreferredImageBase + 0x1000U) &&
            ContextEquals(wrong_base_direct, direct_before),
        "direct Lock redirect rejects an unexpected image base");

    auto wrong_base_unlock = unlock_before;
    failures += Expect(
        !ApplyRendererDeviceLossUnlockCompletion(
            wrong_base_unlock,
            kPreferredImageBase + 0x1000U) &&
            ContextEquals(wrong_base_unlock, unlock_before),
        "buffered Unlock redirect rejects an unexpected image base");

    FakeInstallState valid{};
    const auto installed = InstallRendererDeviceLossPatch(
        kPreferredImageBase,
        InstallActions(valid));
    failures += Expect(
        installed.has_value() && valid.reads == 10 &&
            valid.install_calls == 6 && valid.reset_calls == 0 &&
            valid.hook_sites[0] ==
                RendererContractSite::DeviceLostTail &&
            valid.hook_addresses[0] ==
                kPreferredImageBase + kDeviceLostTailRva &&
            valid.hook_sites[1] ==
                RendererContractSite::VertexBufferResult &&
            valid.hook_addresses[1] ==
                kPreferredImageBase + kVertexBufferResultRva &&
            valid.hook_sites[2] ==
                RendererContractSite::IndexBufferResult &&
            valid.hook_addresses[2] ==
                kPreferredImageBase + kIndexBufferResultRva &&
            valid.hook_sites[3] ==
                RendererContractSite::VertexBufferLockGuard &&
            valid.hook_addresses[3] ==
                kPreferredImageBase + kVertexBufferLockGuardRva &&
            valid.hook_sites[4] ==
                RendererContractSite::DirectLockResult &&
            valid.hook_addresses[4] == 0x004E691EU &&
            valid.hook_sites[5] ==
                RendererContractSite::BufferedUnlockResult &&
            valid.hook_addresses[5] == 0x004E5662U,
        "matching contracts install all six recovery hooks");

    FakeInstallState wrong_base{};
    const auto unsupported = InstallRendererDeviceLossPatch(
        kPreferredImageBase + 0x1000U,
        InstallActions(wrong_base));
    failures += Expect(
        !unsupported &&
            unsupported.error().stage ==
                RendererInstallStage::UnexpectedImageBase &&
            unsupported.error().site == RendererContractSite::None &&
            wrong_base.reads == 0 && wrong_base.install_calls == 0 &&
            wrong_base.reset_calls == 0,
        "unexpected image base is rejected before preflight");

    for (int missing = 0; missing < 4; ++missing) {
        FakeInstallState invalid{};
        auto actions = InstallActions(invalid);
        if (missing == 0) {
            actions.context = nullptr;
        } else if (missing == 1) {
            actions.read = nullptr;
        } else if (missing == 2) {
            actions.install_hook = nullptr;
        } else {
            actions.reset_hook = nullptr;
        }
        const auto rejected_install = InstallRendererDeviceLossPatch(
            kPreferredImageBase,
            actions);
        failures += Expect(
            !rejected_install &&
                rejected_install.error().stage ==
                    RendererInstallStage::InvalidActions &&
                rejected_install.error().site ==
                    RendererContractSite::None &&
                invalid.reads == 0 && invalid.install_calls == 0 &&
                invalid.reset_calls == 0,
            "invalid install actions have no side effects");
    }

    constexpr std::array contract_sites{
        RendererContractSite::DeviceLostTail,
        RendererContractSite::VertexBufferResult,
        RendererContractSite::IndexBufferResult,
        RendererContractSite::InitializerEpilogue,
        RendererContractSite::VertexBufferLockGuard,
        RendererContractSite::VertexBufferLockFailure,
        RendererContractSite::DirectLockResult,
        RendererContractSite::DirectBatchCleanup,
        RendererContractSite::BufferedUnlockResult,
        RendererContractSite::BufferedUnlockContinuation,
    };
    for (const auto site : contract_sites) {
        FakeInstallState unreadable{.read_failure = site};
        const auto read_failure = InstallRendererDeviceLossPatch(
            kPreferredImageBase,
            InstallActions(unreadable));
        failures += Expect(
            !read_failure &&
                read_failure.error().stage ==
                    RendererInstallStage::PreflightRead &&
                read_failure.error().site == site &&
                unreadable.install_calls == 0 &&
                unreadable.reset_calls == 0,
            "unreadable contract prevents hook installation");

        FakeInstallState mismatched{.mismatch = site};
        const auto mismatch = InstallRendererDeviceLossPatch(
            kPreferredImageBase,
            InstallActions(mismatched));
        failures += Expect(
            !mismatch &&
                mismatch.error().stage ==
                    RendererInstallStage::PreflightMismatch &&
                mismatch.error().site == site &&
                mismatched.install_calls == 0 &&
                mismatched.reset_calls == 0,
            "mismatched contract prevents hook installation");
    }

    constexpr std::array hook_sites{
        RendererContractSite::DeviceLostTail,
        RendererContractSite::VertexBufferResult,
        RendererContractSite::IndexBufferResult,
        RendererContractSite::VertexBufferLockGuard,
        RendererContractSite::DirectLockResult,
        RendererContractSite::BufferedUnlockResult,
    };
    for (std::size_t index = 0; index < hook_sites.size(); ++index) {
        const auto site = hook_sites[index];
        FakeInstallState failed_hook{.hook_failure = site};
        const auto hook_failure = InstallRendererDeviceLossPatch(
            kPreferredImageBase,
            InstallActions(failed_hook));
        const auto expected_install_calls =
            static_cast<int>(index + 1);
        failures += Expect(
            !hook_failure &&
                hook_failure.error().stage ==
                    RendererInstallStage::HookInstall &&
                hook_failure.error().site == site &&
                failed_hook.reads == 10 &&
                failed_hook.install_calls == expected_install_calls &&
                failed_hook.reset_calls == 1,
            "hook creation failure resets the six-hook transaction");
    }

    return failures == 0 ? 0 : 1;
}
