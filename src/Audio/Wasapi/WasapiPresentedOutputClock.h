#pragma once
// SPDX-License-Identifier: CC0-1.0

#include "Audio/Mixer/AudioCursorTimeline.h"
#include "Audio/Mixer/PresentedOutputClock.h"

#include <cstdint>
#include <optional>

namespace gc::audio {

class WasapiPresentedOutputClock final : public IPresentedOutputClock
{
public:
    WasapiPresentedOutputClock(
        std::uint32_t output_sample_rate, std::uint64_t qpc_frequency) noexcept;

    void Publish(
        std::uint64_t presented_output_frame,
        std::uint64_t sample_qpc_100ns,
        std::uint64_t submitted_output_frame_end) noexcept;
    [[nodiscard]] std::optional<std::uint64_t>
        CurrentOutputFrame() noexcept override;
    void Invalidate() noexcept override;

private:
    std::uint32_t output_sample_rate_{};
    std::uint64_t qpc_frequency_{};
    PresentedClockPublication publication_;
};

} // namespace gc::audio
