// SPDX-License-Identifier: CC0-1.0

#include "Audio/Wasapi/WasapiPresentedOutputClock.h"

#include <Windows.h>

#include <cstdint>

namespace gc::audio {
WasapiPresentedOutputClock::WasapiPresentedOutputClock(
    std::uint32_t output_sample_rate, std::uint64_t qpc_frequency) noexcept
    : output_sample_rate_(output_sample_rate), qpc_frequency_(qpc_frequency)
{
}

void WasapiPresentedOutputClock::Publish(
    std::uint64_t presented_output_frame,
    std::uint64_t sample_qpc_100ns,
    std::uint64_t submitted_output_frame_end) noexcept
{
    publication_.Publish(
        presented_output_frame,
        sample_qpc_100ns,
        submitted_output_frame_end);
}

std::optional<std::uint64_t>
WasapiPresentedOutputClock::CurrentOutputFrame() noexcept
{
    LARGE_INTEGER now{};
    std::uint64_t now_qpc_ticks{};
    std::uint64_t frequency{};
    if (QueryPerformanceCounter(&now) && now.QuadPart >= 0)
    {
        now_qpc_ticks = static_cast<std::uint64_t>(now.QuadPart);
        frequency = qpc_frequency_;
    }
    return publication_.Project(now_qpc_ticks, frequency, output_sample_rate_);
}

void WasapiPresentedOutputClock::Invalidate() noexcept
{
    publication_.Invalidate();
}

} // namespace gc::audio
