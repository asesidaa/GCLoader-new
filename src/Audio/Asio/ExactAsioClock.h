#pragma once

#include "Audio/ExactOutputClock.h"

#include <atomic>
// ReSharper disable once CppUnusedIncludeDirective
#include <cstddef>
#include <cstdint>
#include <memory>

namespace gc::audio
{
    struct ExactAsioAnchor final
    {
        std::uint64_t sequence{};
        std::uint64_t endpoint_generation{};
        std::uint64_t presented_output_frame{};
        std::uint64_t system_time_ns{};
        std::uint64_t submitted_output_tail{};
    };

    class ExactAsioClock final : public ExactOutputClock
    {
    public:
        ~ExactAsioClock() override;

        [[nodiscard]] static std::shared_ptr<ExactAsioClock> Create(std::uint64_t endpoint_generation,
                                                                    std::uint32_t output_sample_rate,
                                                                    std::int64_t qpc_frequency,
                                                                    std::uint32_t period_frames,
                                                                    std::uint32_t output_latency_frames) noexcept;

        [[nodiscard]] bool Publish(const ExactAsioAnchor& anchor) noexcept;
        [[nodiscard]] ExactOutputClockResult Resolve(const gc::timing::AbsoluteHostTime& timestamp,
                                                     ExactClockResolveIntent intent) const noexcept override;
        [[nodiscard]] ExactOutputClockInfo info() const noexcept override;
        [[nodiscard]] ExactOutputClockCounters counters() const noexcept override;
        void Invalidate() noexcept override;

    private:
        struct Slot;

        ExactAsioClock(std::uint64_t endpoint_generation, std::uint32_t output_sample_rate, std::int64_t qpc_frequency,
                       std::uint32_t period_frames, std::uint32_t output_latency_frames, std::size_t capacity,
                       std::unique_ptr<Slot[]> slots) noexcept;

        [[nodiscard]] bool ReadStable(std::uint64_t publication, ExactAsioAnchor* anchor) const noexcept;
        [[nodiscard]] ExactOutputClockResult CountResult(
            // This result is consumed and returned by value.
            // ReSharper disable once CppPassValueParameterByConstReference
            ExactOutputClockResult result) const noexcept;

        std::uint64_t endpoint_generation_{};
        std::uint32_t output_sample_rate_{};
        std::int64_t qpc_frequency_{};
        std::uint32_t period_frames_{};
        std::uint32_t output_latency_frames_{};
        std::size_t capacity_{};
        std::unique_ptr<Slot[]> slots_;
        std::atomic<std::uint64_t> published_count_{};
        std::atomic<bool> invalidated_{};
        mutable std::atomic<std::uint64_t> resolved_queries_{};
        mutable std::atomic<std::uint64_t> pending_queries_{};
        mutable std::atomic<std::uint64_t> temporarily_unavailable_queries_{};
        mutable std::atomic<std::uint64_t> history_lost_queries_{};
        mutable std::atomic<std::uint64_t> discontinuous_queries_{};
        std::uint64_t writer_publication_count_{};
        bool writer_has_anchor_{};
        ExactAsioAnchor writer_previous_{};
    };
} // namespace gc::audio
