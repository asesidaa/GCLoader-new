// SPDX-License-Identifier: CC0-1.0

#include "Audio/Wasapi/WasapiPresentedOutputClock.h"

#include <Windows.h>

#include <cstdint>

namespace gc::audio {
namespace {

bool QueryProductionPerformanceCounter(
    void*,
    std::uint64_t* ticks) noexcept
{
    if (ticks == nullptr)
    {
        return false;
    }
    LARGE_INTEGER value{};
    if (!QueryPerformanceCounter(&value) || value.QuadPart < 0)
    {
        return false;
    }
    *ticks = static_cast<std::uint64_t>(value.QuadPart);
    return true;
}

} // namespace

WasapiPresentedOutputClockActions
ProductionWasapiPresentedOutputClockActions() noexcept
{
    LARGE_INTEGER frequency{};
    const std::uint64_t cached_frequency =
        QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0
        ? static_cast<std::uint64_t>(frequency.QuadPart)
        : 0;
    return {
        nullptr,
        &QueryProductionPerformanceCounter,
        cached_frequency,
    };
}

WasapiPresentedOutputClock::WasapiPresentedOutputClock(
    std::uint32_t output_sample_rate,
    const WasapiPresentedOutputClockActions& actions) noexcept
    : output_sample_rate_(output_sample_rate),
      actions_(actions)
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
    std::uint64_t now_qpc_ticks{};
    std::uint64_t frequency{};
    if (actions_.query_performance_counter != nullptr &&
        actions_.query_performance_counter(
            actions_.context,
            &now_qpc_ticks))
    {
        frequency = actions_.qpc_frequency;
    }
    return publication_.Project(
        now_qpc_ticks,
        frequency,
        output_sample_rate_);
}

void WasapiPresentedOutputClock::Invalidate() noexcept
{
    publication_.Invalidate();
}

} // namespace gc::audio
