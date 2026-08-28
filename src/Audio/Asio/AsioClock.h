#pragma once
// SPDX-License-Identifier: CC0-1.0

#include "Audio/Asio/AsioLogicalTimeline.h"
#include "Audio/Asio/AsioSubmittedOutputTail.h"
#include "Audio/Mixer/PresentedOutputClock.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>

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

    struct AsioClockNowActions
    {
        void* context{};
        std::uint32_t (*time_get_time_ms)(void*) noexcept{};
    };

    class AsioPresentedClockPublication final
        : public IPresentedOutputClock
    {
    public:
        AsioPresentedClockPublication(
            AsioClockNowActions actions,
            std::shared_ptr<const AsioLogicalTimeline> timeline,
            std::shared_ptr<const AsioSubmittedOutputTail> submitted_tail) noexcept;

        void PublishPhysicalAnchor(
            std::uint64_t presented_output_frame,
            std::uint64_t submitted_output_tail,
            std::uint64_t system_time_ns) noexcept;
        void PublishLogicalAnchor(
            std::uint64_t presented_output_frame,
            std::uint64_t submitted_output_tail,
            std::uint32_t multimedia_time_ms) noexcept;

        [[nodiscard]] std::optional<std::uint64_t>
        CurrentOutputFrame() noexcept override;
        void Invalidate() noexcept override;

    private:
        struct Anchor final
        {
            std::uint64_t presented_output_frame{};
            std::uint64_t submitted_output_tail{};
            std::uint32_t multimedia_time_ms{};
        };

        void StoreAnchor(const Anchor& anchor) noexcept;
        [[nodiscard]] std::optional<Anchor> ReadStable() const noexcept;
        [[nodiscard]] std::optional<std::uint64_t>
        LastReturned() const noexcept;
        std::uint64_t RememberMonotonic(std::uint64_t frame) noexcept;

        AsioClockNowActions actions_{};
        std::shared_ptr<const AsioLogicalTimeline> timeline_;
        std::shared_ptr<const AsioSubmittedOutputTail> submitted_tail_;
        std::atomic_bool invalidated_{};
        std::atomic_uint64_t anchor_version_{};
        std::atomic_uint64_t anchor_presented_output_frame_{};
        std::atomic_uint64_t anchor_submitted_output_tail_{};
        std::atomic_uint32_t anchor_multimedia_time_ms_{};
        std::atomic_bool anchor_available_{};
        std::uint64_t writer_version_{};
        std::atomic_uint64_t last_returned_{};
        std::atomic_bool has_last_returned_{};
    };
} // namespace gc::audio
