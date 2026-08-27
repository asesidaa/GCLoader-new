#pragma once

#include "Timing/CheckedRational.h"

#include <atomic>
#include <cstdint>
#include <expected>
#include <memory>

namespace gc::audio
{
    enum class AsioLogicalTimelineFailure : std::uint8_t
    {
        InvalidConfiguration,
        WriterDeltaAmbiguous,
        TimestampAmbiguous,
        SnapshotUnavailable,
        NegativeCoordinate,
        ArithmeticOverflow,
    };

    class AsioLogicalTimeline final
    {
    public:
        [[nodiscard]] static std::shared_ptr<AsioLogicalTimeline> Create(
            std::uint32_t origin_raw_ms,
            std::uint32_t output_sample_rate) noexcept;

        [[nodiscard]] std::expected<void, AsioLogicalTimelineFailure>
        AdvanceNow(std::uint32_t observed_raw_ms) noexcept;
        [[nodiscard]] std::expected<gc::timing::CheckedRational,
                                    AsioLogicalTimelineFailure>
        ProjectMultimediaMilliseconds(std::uint32_t raw_ms) const noexcept;
        [[nodiscard]] std::expected<gc::timing::CheckedRational,
                                    AsioLogicalTimelineFailure>
        ProjectSystemTimeNanoseconds(std::uint64_t system_time_ns) const noexcept;
        [[nodiscard]] std::expected<std::uint64_t,
                                    AsioLogicalTimelineFailure>
        WholePresentedFrameAt(std::uint32_t raw_ms) const noexcept;
        [[nodiscard]] std::expected<std::uint64_t,
                                    AsioLogicalTimelineFailure>
        WholePresentedFrameAtSystemTime(std::uint64_t system_time_ns) const noexcept;

        [[nodiscard]] std::uint32_t origin_raw_ms() const noexcept;
        [[nodiscard]] std::uint32_t output_sample_rate() const noexcept;

    private:
        struct Snapshot final
        {
            std::uint32_t observed_raw_ms{};
            std::uint64_t observed_unwrapped_ms{};
        };

        AsioLogicalTimeline(std::uint32_t origin_raw_ms,
                            std::uint32_t output_sample_rate) noexcept;

        [[nodiscard]] std::expected<Snapshot, AsioLogicalTimelineFailure>
        ReadSnapshot() const noexcept;
        [[nodiscard]] std::expected<std::uint64_t,
                                    AsioLogicalTimelineFailure>
        Unwrap(std::uint32_t raw_ms) const noexcept;
        [[nodiscard]] std::expected<gc::timing::CheckedRational,
                                    AsioLogicalTimelineFailure>
        ProjectUnwrappedMilliseconds(std::uint64_t unwrapped_ms) const noexcept;
        [[nodiscard]] static std::expected<std::uint64_t,
                                           AsioLogicalTimelineFailure>
        WholeFrame(const std::expected<gc::timing::CheckedRational,
                                       AsioLogicalTimelineFailure>& coordinate) noexcept;

        const std::uint32_t origin_raw_ms_;
        const std::uint32_t output_sample_rate_;

        std::uint32_t writer_raw_ms_;
        std::uint64_t writer_unwrapped_ms_{};
        std::atomic_uint64_t snapshot_version_{};
        std::atomic_uint32_t observed_raw_ms_;
        std::atomic_uint64_t observed_unwrapped_ms_{};
    };
} // namespace gc::audio
