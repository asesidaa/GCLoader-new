#pragma once
// SPDX-License-Identifier: CC0-1.0

#include <cstdint>

namespace gc::audio
{
    enum class AsioClockDecisionKind : std::uint8_t
    {
        valid,
        invalid,
    };

    struct AsioClockDecision
    {
        AsioClockDecisionKind kind{AsioClockDecisionKind::invalid};
        std::uint64_t presented_output_frame{};
        std::uint64_t render_output_frame_begin{};
    };

    class AsioClockTracker final
    {
    public:
        void Reset(
            std::uint32_t buffer_frames,
            std::uint32_t output_latency_frames) noexcept;
        [[nodiscard]] AsioClockDecision Observe(
            std::uint64_t sample_position) noexcept;

    private:
        [[nodiscard]] AsioClockDecision Fault() noexcept;

        std::uint32_t buffer_frames_{};
        std::uint32_t output_latency_frames_{};
        std::uint64_t previous_sample_position_{};
        bool has_previous_sample_position_{};
        bool configured_{};
        bool faulted_{};
    };
} // namespace gc::audio
