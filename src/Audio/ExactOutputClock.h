#pragma once

#include "Audio/ExactAudioTime.h"
#include "Timing/AbsoluteHostTime.h"

#include <cstdint>
#include <memory>
#include <string_view>

namespace gc::audio
{
    enum class ExactOutputClockDomain : std::uint8_t
    {
        WasapiQpc,
        AsioMultimediaMilliseconds,
    };

    [[nodiscard]] std::string_view ExactOutputClockDomainName(ExactOutputClockDomain domain) noexcept;

    struct ExactOutputClockInfo final
    {
        ExactOutputClockDomain domain{};
        std::uint64_t endpoint_generation{};
        std::int64_t qpc_frequency{};
        std::uint32_t output_sample_rate{};
        std::uint32_t period_frames{};
        std::uint32_t output_latency_frames{};
        std::uint32_t timestamp_quantum_ns{};
    };

    struct ExactOutputClockCounters final
    {
        std::uint64_t publication_count{};
        std::uint64_t resolved_queries{};
        std::uint64_t pending_queries{};
        std::uint64_t temporarily_unavailable_queries{};
        std::uint64_t history_lost_queries{};
        std::uint64_t discontinuous_queries{};
    };

    enum class ExactClockResolveIntent : std::uint8_t
    {
        FinalizedTimestamp,
        ProvisionalHorizon,
    };

    class ExactOutputClock
    {
    public:
        virtual ~ExactOutputClock() = default;

        [[nodiscard]] virtual ExactOutputClockResult Resolve(const gc::timing::AbsoluteHostTime& timestamp,
                                                             ExactClockResolveIntent intent) const noexcept = 0;
        [[nodiscard]] virtual ExactOutputClockInfo info() const noexcept = 0;
        [[nodiscard]] virtual ExactOutputClockCounters counters() const noexcept = 0;
        virtual void Invalidate() noexcept = 0;
    };

    [[nodiscard]] std::shared_ptr<const ExactOutputClock> AcquireExactOutputClock() noexcept;

    namespace detail
    {
        [[nodiscard]] std::uint64_t NextExactOutputClockGeneration() noexcept;
        [[nodiscard]] bool RegisterExactOutputClock(const std::shared_ptr<ExactOutputClock>& provider) noexcept;
        void UnregisterExactOutputClock(std::uint64_t expected_generation) noexcept;
    } // namespace detail
} // namespace gc::audio
