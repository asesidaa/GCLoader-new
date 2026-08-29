#include "Audio/Wasapi/ExactWasapiClock.h"

#include <algorithm>
#include <limits>
#include <new>
#include <optional>
#include <utility>

namespace gc::audio
{
    namespace
    {
        constexpr std::memory_order kExactClockAtomicOrder = std::memory_order_seq_cst;
        constexpr std::uint64_t kReferenceTimePerSecond = 10'000'000;
        constexpr std::uint64_t kRetentionSeconds = 60;
        constexpr int kStableReadAttempts = 3;

        static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
        static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
        static_assert(std::atomic<bool>::is_always_lock_free);

        struct RawQpcParts
        {
            std::int64_t whole_seconds{};
            std::uint64_t remainder{};
        };

        RawQpcParts SplitRawQpc(std::int64_t ticks, std::int64_t frequency) noexcept
        {
            auto whole = ticks / frequency;
            auto remainder = ticks % frequency;
            if (remainder < 0)
            {
                --whole;
                remainder += frequency;
            }
            return {
                whole,
                static_cast<std::uint64_t>(remainder),
            };
        }

        std::optional<int> CompareQpc(const RawQpcParts& raw, std::int64_t raw_frequency,
                                      std::uint64_t qpc_100ns) noexcept
        {
            const auto anchor_whole = qpc_100ns / kReferenceTimePerSecond;
            if (raw.whole_seconds < 0)
            {
                return -1;
            }

            const auto raw_whole = static_cast<std::uint64_t>(raw.whole_seconds);
            if (raw_whole != anchor_whole)
            {
                return raw_whole < anchor_whole ? -1 : 1;
            }

            const auto raw_fraction = gc::timing::CheckedRational::Create(static_cast<std::int64_t>(raw.remainder),
                                                                          static_cast<std::uint64_t>(raw_frequency));
            const auto anchor_fraction = gc::timing::CheckedRational::Create(
                static_cast<std::int64_t>(qpc_100ns % kReferenceTimePerSecond), kReferenceTimePerSecond);
            if (!raw_fraction.has_value() || !anchor_fraction.has_value())
            {
                return std::nullopt;
            }
            return raw_fraction->Compare(*anchor_fraction);
        }

        std::optional<gc::timing::CheckedRational> QpcDeltaSeconds(const RawQpcParts& raw, std::int64_t raw_frequency,
                                                                   std::uint64_t anchor_qpc_100ns) noexcept
        {
            const auto anchor_whole = anchor_qpc_100ns / kReferenceTimePerSecond;
            if (raw.whole_seconds < 0 || static_cast<std::uint64_t>(raw.whole_seconds) < anchor_whole)
            {
                return std::nullopt;
            }

            const auto whole_delta = static_cast<std::uint64_t>(raw.whole_seconds) - anchor_whole;
            if (whole_delta > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
            {
                return std::nullopt;
            }

            const auto raw_fraction = gc::timing::CheckedRational::Create(static_cast<std::int64_t>(raw.remainder),
                                                                          static_cast<std::uint64_t>(raw_frequency));
            const auto anchor_fraction = gc::timing::CheckedRational::Create(
                static_cast<std::int64_t>(anchor_qpc_100ns % kReferenceTimePerSecond), kReferenceTimePerSecond);
            if (!raw_fraction.has_value() || !anchor_fraction.has_value())
            {
                return std::nullopt;
            }

            const auto with_raw_fraction =
                gc::timing::CheckedRational::Whole(static_cast<std::int64_t>(whole_delta)).Add(*raw_fraction);
            if (!with_raw_fraction.has_value())
            {
                return std::nullopt;
            }
            const auto delta = with_raw_fraction->Subtract(*anchor_fraction);
            if (!delta.has_value() || delta->Compare(gc::timing::CheckedRational::Whole(0)) < 0)
            {
                return std::nullopt;
            }
            return *delta;
        }

        ExactJudgementTimelineResult Result(ExactClockStatus status, std::uint64_t endpoint_generation,
                                            std::uint64_t submitted_output_tail = 0,
                                            std::uint64_t anchor_sequence = 0,
                                      // Keep this small optional value parameter self-contained in the result
                                      // helper.
                                      // ReSharper disable once CppPassValueParameterByConstReference
                                      std::optional<std::uint64_t> anchor_endpoint_position = std::nullopt) noexcept
        {
            return {
                .status = status,
                .timeline_generation = endpoint_generation,
                .logical_output_frame = std::nullopt,
                .available_output_tail = submitted_output_tail,
                .provider_anchor_sequence = anchor_sequence,
                .provider_position = anchor_endpoint_position,
            };
        }

        bool SameMapping(const EndpointClockMapping& left, const EndpointClockMapping& right) noexcept
        {
            return left.origin_position == right.origin_position && left.clock_frequency == right.clock_frequency &&
                   left.origin_output_frame == right.origin_output_frame &&
                   left.output_sample_rate == right.output_sample_rate;
        }
    } // namespace

    struct ExactWasapiClock::Slot
    {
        std::atomic<std::uint64_t> version{};
        std::atomic<std::uint64_t> sequence{};
        std::atomic<std::uint64_t> endpoint_generation{};
        std::atomic<std::uint64_t> endpoint_position{};
        std::atomic<std::uint64_t> qpc_100ns{};
        std::atomic<std::uint64_t> mapping_origin_position{};
        std::atomic<std::uint64_t> mapping_clock_frequency{};
        std::atomic<std::uint64_t> mapping_origin_output_frame{};
        std::atomic<std::uint32_t> mapping_output_sample_rate{};
        std::atomic<std::uint64_t> submitted_output_tail{};
    };

    ExactWasapiClock::ExactWasapiClock(std::uint64_t endpoint_generation, std::uint32_t output_sample_rate,
                                       std::uint64_t clock_frequency, std::int64_t qpc_frequency,
                                       std::uint32_t period_frames, std::size_t capacity,
                                       std::unique_ptr<Slot[]> slots) noexcept
        : endpoint_generation_(endpoint_generation), output_sample_rate_(output_sample_rate),
          clock_frequency_(clock_frequency), qpc_frequency_(qpc_frequency), period_frames_(period_frames),
          capacity_(capacity), slots_(std::move(slots))
    {
    }

    ExactWasapiClock::~ExactWasapiClock() = default;

    std::shared_ptr<ExactWasapiClock> ExactWasapiClock::Create(std::uint64_t endpoint_generation,
                                                               std::uint32_t output_sample_rate,
                                                               std::uint64_t clock_frequency,
                                                               std::int64_t qpc_frequency,
                                                               std::uint32_t period_frames) noexcept
    {
        if (endpoint_generation == 0 || output_sample_rate == 0 || clock_frequency == 0 ||
            clock_frequency > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
            qpc_frequency <= 0 || period_frames == 0)
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

        auto* provider =
            new (std::nothrow) ExactWasapiClock(endpoint_generation, output_sample_rate, clock_frequency, qpc_frequency,
                                                period_frames, static_cast<std::size_t>(capacity), std::move(slots));
        if (provider == nullptr)
        {
            return nullptr;
        }
        return std::shared_ptr<ExactWasapiClock>(provider);
    }

    void ExactWasapiClock::Publish(const ExactWasapiAnchor& anchor) noexcept
    {
        if (invalidated_.load(kExactClockAtomicOrder))
        {
            return;
        }

        const bool invalid_mapping =
            anchor.mapping.clock_frequency != clock_frequency_ ||
            anchor.mapping.output_sample_rate != output_sample_rate_ ||
            anchor.endpoint_position < anchor.mapping.origin_position ||
            anchor.endpoint_position - anchor.mapping.origin_position >
                static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
            anchor.mapping.origin_output_frame > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
            anchor.submitted_output_tail > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
        const bool decreasing_identity =
            writer_has_anchor_ && (anchor.sequence <= writer_previous_.sequence ||
                                   anchor.endpoint_position < writer_previous_.endpoint_position ||
                                   anchor.qpc_100ns < writer_previous_.qpc_100ns ||
                                   anchor.submitted_output_tail < writer_previous_.submitted_output_tail ||
                                   !SameMapping(anchor.mapping, writer_previous_.mapping));
        if (anchor.sequence == 0 || anchor.endpoint_generation != endpoint_generation_ || invalid_mapping ||
            decreasing_identity || writer_publication_count_ > (std::numeric_limits<std::uint64_t>::max() - 2) / 2)
        {
            Invalidate();
            return;
        }

        const auto publication = writer_publication_count_++;
        auto& slot = slots_[publication % capacity_];
        const auto writing_version = publication * 2 + 1;
        slot.version.store(writing_version, kExactClockAtomicOrder);
        slot.sequence.store(anchor.sequence, kExactClockAtomicOrder);
        slot.endpoint_generation.store(anchor.endpoint_generation, kExactClockAtomicOrder);
        slot.endpoint_position.store(anchor.endpoint_position, kExactClockAtomicOrder);
        slot.qpc_100ns.store(anchor.qpc_100ns, kExactClockAtomicOrder);
        slot.mapping_origin_position.store(anchor.mapping.origin_position, kExactClockAtomicOrder);
        slot.mapping_clock_frequency.store(anchor.mapping.clock_frequency, kExactClockAtomicOrder);
        slot.mapping_origin_output_frame.store(anchor.mapping.origin_output_frame, kExactClockAtomicOrder);
        slot.mapping_output_sample_rate.store(anchor.mapping.output_sample_rate, kExactClockAtomicOrder);
        slot.submitted_output_tail.store(anchor.submitted_output_tail, kExactClockAtomicOrder);
        slot.version.store(writing_version + 1, kExactClockAtomicOrder);
        published_count_.store(publication + 1, kExactClockAtomicOrder);

        writer_previous_ = anchor;
        writer_has_anchor_ = true;
    }

    void ExactWasapiClock::Invalidate() noexcept
    {
        invalidated_.store(true, kExactClockAtomicOrder);
    }

    bool ExactWasapiClock::ReadStable(std::uint64_t publication, ExactWasapiAnchor* anchor) const noexcept
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

            ExactWasapiAnchor candidate{
                slot.sequence.load(kExactClockAtomicOrder),
                slot.endpoint_generation.load(kExactClockAtomicOrder),
                slot.endpoint_position.load(kExactClockAtomicOrder),
                slot.qpc_100ns.load(kExactClockAtomicOrder),
                {
                    slot.mapping_origin_position.load(kExactClockAtomicOrder),
                    slot.mapping_clock_frequency.load(kExactClockAtomicOrder),
                    slot.mapping_origin_output_frame.load(kExactClockAtomicOrder),
                    slot.mapping_output_sample_rate.load(kExactClockAtomicOrder),
                },
                slot.submitted_output_tail.load(kExactClockAtomicOrder),
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

    ExactJudgementTimelineResult ExactWasapiClock::ResolveQpc(std::int64_t raw_qpc_ticks) const noexcept
    {
        if (invalidated_.load(kExactClockAtomicOrder))
        {
            return Result(ExactClockStatus::Discontinuous, endpoint_generation_);
        }

        const auto published = published_count_.load(kExactClockAtomicOrder);
        if (published == 0)
        {
            return Result(ExactClockStatus::Pending, endpoint_generation_);
        }

        const auto raw = SplitRawQpc(raw_qpc_ticks, qpc_frequency_);
        const auto available = std::min<std::uint64_t>(published, static_cast<std::uint64_t>(capacity_));
        std::optional<ExactWasapiAnchor> selected;
        std::optional<ExactWasapiAnchor> oldest;
        std::uint64_t latest_tail{};
        bool unstable{};

        for (std::uint64_t offset = 0; offset < available; ++offset)
        {
            const auto publication = published - offset - 1;
            ExactWasapiAnchor anchor{};
            if (!ReadStable(publication, &anchor))
            {
                unstable = true;
                continue;
            }
            if (anchor.endpoint_generation != endpoint_generation_ ||
                anchor.mapping.clock_frequency != clock_frequency_ ||
                anchor.mapping.output_sample_rate != output_sample_rate_ ||
                anchor.endpoint_position < anchor.mapping.origin_position)
            {
                return Result(ExactClockStatus::Discontinuous, endpoint_generation_, latest_tail, anchor.sequence,
                              anchor.endpoint_position);
            }
            if (!oldest.has_value())
            {
                latest_tail = anchor.submitted_output_tail;
            }
            oldest = anchor;

            const auto comparison = CompareQpc(raw, qpc_frequency_, anchor.qpc_100ns);
            if (!comparison.has_value())
            {
                return Result(ExactClockStatus::Discontinuous, endpoint_generation_, latest_tail, anchor.sequence,
                              anchor.endpoint_position);
            }
            if (*comparison >= 0 && !selected.has_value())
            {
                selected = anchor;
                break;
            }
        }

        if (invalidated_.load(kExactClockAtomicOrder))
        {
            return Result(ExactClockStatus::Discontinuous, endpoint_generation_, latest_tail,
                          selected ? selected->sequence : 0,
                          selected ? std::optional<std::uint64_t>(selected->endpoint_position) : std::nullopt);
        }
        if (!selected.has_value())
        {
            if (unstable)
            {
                return Result(ExactClockStatus::TemporarilyUnavailable, endpoint_generation_, latest_tail);
            }
            if (published > capacity_ && oldest.has_value())
            {
                const auto comparison = CompareQpc(raw, qpc_frequency_, oldest->qpc_100ns);
                if (comparison.has_value() && *comparison < 0)
                {
                    return Result(ExactClockStatus::HistoryLost, endpoint_generation_, latest_tail, oldest->sequence,
                                  oldest->endpoint_position);
                }
            }
            return Result(ExactClockStatus::Pending, endpoint_generation_, latest_tail);
        }

        const auto& anchor = *selected;
        const auto delta_seconds = QpcDeltaSeconds(raw, qpc_frequency_, anchor.qpc_100ns);
        if (!delta_seconds.has_value())
        {
            return Result(ExactClockStatus::Discontinuous, endpoint_generation_, anchor.submitted_output_tail,
                          anchor.sequence, anchor.endpoint_position);
        }

        const auto clock_delta = delta_seconds->Multiply(static_cast<std::int64_t>(clock_frequency_), 1);
        const auto mapped_endpoint_delta = anchor.endpoint_position - anchor.mapping.origin_position;
        if (!clock_delta.has_value() ||
            mapped_endpoint_delta > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        {
            return Result(ExactClockStatus::Discontinuous, endpoint_generation_, anchor.submitted_output_tail,
                          anchor.sequence, anchor.endpoint_position);
        }

        const auto endpoint_offset =
            gc::timing::CheckedRational::Whole(static_cast<std::int64_t>(mapped_endpoint_delta)).Add(*clock_delta);
        if (!endpoint_offset.has_value())
        {
            return Result(ExactClockStatus::Discontinuous, endpoint_generation_, anchor.submitted_output_tail,
                          anchor.sequence, anchor.endpoint_position);
        }
        const auto output_offset = endpoint_offset->Multiply(
            static_cast<std::int64_t>(anchor.mapping.output_sample_rate), anchor.mapping.clock_frequency);
        if (!output_offset.has_value() ||
            anchor.mapping.origin_output_frame > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        {
            return Result(ExactClockStatus::Discontinuous, endpoint_generation_, anchor.submitted_output_tail,
                          anchor.sequence, anchor.endpoint_position);
        }
        const auto output_frame =
            gc::timing::CheckedRational::Whole(static_cast<std::int64_t>(anchor.mapping.origin_output_frame))
                .Add(*output_offset);
        if (!output_frame.has_value() || output_frame->Compare(gc::timing::CheckedRational::Whole(0)) < 0)
        {
            return Result(ExactClockStatus::Discontinuous, endpoint_generation_, anchor.submitted_output_tail,
                          anchor.sequence, anchor.endpoint_position);
        }
        if (invalidated_.load(kExactClockAtomicOrder))
        {
            return Result(ExactClockStatus::Discontinuous, endpoint_generation_, anchor.submitted_output_tail,
                          anchor.sequence, anchor.endpoint_position);
        }

        if (anchor.submitted_output_tail == 0 || output_frame->Compare(gc::timing::CheckedRational::Whole(
                                                     static_cast<std::int64_t>(anchor.submitted_output_tail))) >= 0)
        {
            return Result(ExactClockStatus::Pending, endpoint_generation_, anchor.submitted_output_tail,
                          anchor.sequence, anchor.endpoint_position);
        }

        return {
            .status = ExactClockStatus::Resolved,
            .timeline_generation = endpoint_generation_,
            .logical_output_frame = *output_frame,
            .available_output_tail = anchor.submitted_output_tail,
            .provider_anchor_sequence = anchor.sequence,
            .provider_position = anchor.endpoint_position,
        };
    }

    ExactJudgementTimelineResult ExactWasapiClock::Resolve(const gc::timing::AbsoluteHostTime& timestamp,
                                                           const ExactClockResolveIntent) const noexcept
    {
        const auto result = ResolveQpc(timestamp.qpc_ticks);
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

    ExactJudgementTimelineInfo ExactWasapiClock::info() const noexcept
    {
        return {
            .domain = ExactJudgementTimelineDomain::WasapiQpc,
            .timeline_generation = endpoint_generation_,
            .qpc_frequency = qpc_frequency_,
            .logical_output_rate = output_sample_rate_,
            .provider_period_frames = period_frames_,
            .provider_output_latency_frames = 0,
            .timestamp_quantum_ns = 0,
        };
    }

    ExactJudgementTimelineCounters ExactWasapiClock::counters() const noexcept
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

    std::uint64_t ExactWasapiClock::endpoint_generation() const noexcept
    {
        return endpoint_generation_;
    }

    std::int64_t ExactWasapiClock::qpc_frequency() const noexcept
    {
        return qpc_frequency_;
    }

    std::uint64_t ExactWasapiClock::publication_count() const noexcept
    {
        return published_count_.load(kExactClockAtomicOrder);
    }

    std::shared_ptr<const ExactWasapiClock> AcquireExactWasapiClock() noexcept
    {
        auto provider = AcquireExactJudgementTimeline();
        if (provider == nullptr || provider->info().domain != ExactJudgementTimelineDomain::WasapiQpc)
        {
            return nullptr;
        }
        return std::static_pointer_cast<const ExactWasapiClock>(std::move(provider));
    }
} // namespace gc::audio
