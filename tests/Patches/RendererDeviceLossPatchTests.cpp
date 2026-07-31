#include "Patches/RendererDeviceLoss/RendererDeviceLossPatch.h"

#include <safetyhook.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <span>

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

using gc::renderer_device_loss::RendererContractSite;
using gc::renderer_device_loss::RendererInstallActions;

struct FakeInstallState {
    std::uintptr_t image_base{
        gc::renderer_device_loss::kPreferredImageBase};
    std::array<std::byte, 7> result_bytes{
        gc::renderer_device_loss::kVertexBufferResultPattern};
    std::array<std::byte, 7> epilogue_bytes{
        gc::renderer_device_loss::kRendererEpiloguePattern};
    RendererContractSite read_failure{RendererContractSite::None};
    RendererContractSite mismatch{RendererContractSite::None};
    bool install_succeeds{true};
    int reads{};
    int install_calls{};
    int reset_calls{};
    std::uintptr_t hook_address{};
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

    const auto source =
        site == RendererContractSite::VertexBufferResult
        ? std::span<const std::byte>{state.result_bytes}
        : std::span<const std::byte>{state.epilogue_bytes};
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
    std::uintptr_t address) noexcept {
    auto& state = *static_cast<FakeInstallState*>(opaque);
    ++state.install_calls;
    state.hook_address = address;
    return state.install_succeeds;
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
        installed.has_value() && valid.reads == 2 &&
            valid.install_calls == 1 && valid.reset_calls == 0 &&
            valid.hook_address ==
                kPreferredImageBase + kVertexBufferResultRva,
        "matching contracts install one result hook");

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

    FakeInstallState failed_hook{.install_succeeds = false};
    const auto hook_failure = InstallRendererDeviceLossPatch(
        kPreferredImageBase,
        InstallActions(failed_hook));
    failures += Expect(
        !hook_failure &&
            hook_failure.error().stage ==
                RendererInstallStage::HookInstall &&
            hook_failure.error().site ==
                RendererContractSite::VertexBufferResult &&
            failed_hook.reads == 2 && failed_hook.install_calls == 1 &&
            failed_hook.reset_calls == 1,
        "hook creation failure resets unpublished hook state");

    return failures == 0 ? 0 : 1;
}
