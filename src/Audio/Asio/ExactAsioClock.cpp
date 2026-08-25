#include "Audio/Asio/ExactAsioClock.h"

#include <algorithm>
#include <bit>
#include <limits>
#include <new>
#include <optional>
#include <utility>

namespace gc::audio
{
    namespace
    {
        constexpr std::memory_order kExactClockAtomicOrder = std::memory_order_seq_cst;
        constexpr std::uint32_t kSupportedOutputSampleRate = 48'000;
        constexpr std::uint64_t kNanosecondsPerMillisecond = 1'000'000;
        constexpr std::uint64_t kNanosecondsPerSecond = 1'000'000'000;
        constexpr std::uint64_t kRetentionSeconds = 60;
        constexpr int kStableReadAttempts = 3;

        static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
        static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
        static_assert(std::atomic<bool>::is_always_lock_free);
        static_assert(static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max()) <=
                      std::numeric_limits<std::int64_t>::max() / static_cast<std::int64_t>(kNanosecondsPerMillisecond));

        [[nodiscard]] ExactOutputClockResult Result(
            const ExactClockStatus status, const std::uint64_t endpoint_generation,
            const std::uint64_t submitted_output_tail = 0, const std::uint64_t anchor_sequence = 0,
            const std::optional<std::uint64_t>& anchor_position = std::nullopt) noexcept
        {
            return {
                .status = status,
                .endpoint_generation = endpoint_generation,
                .output_frame = std::nullopt,
                .submitted_output_tail = submitted_output_tail,
                .anchor_sequence = anchor_sequence,
                .anchor_endpoint_position = anchor_position,
            };
        }

        [[nodiscard]] std::int64_t MultimediaDeltaNanoseconds(const std::uint32_t event_time_ms,
                                                              const std::uint64_t anchor_system_time_ns) noexcept
        {
            const auto anchor_time_ms = static_cast<std::uint32_t>(anchor_system_time_ns / kNanosecondsPerMillisecond);
            const auto anchor_remainder_ns =
                static_cast<std::int64_t>(anchor_system_time_ns % kNanosecondsPerMillisecond);
            const auto delta_bits = static_cast<std::uint32_t>(event_time_ms - anchor_time_ms);
            const auto delta_ms = static_cast<std::int64_t>(std::bit_cast<std::int32_t>(delta_bits));
            return delta_ms * static_cast<std::int64_t>(kNanosecondsPerMillisecond) - anchor_remainder_ns;
        }
    } // namespace

    struct ExactAsioClock::Slot final
    {
        std::atomic<std::uint64_t> version{};
        std::atomic<std::uint64_t> sequence{};
        std::atomic<std::uint64_t> endpoint_generation{};
        std::atomic<std::uint64_t> presented_output_frame{};
        std::atomic<std::uint64_t> system_time_ns{};
        std::atomic<std::uint64_t> submitted_output_tail{};
    };

    ExactAsioClock::ExactAsioClock(const std::uint64_t endpoint_generation, const std::uint32_t output_sample_rate,
                                   const std::int64_t qpc_frequency, const std::uint32_t period_frames,
                                   const std::uint32_t output_latency_frames, const std::size_t capacity,
                                   std::unique_ptr<Slot[]> slots) noexcept
        : endpoint_generation_(endpoint_generation), output_sample_rate_(output_sample_rate),
          qpc_frequency_(qpc_frequency), period_frames_(period_frames), output_latency_frames_(output_latency_frames),
          capacity_(capacity), slots_(std::move(slots))
    {
    }

    ExactAsioClock::~ExactAsioClock() = default;

    std::shared_ptr<ExactAsioClock> ExactAsioClock::Create(const std::uint64_t endpoint_generation,
                                                           const std::uint32_t output_sample_rate,
                                                           const std::int64_t qpc_frequency,
                                                           const std::uint32_t period_frames,
                                                           const std::uint32_t output_latency_frames) noexcept
    {
        if (endpoint_generation == 0 || output_sample_rate != kSupportedOutputSampleRate || qpc_frequency <= 0 ||
            period_frames == 0)
        {
            return nullptr;
        }

        const auto retention_frames = kRetentionSeconds * output_sample_rate;
        auto capacity = retention_frames / period_frames;
        if (retention_frames % period_frames != 0)
        {
            ++capacity;
        }
        if (capacity > std::numeric_limits<std::size_t>::max() - 2)
        {
            return nullptr;
        }
        capacity += 2;
        if (capacity > std::numeric_limits<std::size_t>::max() / sizeof(Slot))
        {
            return nullptr;
        }

        auto slots = std::unique_ptr<Slot[]>(new (std::nothrow) Slot[static_cast<std::size_t>(capacity)]);
        if (slots == nullptr)
        {
            return nullptr;
        }
        auto* provider = new (std::nothrow)
            ExactAsioClock(endpoint_generation, output_sample_rate, qpc_frequency, period_frames, output_latency_frames,
                           static_cast<std::size_t>(capacity), std::move(slots));
        if (provider == nullptr)
        {
            return nullptr;
        }
        return std::shared_ptr<ExactAsioClock>(provider);
    }

    bool ExactAsioClock::Publish(const ExactAsioAnchor& anchor) noexcept
    {
        if (invalidated_.load(kExactClockAtomicOrder))
        {
            return false;
        }

        const bool decreasing_identity =
            writer_has_anchor_ && (anchor.sequence <= writer_previous_.sequence ||
                                   anchor.presented_output_frame <= writer_previous_.presented_output_frame ||
                                   anchor.submitted_output_tail < writer_previous_.submitted_output_tail);
        if (anchor.sequence == 0 || anchor.endpoint_generation != endpoint_generation_ ||
            anchor.submitted_output_tail < anchor.presented_output_frame || decreasing_identity ||
            writer_publication_count_ > (std::numeric_limits<std::uint64_t>::max() - 2) / 2)
        {
            Invalidate();
            return false;
        }

        const auto publication = writer_publication_count_++;
        auto& slot = slots_[publication % capacity_];
        const auto writing_version = publication * 2 + 1;
        slot.version.store(writing_version, kExactClockAtomicOrder);
        slot.sequence.store(anchor.sequence, kExactClockAtomicOrder);
        slot.endpoint_generation.store(anchor.endpoint_generation, kExactClockAtomicOrder);
        slot.presented_output_frame.store(anchor.presented_output_frame, kExactClockAtomicOrder);
        slot.system_time_ns.store(anchor.system_time_ns, kExactClockAtomicOrder);
        slot.submitted_output_tail.store(anchor.submitted_output_tail, kExactClockAtomicOrder);
        slot.version.store(writing_version + 1, kExactClockAtomicOrder);
        published_count_.store(publication + 1, kExactClockAtomicOrder);

        writer_previous_ = anchor;
        writer_has_anchor_ = true;
        return true;
    }

    void ExactAsioClock::Invalidate() noexcept
    {
        invalidated_.store(true, kExactClockAtomicOrder);
    }

    bool ExactAsioClock::ReadStable(const std::uint64_t publication, ExactAsioAnchor* const anchor) const noexcept
    {
        if (anchor == nullptr || publication > (std::numeric_limits<std::uint64_t>::max() - 2) / 2)
        {
            return false;
        }

        const auto expected_version = publication * 2 + 2;
        auto& slot = slots_[publication % capacity_];
        for (int attempt = 0; attempt < kStableReadAttempts; ++attempt)
        {
            const auto before = slot.version.load(kExactClockAtomicOrder);
            if (before != expected_version || (before & 1U) != 0)
            {
                continue;
            }
            const ExactAsioAnchor candidate{
                .sequence = slot.sequence.load(kExactClockAtomicOrder),
                .endpoint_generation = slot.endpoint_generation.load(kExactClockAtomicOrder),
                .presented_output_frame = slot.presented_output_frame.load(kExactClockAtomicOrder),
                .system_time_ns = slot.system_time_ns.load(kExactClockAtomicOrder),
                .submitted_output_tail = slot.submitted_output_tail.load(kExactClockAtomicOrder),
            };
            const auto after = slot.version.load(kExactClockAtomicOrder);
            if (before == after && after == expected_version)
            {
                *anchor = candidate;
                return true;
            }
        }
        return false;
    }

    ExactOutputClockResult ExactAsioClock::Resolve(const gc::timing::AbsoluteHostTime& timestamp,
                                                   const ExactClockResolveIntent intent) const noexcept
    {
        if (intent != ExactClockResolveIntent::FinalizedTimestamp &&
            intent != ExactClockResolveIntent::ProvisionalHorizon)
        {
            return CountResult(Result(ExactClockStatus::Discontinuous, endpoint_generation_));
        }
        if (invalidated_.load(kExactClockAtomicOrder))
        {
            return CountResult(Result(ExactClockStatus::Discontinuous, endpoint_generation_));
        }

        const auto published = published_count_.load(kExactClockAtomicOrder);
        if (published == 0)
        {
            return CountResult(Result(ExactClockStatus::Pending, endpoint_generation_));
        }

        const auto available = std::min<std::uint64_t>(published, static_cast<std::uint64_t>(capacity_));
        std::optional<ExactAsioAnchor> left;
        std::optional<ExactAsioAnchor> right;
        std::optional<ExactAsioAnchor> oldest;
        std::int64_t left_delta_ns{};
        std::int64_t right_delta_ns{};
        std::uint64_t latest_tail{};
        bool unstable{};

        for (std::uint64_t offset = 0; offset < available; ++offset)
        {
            const auto publication = published - offset - 1;
            ExactAsioAnchor anchor{};
            if (!ReadStable(publication, &anchor))
            {
                unstable = true;
                continue;
            }
            if (anchor.sequence == 0 || anchor.endpoint_generation != endpoint_generation_ ||
                anchor.submitted_output_tail < anchor.presented_output_frame)
            {
                return CountResult(Result(ExactClockStatus::Discontinuous, endpoint_generation_, latest_tail,
                                          anchor.sequence, anchor.presented_output_frame));
            }
            if (!oldest.has_value())
            {
                latest_tail = anchor.submitted_output_tail;
            }
            oldest = anchor;

            const auto delta_ns = MultimediaDeltaNanoseconds(timestamp.multimedia_time_ms, anchor.system_time_ns);
            if (delta_ns < 0)
            {
                right = anchor;
                right_delta_ns = delta_ns;
                continue;
            }
            if (unstable)
            {
                return CountResult(Result(ExactClockStatus::TemporarilyUnavailable, endpoint_generation_, latest_tail));
            }
            left = anchor;
            left_delta_ns = delta_ns;
            break;
        }

        if (invalidated_.load(kExactClockAtomicOrder))
        {
            return CountResult(
                Result(ExactClockStatus::Discontinuous, endpoint_generation_, latest_tail, left ? left->sequence : 0,
                       left ? std::optional<std::uint64_t>(left->presented_output_frame) : std::nullopt));
        }
        if (!left.has_value())
        {
            if (unstable)
            {
                return CountResult(Result(ExactClockStatus::TemporarilyUnavailable, endpoint_generation_, latest_tail));
            }
            if (published > capacity_ && oldest.has_value() &&
                MultimediaDeltaNanoseconds(timestamp.multimedia_time_ms, oldest->system_time_ns) < 0)
            {
                return CountResult(Result(ExactClockStatus::HistoryLost, endpoint_generation_, latest_tail,
                                          oldest->sequence, oldest->presented_output_frame));
            }
            return CountResult(Result(ExactClockStatus::Pending, endpoint_generation_, latest_tail));
        }

        const auto& left_anchor = *left;
        if (!right.has_value() && intent == ExactClockResolveIntent::FinalizedTimestamp)
        {
            return CountResult(Result(ExactClockStatus::Pending, endpoint_generation_, latest_tail,
                                      left_anchor.sequence, left_anchor.presented_output_frame));
        }

        if (left_anchor.presented_output_frame > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
            latest_tail > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        {
            return CountResult(Result(ExactClockStatus::Discontinuous, endpoint_generation_, latest_tail,
                                      left_anchor.sequence, left_anchor.presented_output_frame));
        }

        auto output_frame = std::expected<gc::timing::CheckedRational, gc::timing::RationalError>(
            std::unexpected(gc::timing::RationalError::Overflow));
        if (right.has_value())
        {
            const auto& right_anchor = *right;
            if (right_anchor.presented_output_frame >
                    static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
                right_anchor.sequence <= left_anchor.sequence ||
                right_anchor.presented_output_frame <= left_anchor.presented_output_frame)
            {
                return CountResult(Result(ExactClockStatus::Discontinuous, endpoint_generation_, latest_tail,
                                          left_anchor.sequence, left_anchor.presented_output_frame));
            }

            const auto right_distance_ns = static_cast<std::uint64_t>(-(right_delta_ns + 1)) + 1;
            const auto span_ns = static_cast<std::uint64_t>(left_delta_ns) + right_distance_ns;
            const auto frame_span = right_anchor.presented_output_frame - left_anchor.presented_output_frame;
            if (span_ns == 0 || frame_span > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
            {
                return CountResult(Result(ExactClockStatus::Discontinuous, endpoint_generation_, latest_tail,
                                          left_anchor.sequence, left_anchor.presented_output_frame));
            }

            const auto interpolation = gc::timing::CheckedRational::Create(left_delta_ns, span_ns);
            const auto output_offset = interpolation
                                           ? interpolation->Multiply(static_cast<std::int64_t>(frame_span), 1)
                                           : std::expected<gc::timing::CheckedRational, gc::timing::RationalError>(
                                                 std::unexpected(gc::timing::RationalError::Overflow));
            if (!interpolation || !output_offset)
            {
                return CountResult(Result(ExactClockStatus::Discontinuous, endpoint_generation_, latest_tail,
                                          left_anchor.sequence, left_anchor.presented_output_frame));
            }
            output_frame =
                gc::timing::CheckedRational::Whole(static_cast<std::int64_t>(left_anchor.presented_output_frame))
                    .Add(*output_offset);
        }
        else
        {
            const auto delta_seconds = gc::timing::CheckedRational::Create(left_delta_ns, kNanosecondsPerSecond);
            const auto output_offset = delta_seconds
                                           ? delta_seconds->Multiply(output_sample_rate_, 1)
                                           : std::expected<gc::timing::CheckedRational, gc::timing::RationalError>(
                                                 std::unexpected(gc::timing::RationalError::Overflow));
            if (!delta_seconds || !output_offset)
            {
                return CountResult(Result(ExactClockStatus::Discontinuous, endpoint_generation_, latest_tail,
                                          left_anchor.sequence, left_anchor.presented_output_frame));
            }
            output_frame =
                gc::timing::CheckedRational::Whole(static_cast<std::int64_t>(left_anchor.presented_output_frame))
                    .Add(*output_offset);
        }
        if (!output_frame || output_frame->Compare(gc::timing::CheckedRational::Whole(0)) < 0)
        {
            return CountResult(Result(ExactClockStatus::Discontinuous, endpoint_generation_, latest_tail,
                                      left_anchor.sequence, left_anchor.presented_output_frame));
        }
        if (invalidated_.load(kExactClockAtomicOrder))
        {
            return CountResult(Result(ExactClockStatus::Discontinuous, endpoint_generation_, latest_tail,
                                      left_anchor.sequence, left_anchor.presented_output_frame));
        }
        if (latest_tail == 0 ||
            output_frame->Compare(gc::timing::CheckedRational::Whole(static_cast<std::int64_t>(latest_tail))) >= 0)
        {
            return CountResult(Result(ExactClockStatus::Pending, endpoint_generation_, latest_tail,
                                      left_anchor.sequence, left_anchor.presented_output_frame));
        }

        return CountResult({
            .status = ExactClockStatus::Resolved,
            .endpoint_generation = endpoint_generation_,
            .output_frame = *output_frame,
            .submitted_output_tail = latest_tail,
            .anchor_sequence = left_anchor.sequence,
            .anchor_endpoint_position = left_anchor.presented_output_frame,
        });
    }

    ExactOutputClockInfo ExactAsioClock::info() const noexcept
    {
        return {
            .domain = ExactOutputClockDomain::AsioMultimediaMilliseconds,
            .endpoint_generation = endpoint_generation_,
            .qpc_frequency = qpc_frequency_,
            .output_sample_rate = output_sample_rate_,
            .period_frames = period_frames_,
            .output_latency_frames = output_latency_frames_,
            .timestamp_quantum_ns = static_cast<std::uint32_t>(kNanosecondsPerMillisecond),
        };
    }

    ExactOutputClockCounters ExactAsioClock::counters() const noexcept
    {
        return {
            .publication_count = published_count_.load(kExactClockAtomicOrder),
            .resolved_queries = resolved_queries_.load(kExactClockAtomicOrder),
            .pending_queries = pending_queries_.load(kExactClockAtomicOrder),
            .temporarily_unavailable_queries = temporarily_unavailable_queries_.load(kExactClockAtomicOrder),
            .history_lost_queries = history_lost_queries_.load(kExactClockAtomicOrder),
            .discontinuous_queries = discontinuous_queries_.load(kExactClockAtomicOrder),
        };
    }

    ExactOutputClockResult ExactAsioClock::CountResult(
        // This result is consumed and returned by value.
        // ReSharper disable once CppPassValueParameterByConstReference
        ExactOutputClockResult result) const noexcept
    {
        switch (result.status)
        {
        case ExactClockStatus::Resolved:
            resolved_queries_.fetch_add(1, kExactClockAtomicOrder);
            break;
        case ExactClockStatus::Pending:
            pending_queries_.fetch_add(1, kExactClockAtomicOrder);
            break;
        case ExactClockStatus::TemporarilyUnavailable:
            temporarily_unavailable_queries_.fetch_add(1, kExactClockAtomicOrder);
            break;
        case ExactClockStatus::HistoryLost:
            history_lost_queries_.fetch_add(1, kExactClockAtomicOrder);
            break;
        case ExactClockStatus::Discontinuous:
            discontinuous_queries_.fetch_add(1, kExactClockAtomicOrder);
            break;
        case ExactClockStatus::NoPlayback:
        case ExactClockStatus::OutsidePlayback:
            break;
        }
        return result;
    }
} // namespace gc::audio
