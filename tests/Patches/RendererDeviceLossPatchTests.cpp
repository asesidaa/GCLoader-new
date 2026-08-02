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
    std::array<std::byte, 7> result_bytes{
        gc::renderer_device_loss::kVertexBufferResultPattern};
    std::array<std::byte, 7> epilogue_bytes{
        gc::renderer_device_loss::kRendererEpiloguePattern};
    std::array<std::byte, 9> lock_guard_bytes{
        gc::renderer_device_loss::kVertexBufferLockGuardPattern};
    std::array<std::byte, 12> lock_failure_bytes{
        gc::renderer_device_loss::kVertexBufferLockFailurePattern};
    RendererContractSite read_failure{RendererContractSite::None};
    RendererContractSite mismatch{RendererContractSite::None};
    RendererContractSite hook_failure{RendererContractSite::None};
    int reads{};
    int install_calls{};
    int reset_calls{};
    std::array<RendererContractSite, 2> hook_sites{};
    std::array<std::uintptr_t, 2> hook_addresses{};
};

RendererContractSite SiteForAddress(
    const FakeInstallState& state,
    std::uintptr_t address) noexcept {
    using namespace gc::renderer_device_loss;
    if (address == state.image_base + kVertexBufferResultRva) {
        return RendererContractSite::VertexBufferResult;
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
    return RendererContractSite::None;
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

    std::span<const std::byte> source;
    switch (site) {
    case RendererContractSite::VertexBufferResult:
        source = state.result_bytes;
        break;
    case RendererContractSite::InitializerEpilogue:
        source = state.epilogue_bytes;
        break;
    case RendererContractSite::VertexBufferLockGuard:
        source = state.lock_guard_bytes;
        break;
    case RendererContractSite::VertexBufferLockFailure:
        source = state.lock_failure_bytes;
        break;
    case RendererContractSite::None:
        return false;
    }
    if (output.size() != source.size()) {
        return false;
    }
    std::copy(source.begin(), source.end(), output.begin());
    if (site == state.mismatch) {
        output.front() ^= std::byte{0x01};
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

    FakeInstallState valid{};
    const auto installed = InstallRendererDeviceLossPatch(
        kPreferredImageBase,
        InstallActions(valid));
    failures += Expect(
        installed.has_value() && valid.reads == 4 &&
            valid.install_calls == 2 && valid.reset_calls == 0 &&
            valid.hook_sites[0] ==
                RendererContractSite::VertexBufferResult &&
            valid.hook_addresses[0] ==
                kPreferredImageBase + kVertexBufferResultRva &&
            valid.hook_sites[1] ==
                RendererContractSite::VertexBufferLockGuard &&
            valid.hook_addresses[1] ==
                kPreferredImageBase + kVertexBufferLockGuardRva,
        "matching contracts install result and draw-lock hooks");

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
        RendererContractSite::VertexBufferResult,
        RendererContractSite::InitializerEpilogue,
        RendererContractSite::VertexBufferLockGuard,
        RendererContractSite::VertexBufferLockFailure,
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
        RendererContractSite::VertexBufferResult,
        RendererContractSite::VertexBufferLockGuard,
    };
    for (const auto site : hook_sites) {
        FakeInstallState failed_hook{.hook_failure = site};
        const auto hook_failure = InstallRendererDeviceLossPatch(
            kPreferredImageBase,
            InstallActions(failed_hook));
        const int expected_install_calls =
            site == RendererContractSite::VertexBufferResult ? 1 : 2;
        failures += Expect(
            !hook_failure &&
                hook_failure.error().stage ==
                    RendererInstallStage::HookInstall &&
                hook_failure.error().site == site &&
                failed_hook.reads == 4 &&
                failed_hook.install_calls == expected_install_calls &&
                failed_hook.reset_calls == 1,
            "hook creation failure resets the two-hook transaction");
    }

    return failures == 0 ? 0 : 1;
}
