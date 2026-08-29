// SPDX-License-Identifier: CC0-1.0

#include "Audio/Asio/AsioClock.h"

#include <limits>

namespace gc::audio
{
    void AsioClockTracker::Reset(
        const std::uint32_t buffer_frames,
        const std::uint32_t output_latency_frames) noexcept
    {
        buffer_frames_ = buffer_frames;
        output_latency_frames_ = output_latency_frames;
        previous_sample_position_ = 0;
        has_previous_sample_position_ = false;
        configured_ = buffer_frames != 0;
        faulted_ = !configured_;
    }

    AsioClockDecision AsioClockTracker::Fault() noexcept
    {
        faulted_ = true;
        return {};
    }

    AsioClockDecision AsioClockTracker::Observe(
        const std::uint64_t sample_position) noexcept
    {
        if (!configured_ || faulted_)
        {
            return Fault();
        }

        if (has_previous_sample_position_)
        {
            if (sample_position <= previous_sample_position_ ||
                sample_position - previous_sample_position_ != buffer_frames_)
            {
                return Fault();
            }
        }

        if (sample_position >
            std::numeric_limits<std::uint64_t>::max() -
            output_latency_frames_)
        {
            return Fault();
        }

        previous_sample_position_ = sample_position;
        has_previous_sample_position_ = true;

        return {
            .kind = AsioClockDecisionKind::valid,
            .presented_output_frame = sample_position,
            .render_output_frame_begin =
            sample_position + output_latency_frames_,
        };
    }
} // namespace gc::audio
