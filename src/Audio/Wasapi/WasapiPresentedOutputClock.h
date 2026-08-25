#pragma once
// SPDX-License-Identifier: CC0-1.0

#include "Audio/Mixer/AudioCursorTimeline.h"
#include "Audio/Mixer/PresentedOutputClock.h"

#include <cstdint>
#include <optional>

namespace gc::audio {

struct WasapiPresentedOutputClockActions
{
    void* context{};
    bool (*query_performance_counter)(
        void* context,
        std::uint64_t* ticks) noexcept{};
    std::uint64_t qpc_frequency{};
};

[[nodiscard]] WasapiPresentedOutputClockActions
ProductionWasapiPresentedOutputClockActions() noexcept;

class WasapiPresentedOutputClock final : public IPresentedOutputClock
{
public:
    WasapiPresentedOutputClock(
        std::uint32_t output_sample_rate,
        const WasapiPresentedOutputClockActions& actions) noexcept;

    void Publish(
        std::uint64_t presented_output_frame,
        std::uint64_t sample_qpc_100ns,
        std::uint64_t submitted_output_frame_end) noexcept;
    [[nodiscard]] std::optional<std::uint64_t>
        CurrentOutputFrame() noexcept override;
    void Invalidate() noexcept override;

private:
    std::uint32_t output_sample_rate_{};
    WasapiPresentedOutputClockActions actions_{};
    PresentedClockPublication publication_;
};

} // namespace gc::audio
