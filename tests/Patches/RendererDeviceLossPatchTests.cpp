#include "Patches/RendererDeviceLoss/RendererDeviceLossPatch.h"

#include <safetyhook.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>

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

    return failures == 0 ? 0 : 1;
}
