#include "Patches/Countdown/CountdownTimerFreeze.h"

#include <cstring>
#include <optional>

namespace gc::timer_freeze {
namespace {

runtime_image::BytePatch PatchFor(const CountdownDeltaPatchSite& site) noexcept {
    runtime_image::BytePattern original{};
    original.size = kCountdownDeltaCallPatchSize;
    original.bytes[0] = std::byte{0xE8};
    const auto displacement = static_cast<std::int32_t>(
        kRvaGlobalFrameDeltaSeconds - site.return_rva);
    std::memcpy(original.bytes.data() + 1, &displacement, sizeof(displacement));
    return {
        .identity = {"Countdown", "delta_call", static_cast<runtime_image::Rva>(site.call_rva)},
        .original = original,
        .replacement = runtime_image::PatternOf<0xD9, 0xEE, 0x90, 0x90, 0x90>(),
    };
}

} // namespace

std::expected<void, runtime_image::RuntimeImageError> InstallCountdownTimerFreeze(
    const runtime_image::RuntimeImage& image, bool enabled) noexcept {
    if (!enabled) {
        return {};
    }

    std::optional<runtime_image::BytePatchState> first_state;
    for (const auto& site : kCountdownDeltaPatchSites) {
        const auto patch = PatchFor(site);
        const auto state = image.Inspect(patch);
        if (!state) {
            return std::unexpected(state.error());
        }
        if (*state == runtime_image::BytePatchState::mismatch ||
            (first_state && *state != *first_state)) {
            const auto observed = image.Read(patch.identity, patch.original.size);
            if (!observed) {
                return std::unexpected(observed.error());
            }
            return std::unexpected(runtime_image::RuntimeImageError{
                .stage = runtime_image::MemoryStage::read,
                .identity = patch.identity,
                .address = image.base() + patch.identity.rva,
                .size = patch.original.size,
                .expected = first_state == runtime_image::BytePatchState::installed
                    ? patch.replacement : patch.original,
                .observed = *observed,
                .win32_error = ERROR_INVALID_DATA,
            });
        }
        first_state = *state;
    }
    if (first_state == runtime_image::BytePatchState::installed) {
        return {};
    }
    for (const auto& site : kCountdownDeltaPatchSites) {
        const auto patch = PatchFor(site);
        if (const auto written = image.Write(
                patch.identity, patch.replacement, patch.memory_kind); !written) {
            return std::unexpected(written.error());
        }
    }
    return {};
}

} // namespace gc::timer_freeze
