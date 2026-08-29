#include "Audio/Mixer/AudioCursorTimeline.h"

#include <algorithm>
#include <atomic>
#include <limits>
#include <new>
#include <utility>

namespace gc::audio
{
    namespace
    {
        static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
        static_assert(std::atomic<std::int64_t>::is_always_lock_free);
        static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
        static_assert(std::atomic<std::uint8_t>::is_always_lock_free);
        static_assert(std::atomic<bool>::is_always_lock_free);
        static_assert(std::atomic_ref<std::uint64_t>::is_always_lock_free);
        static_assert(std::atomic_ref<bool>::is_always_lock_free);

        constexpr std::uint64_t StableSequence(
            std::uint64_t generation) noexcept
        {
            return generation * 2 + 2;
        }

        struct ModularSum
        {
            std::uint64_t remainder;
            std::uint64_t quotient;
        };

        struct ModularProduct
        {
            std::uint64_t quotient;
            std::uint64_t remainder;
        };

        ModularSum AddModulo(
            std::uint64_t left,
            std::uint64_t right,
            std::uint64_t denominator) noexcept
        {
            if (left >= denominator - right)
            {
                return {left - (denominator - right), 1};
            }
            return {left + right, 0};
        }

        ModularProduct MultiplyProperFractions(
            std::uint64_t left,
            std::uint64_t right,
            std::uint64_t denominator) noexcept
        {
            if (left == 0 || right == 0)
            {
                return {};
            }
            if (left <= std::numeric_limits<std::uint64_t>::max() / right)
            {
                const auto product = left * right;
                return {product / denominator, product % denominator};
            }

            std::uint64_t quotient{};
            std::uint64_t remainder{};
            for (int bit = 63; bit >= 0; --bit)
            {
                quotient *= 2;
                const auto doubled = AddModulo(remainder, remainder, denominator);
                remainder = doubled.remainder;
                quotient += doubled.quotient;

                if ((left & (std::uint64_t{1} << bit)) != 0)
                {
                    const auto added = AddModulo(remainder, right, denominator);
                    remainder = added.remainder;
                    quotient += added.quotient;
                }
            }
            return {quotient, remainder};
        }

        bool CheckedMultiply(
            std::uint64_t left,
            std::uint64_t right,
            std::uint64_t* result) noexcept
        {
            if (left != 0 &&
                right > std::numeric_limits<std::uint64_t>::max() / left)
            {
                return false;
            }
            *result = left * right;
            return true;
        }

        bool CheckedAdd(
            std::uint64_t value,
            std::uint64_t increment,
            std::uint64_t* result) noexcept
        {
            if (increment > std::numeric_limits<std::uint64_t>::max() - value)
            {
                return false;
            }
            *result = value + increment;
            return true;
        }

        std::optional<std::uint64_t> ScaleFloor(
            std::uint64_t value,
            std::uint64_t numerator,
            std::uint64_t denominator) noexcept
        {
            if (denominator == 0)
            {
                return std::nullopt;
            }

            const auto numerator_quotient = numerator / denominator;
            const auto numerator_remainder = numerator % denominator;
            const auto value_quotient = value / denominator;
            const auto value_remainder = value % denominator;

            std::uint64_t result{};
            std::uint64_t term{};
            if (!CheckedMultiply(value, numerator_quotient, &result) ||
                !CheckedMultiply(value_quotient, numerator_remainder, &term) ||
                !CheckedAdd(result, term, &result))
            {
                return std::nullopt;
            }

            const auto fractional = MultiplyProperFractions(
                value_remainder,
                numerator_remainder,
                denominator);
            if (!CheckedAdd(result, fractional.quotient, &result))
            {
                return std::nullopt;
            }
            return result;
        }

        std::optional<std::uint64_t> ScaleCeil(
            std::uint64_t value,
            std::uint64_t numerator,
            std::uint64_t denominator) noexcept
        {
            const auto floor = ScaleFloor(value, numerator, denominator);
            if (!floor.has_value() || denominator == 0)
            {
                return std::nullopt;
            }

            const auto fractional = MultiplyProperFractions(
                value % denominator,
                numerator % denominator,
                denominator);
            if (fractional.remainder == 0)
            {
                return floor;
            }

            std::uint64_t ceiling{};
            if (!CheckedAdd(*floor, 1, &ceiling))
            {
                return std::nullopt;
            }
            return ceiling;
        }

        template <typename Value>
        void StoreSpanValue(Value& destination, Value value) noexcept
        {
            std::atomic_ref<Value>(destination).store(
                value,
                detail::kRenderSpanAtomicOrder);
        }

        template <typename Value>
        Value LoadSpanValue(Value& source) noexcept
        {
            return std::atomic_ref<Value>(source).load(
                detail::kRenderSpanAtomicOrder);
        }

        void StoreSpan(
            AudioRenderSpan& destination,
            const AudioRenderSpan& source) noexcept
        {
            StoreSpanValue(destination.output_frame_begin, source.output_frame_begin);
            StoreSpanValue(destination.output_frame_end, source.output_frame_end);
            StoreSpanValue(
                destination.source_frame_begin_unwrapped,
                source.source_frame_begin_unwrapped);
            StoreSpanValue(
                destination.source_frame_end_unwrapped,
                source.source_frame_end_unwrapped);
            StoreSpanValue(destination.epoch, source.epoch);
            StoreSpanValue(destination.loop_wrapped, source.loop_wrapped);
            StoreSpanValue(destination.source_ended, source.source_ended);
        }

        AudioRenderSpan LoadSpan(AudioRenderSpan& source) noexcept
        {
            return {
                LoadSpanValue(source.output_frame_begin),
                LoadSpanValue(source.output_frame_end),
                LoadSpanValue(source.source_frame_begin_unwrapped),
                LoadSpanValue(source.source_frame_end_unwrapped),
                LoadSpanValue(source.epoch),
                LoadSpanValue(source.loop_wrapped),
                LoadSpanValue(source.source_ended),
            };
        }

        std::optional<gc::timing::CheckedRational> WholeUnsigned(
            std::uint64_t value) noexcept
        {
            if (value > static_cast<std::uint64_t>(
                std::numeric_limits<std::int64_t>::max()))
            {
                return std::nullopt;
            }
            return gc::timing::CheckedRational::Whole(
                static_cast<std::int64_t>(value));
        }

        std::optional<gc::timing::CheckedRational> SourceAtOutput(
            const ExactPlaybackEpoch& epoch,
            const gc::timing::CheckedRational& output) noexcept
        {
            if (epoch.output_rate == 0 || epoch.source_rate == 0)
            {
                return std::nullopt;
            }
            const auto output_origin = WholeUnsigned(epoch.output_origin);
            const auto source_origin = WholeUnsigned(epoch.source_origin);
            if (!output_origin.has_value() || !source_origin.has_value() ||
                output.Compare(*output_origin) < 0)
            {
                return std::nullopt;
            }
            const auto delta = output.Subtract(*output_origin);
            if (!delta.has_value())
            {
                return std::nullopt;
            }
            const auto scaled = delta->Multiply(
                epoch.source_rate,
                epoch.output_rate);
            if (!scaled.has_value())
            {
                return std::nullopt;
            }
            const auto source = source_origin->Add(*scaled);
            return source.has_value()
                       ? std::optional<gc::timing::CheckedRational>(*source)
                       : std::nullopt;
        }

        std::optional<gc::timing::CheckedRational> ExactEpochTail(
            const ExactPlaybackEpoch& epoch) noexcept
        {
            if (epoch.closed_source_tail.has_value())
            {
                return epoch.closed_source_tail;
            }
            const auto output_tail = WholeUnsigned(epoch.mapped_output_tail);
            return output_tail.has_value()
                       ? SourceAtOutput(epoch, *output_tail)
                       : std::nullopt;
        }
    } // namespace

    void AudioCursorTimeline::Publish(const AudioRenderSpan& span) noexcept
    {
        const auto generation = writer_generation_++;
        auto& slot = slots_[generation % kRenderSpanCapacity];
        const auto writing = generation * 2 + 1;
        slot.sequence.store(writing, detail::kRenderSpanAtomicOrder);
        StoreSpan(slot.span, span);
        slot.sequence.store(writing + 1, detail::kRenderSpanAtomicOrder);
        published_generation_.store(
            generation + 1,
            detail::kRenderSpanAtomicOrder);
    }

    AudioCursorResolution AudioCursorTimeline::ResolveSourceFrame(
        std::uint64_t output_frame,
        std::uint64_t requested_generation,
        std::uint64_t source_length_frames) const noexcept
    {
        if (source_length_frames == 0)
        {
            return {AudioCursorResolutionKind::Unmapped, 0, 0};
        }

        const auto published = published_generation_.load(
            detail::kRenderSpanAtomicOrder);
        const auto available = std::min<std::uint64_t>(
            published,
            kRenderSpanCapacity);
        bool generation_seen{};
        auto earliest_begin = std::numeric_limits<std::uint64_t>::max();

        for (std::uint64_t offset = 0; offset < available; ++offset)
        {
            const auto generation = published - offset - 1;
            const auto expected_sequence = StableSequence(generation);
            const auto& slot = slots_[generation % kRenderSpanCapacity];

            std::optional<AudioRenderSpan> stable_span;
            for (int attempt = 0; attempt < 3; ++attempt)
            {
                const auto before =
                    slot.sequence.load(detail::kRenderSpanAtomicOrder);
                if (before != expected_sequence || (before & 1U) != 0)
                {
                    continue;
                }

                const auto candidate = LoadSpan(slot.span);
                const auto after =
                    slot.sequence.load(detail::kRenderSpanAtomicOrder);
                if (before == after && after == expected_sequence)
                {
                    stable_span = candidate;
                    break;
                }
            }

            if (!stable_span.has_value())
            {
                continue;
            }

            const auto& span = *stable_span;
            if (span.output_frame_end <= span.output_frame_begin ||
                span.source_frame_end_unwrapped <
                span.source_frame_begin_unwrapped ||
                span.epoch != requested_generation)
            {
                continue;
            }

            generation_seen = true;
            earliest_begin = std::min(
                earliest_begin,
                span.output_frame_begin);
            if (
                output_frame < span.output_frame_begin ||
                output_frame >= span.output_frame_end)
            {
                continue;
            }

            const auto scaled = ScaleFloor(
                output_frame - span.output_frame_begin,
                span.source_frame_end_unwrapped -
                span.source_frame_begin_unwrapped,
                span.output_frame_end - span.output_frame_begin);
            if (!scaled.has_value() || *scaled >
                std::numeric_limits<std::uint64_t>::max() -
                span.source_frame_begin_unwrapped)
            {
                return {AudioCursorResolutionKind::Unmapped, 0, 0};
            }

            const auto source_frame_unwrapped =
                span.source_frame_begin_unwrapped + *scaled;
            return {
                AudioCursorResolutionKind::Resolved,
                source_frame_unwrapped % source_length_frames,
                source_frame_unwrapped,
            };
        }

        return !generation_seen || output_frame < earliest_begin
                   ? AudioCursorResolution{
                       AudioCursorResolutionKind::PendingGeneration, 0, 0
                   }
                   : AudioCursorResolution{AudioCursorResolutionKind::Unmapped, 0, 0};
    }

    bool AudioCursorTimeline::BeginExactPublication(
        std::uint64_t* writing) noexcept
    {
        if (writing == nullptr)
        {
            return false;
        }
        const auto stable = exact_publication_sequence_.load(
            detail::kRenderSpanAtomicOrder);
        if ((stable & 1U) != 0 ||
            stable > std::numeric_limits<std::uint64_t>::max() - 2)
        {
            return false;
        }
        *writing = stable + 1;
        exact_publication_sequence_.store(
            *writing,
            detail::kRenderSpanAtomicOrder);
        return true;
    }

    void AudioCursorTimeline::EndExactPublication(
        std::uint64_t writing) noexcept
    {
        exact_publication_sequence_.store(
            writing + 1,
            detail::kRenderSpanAtomicOrder);
    }

    // This helper mutates the logical timeline through its allocated slot table.
    // ReSharper disable once CppMemberFunctionMayBeConst
    bool AudioCursorTimeline::StoreExactSlot(
        std::size_t index,
        const ExactPlaybackEpoch& epoch) noexcept
    {
        if (exact_slots_ == nullptr || index >= exact_slots_->size())
        {
            return false;
        }
        auto& slot = (*exact_slots_)[index];
        const auto stable = slot.version.load(detail::kRenderSpanAtomicOrder);
        if ((stable & 1U) != 0 ||
            stable > std::numeric_limits<std::uint64_t>::max() - 2)
        {
            return false;
        }
        const auto writing = stable + 1;
        slot.version.store(writing, detail::kRenderSpanAtomicOrder);
        slot.buffer_instance_id.store(
            epoch.buffer_instance_id, detail::kRenderSpanAtomicOrder);
        slot.timeline_generation.store(
            epoch.timeline_generation, detail::kRenderSpanAtomicOrder);
        slot.playback_generation.store(
            epoch.playback_generation, detail::kRenderSpanAtomicOrder);
        slot.origin.store(
            static_cast<std::uint8_t>(epoch.origin),
            detail::kRenderSpanAtomicOrder);
        slot.output_origin.store(
            epoch.output_origin, detail::kRenderSpanAtomicOrder);
        slot.source_origin.store(
            epoch.source_origin, detail::kRenderSpanAtomicOrder);
        slot.output_rate.store(
            epoch.output_rate, detail::kRenderSpanAtomicOrder);
        slot.source_rate.store(
            epoch.source_rate, detail::kRenderSpanAtomicOrder);
        slot.mapped_output_tail.store(
            epoch.mapped_output_tail, detail::kRenderSpanAtomicOrder);
        slot.closure_engaged.store(
            epoch.closure.has_value(), detail::kRenderSpanAtomicOrder);
        slot.closure.store(
            epoch.closure.has_value()
                ? static_cast<std::uint8_t>(*epoch.closure)
                : 0,
            detail::kRenderSpanAtomicOrder);
        slot.closed_tail_engaged.store(
            epoch.closed_source_tail.has_value(),
            detail::kRenderSpanAtomicOrder);
        slot.closed_tail_numerator.store(
            epoch.closed_source_tail.has_value()
                ? epoch.closed_source_tail->numerator()
                : 0,
            detail::kRenderSpanAtomicOrder);
        slot.closed_tail_denominator.store(
            epoch.closed_source_tail.has_value()
                ? epoch.closed_source_tail->denominator()
                : 1,
            detail::kRenderSpanAtomicOrder);
        slot.version.store(writing + 1, detail::kRenderSpanAtomicOrder);
        return true;
    }

    std::optional<ExactPlaybackEpoch> AudioCursorTimeline::LoadExactSlot(
        std::size_t index) const noexcept
    {
        if (exact_slots_ == nullptr || index >= exact_slots_->size())
        {
            return std::nullopt;
        }
        const auto& slot = (*exact_slots_)[index];
        for (int attempt = 0; attempt < 3; ++attempt)
        {
            const auto before = slot.version.load(
                detail::kRenderSpanAtomicOrder);
            if (before == 0 || (before & 1U) != 0)
            {
                continue;
            }
            const auto buffer_instance_id = slot.buffer_instance_id.load(
                detail::kRenderSpanAtomicOrder);
            const auto timeline_generation = slot.timeline_generation.load(
                detail::kRenderSpanAtomicOrder);
            const auto playback_generation = slot.playback_generation.load(
                detail::kRenderSpanAtomicOrder);
            const auto origin_value = slot.origin.load(
                detail::kRenderSpanAtomicOrder);
            const auto output_origin = slot.output_origin.load(
                detail::kRenderSpanAtomicOrder);
            const auto source_origin = slot.source_origin.load(
                detail::kRenderSpanAtomicOrder);
            const auto output_rate = slot.output_rate.load(
                detail::kRenderSpanAtomicOrder);
            const auto source_rate = slot.source_rate.load(
                detail::kRenderSpanAtomicOrder);
            const auto mapped_output_tail = slot.mapped_output_tail.load(
                detail::kRenderSpanAtomicOrder);
            const auto closure_engaged = slot.closure_engaged.load(
                detail::kRenderSpanAtomicOrder);
            const auto closure_value = slot.closure.load(
                detail::kRenderSpanAtomicOrder);
            const auto closed_tail_engaged = slot.closed_tail_engaged.load(
                detail::kRenderSpanAtomicOrder);
            const auto closed_tail_numerator = slot.closed_tail_numerator.load(
                detail::kRenderSpanAtomicOrder);
            const auto closed_tail_denominator = slot.closed_tail_denominator.load(
                detail::kRenderSpanAtomicOrder);
            const auto after = slot.version.load(
                detail::kRenderSpanAtomicOrder);
            if (before != after || (after & 1U) != 0 ||
                origin_value > static_cast<std::uint8_t>(
                    ExactPlaybackOrigin::Seek) ||
                (closure_engaged && closure_value >
                    static_cast<std::uint8_t>(
                        ExactPlaybackClosure::WriterQuiescedRelease)))
            {
                continue;
            }

            ExactPlaybackEpoch epoch{
                .buffer_instance_id = buffer_instance_id,
                .timeline_generation = timeline_generation,
                .playback_generation = playback_generation,
                .origin = static_cast<ExactPlaybackOrigin>(origin_value),
                .output_origin = output_origin,
                .source_origin = source_origin,
                .output_rate = output_rate,
                .source_rate = source_rate,
                .mapped_output_tail = mapped_output_tail,
            };
            if (closure_engaged)
            {
                epoch.closure = static_cast<ExactPlaybackClosure>(closure_value);
            }
            if (closed_tail_engaged)
            {
                const auto rational = gc::timing::CheckedRational::Create(
                    closed_tail_numerator,
                    closed_tail_denominator);
                if (!rational.has_value())
                {
                    continue;
                }
                epoch.closed_source_tail = *rational;
            }
            return epoch;
        }
        return std::nullopt;
    }

    bool AudioCursorTimeline::ConfigureExactPlaybackHistory(
        std::uint64_t buffer_instance_id,
        std::uint64_t timeline_generation) noexcept
    {
        if (buffer_instance_id == 0 || timeline_generation == 0 ||
            HasExactPlaybackHistory() ||
            exact_buffer_instance_id_.load(detail::kRenderSpanAtomicOrder) !=
            buffer_instance_id)
        {
            return false;
        }
        // The nothrow allocation is required by this noexcept API.
        // ReSharper disable once CppSmartPointerVsMakeFunction
        auto slots = std::unique_ptr<
            std::array<ExactSlot, kExactPlaybackEpochCapacity>>(
            new(std::nothrow)
            std::array<ExactSlot, kExactPlaybackEpochCapacity>());
        if (slots == nullptr)
        {
            return false;
        }
        exact_timeline_generation_.store(
            timeline_generation, detail::kRenderSpanAtomicOrder);
        exact_slots_ = std::move(slots);
        bool expected = false;
        if (!exact_configured_.compare_exchange_strong(
            expected,
            true,
            detail::kRenderSpanAtomicOrder,
            detail::kRenderSpanAtomicOrder))
        {
            return false;
        }
        return true;
    }

    bool AudioCursorTimeline::AssignBufferInstanceId(
        std::uint64_t buffer_instance_id) noexcept
    {
        if (buffer_instance_id == 0 || HasExactPlaybackHistory())
        {
            return false;
        }
        std::uint64_t expected{};
        return exact_buffer_instance_id_.compare_exchange_strong(
            expected,
            buffer_instance_id,
            detail::kRenderSpanAtomicOrder,
            detail::kRenderSpanAtomicOrder);
    }

    bool AudioCursorTimeline::HasExactPlaybackHistory() const noexcept
    {
        return exact_configured_.load(detail::kRenderSpanAtomicOrder);
    }

    std::uint64_t AudioCursorTimeline::exact_buffer_instance_id() const noexcept
    {
        return exact_buffer_instance_id_.load(detail::kRenderSpanAtomicOrder);
    }

    std::uint64_t AudioCursorTimeline::exact_timeline_generation() const noexcept
    {
        return exact_timeline_generation_.load(detail::kRenderSpanAtomicOrder);
    }

    bool AudioCursorTimeline::ExpectExactPlaybackGeneration(
        std::uint64_t playback_generation) noexcept
    {
        if (!HasExactPlaybackHistory() || playback_generation == 0)
        {
            return false;
        }
        auto observed = exact_requested_generation_.load(
            detail::kRenderSpanAtomicOrder);
        while (observed < playback_generation &&
            !exact_requested_generation_.compare_exchange_weak(
                observed,
                playback_generation,
                detail::kRenderSpanAtomicOrder,
                detail::kRenderSpanAtomicOrder))
        {
        }
        return observed <= playback_generation;
    }

    bool AudioCursorTimeline::FailExactMappedSpanPublication(
        ExactMappedSpanPublicationFailure reason,
        std::uint64_t expected,
        std::uint64_t actual) noexcept
    {
        exact_discontinuous_.store(true, detail::kRenderSpanAtomicOrder);

        bool unclaimed = false;
        if (exact_mapped_span_failure_claimed_.compare_exchange_strong(
            unclaimed,
            true,
            detail::kRenderSpanAtomicOrder,
            detail::kRenderSpanAtomicOrder))
        {
            exact_mapped_span_failure_expected_.store(
                expected, detail::kRenderSpanAtomicOrder);
            exact_mapped_span_failure_actual_.store(
                actual, detail::kRenderSpanAtomicOrder);
            exact_mapped_span_failure_reason_.store(
                static_cast<std::uint8_t>(reason),
                detail::kRenderSpanAtomicOrder);
        }
        return false;
    }

    ExactMappedSpanPublicationFailureSnapshot
    AudioCursorTimeline::exact_mapped_span_publication_failure() const noexcept
    {
        const auto reason = static_cast<ExactMappedSpanPublicationFailure>(
            exact_mapped_span_failure_reason_.load(detail::kRenderSpanAtomicOrder));
        if (reason == ExactMappedSpanPublicationFailure::None)
        {
            return {};
        }
        return {
            reason,
            exact_mapped_span_failure_expected_.load(
                detail::kRenderSpanAtomicOrder),
            exact_mapped_span_failure_actual_.load(
                detail::kRenderSpanAtomicOrder),
        };
    }

    bool AudioCursorTimeline::PublishExactMappedSpan(
        std::uint64_t playback_generation,
        ExactPlaybackOrigin origin,
        std::uint64_t output_origin,
        std::uint64_t source_origin,
        std::uint32_t output_rate,
        std::uint32_t source_rate,
        std::uint64_t mapped_output_tail,
        bool natural_end,
        std::uint64_t natural_source_tail) noexcept
    {
        if (!HasExactPlaybackHistory() || playback_generation == 0 ||
            output_rate == 0 || source_rate == 0 ||
            mapped_output_tail <= output_origin)
        {
            return FailExactMappedSpanPublication(
                ExactMappedSpanPublicationFailure::InvalidArguments,
                output_origin,
                mapped_output_tail);
        }

        ExactPlaybackEpoch epoch{
            .buffer_instance_id = exact_buffer_instance_id(),
            .timeline_generation = exact_timeline_generation(),
            .playback_generation = playback_generation,
            .origin = origin,
            .output_origin = output_origin,
            .source_origin = source_origin,
            .output_rate = output_rate,
            .source_rate = source_rate,
            .mapped_output_tail = mapped_output_tail,
        };
        if (natural_end)
        {
            epoch.closure = ExactPlaybackClosure::NaturalEnd;
            epoch.closed_source_tail = WholeUnsigned(natural_source_tail);
            if (!epoch.closed_source_tail.has_value())
            {
                return FailExactMappedSpanPublication(
                    ExactMappedSpanPublicationFailure::
                    NaturalEndTailUnrepresentable,
                    static_cast<std::uint64_t>(
                        std::numeric_limits<std::int64_t>::max()),
                    natural_source_tail);
            }
        }

        std::optional<ExactPlaybackEpoch> previous;
        bool new_epoch = !exact_writer_has_current_ ||
            exact_writer_current_generation_ != playback_generation;
        if (new_epoch &&
            exact_writer_epoch_count_ ==
            std::numeric_limits<std::uint64_t>::max())
        {
            return FailExactMappedSpanPublication(
                ExactMappedSpanPublicationFailure::EpochCounterOverflow,
                std::numeric_limits<std::uint64_t>::max() - 1,
                exact_writer_epoch_count_);
        }
        if (new_epoch && exact_writer_has_current_)
        {
            if (playback_generation <= exact_writer_current_generation_)
            {
                return FailExactMappedSpanPublication(
                    ExactMappedSpanPublicationFailure::
                    PlaybackGenerationNotIncreasing,
                    exact_writer_current_generation_,
                    playback_generation);
            }
            previous = LoadExactSlot(exact_writer_current_slot_);
            if (!previous.has_value())
            {
                return FailExactMappedSpanPublication(
                    ExactMappedSpanPublicationFailure::PreviousEpochUnavailable,
                    exact_writer_current_slot_);
            }
            if (previous->closure.has_value() &&
                previous->closure != ExactPlaybackClosure::NaturalEnd)
            {
                return FailExactMappedSpanPublication(
                    ExactMappedSpanPublicationFailure::PreviousEpochAlreadyClosed,
                    static_cast<std::uint64_t>(
                        ExactPlaybackClosure::NaturalEnd),
                    static_cast<std::uint64_t>(*previous->closure));
            }
            if (!previous->closure.has_value())
            {
                previous->closure = ExactPlaybackClosure::LaterEpoch;
                previous->closed_source_tail = ExactEpochTail(*previous);
                if (!previous->closed_source_tail.has_value())
                {
                    return FailExactMappedSpanPublication(
                        ExactMappedSpanPublicationFailure::
                        PreviousEpochTailUnrepresentable);
                }
            }
        }
        else if (!new_epoch)
        {
            const auto current = LoadExactSlot(exact_writer_current_slot_);
            if (!current.has_value())
            {
                return FailExactMappedSpanPublication(
                    ExactMappedSpanPublicationFailure::CurrentEpochUnavailable,
                    exact_writer_current_slot_);
            }
            if (current->closure.has_value())
            {
                return FailExactMappedSpanPublication(
                    ExactMappedSpanPublicationFailure::CurrentEpochClosed,
                    0,
                    static_cast<std::uint64_t>(*current->closure) + 1);
            }
            if (current->buffer_instance_id != epoch.buffer_instance_id)
            {
                return FailExactMappedSpanPublication(
                    ExactMappedSpanPublicationFailure::BufferInstanceChanged,
                    current->buffer_instance_id,
                    epoch.buffer_instance_id);
            }
            if (current->timeline_generation != epoch.timeline_generation)
            {
                return FailExactMappedSpanPublication(
                    ExactMappedSpanPublicationFailure::TimelineGenerationChanged,
                    current->timeline_generation,
                    epoch.timeline_generation);
            }
            if (current->playback_generation != epoch.playback_generation)
            {
                return FailExactMappedSpanPublication(
                    ExactMappedSpanPublicationFailure::PlaybackGenerationChanged,
                    current->playback_generation,
                    epoch.playback_generation);
            }
            if (current->origin != epoch.origin)
            {
                return FailExactMappedSpanPublication(
                    ExactMappedSpanPublicationFailure::OriginChanged,
                    static_cast<std::uint64_t>(current->origin),
                    static_cast<std::uint64_t>(epoch.origin));
            }
            if (current->output_origin != epoch.output_origin)
            {
                return FailExactMappedSpanPublication(
                    ExactMappedSpanPublicationFailure::OutputOriginChanged,
                    current->output_origin,
                    epoch.output_origin);
            }
            if (current->source_origin != epoch.source_origin)
            {
                return FailExactMappedSpanPublication(
                    ExactMappedSpanPublicationFailure::SourceOriginChanged,
                    current->source_origin,
                    epoch.source_origin);
            }
            if (current->output_rate != epoch.output_rate)
            {
                return FailExactMappedSpanPublication(
                    ExactMappedSpanPublicationFailure::OutputRateChanged,
                    current->output_rate,
                    epoch.output_rate);
            }
            if (current->source_rate != epoch.source_rate)
            {
                return FailExactMappedSpanPublication(
                    ExactMappedSpanPublicationFailure::SourceRateChanged,
                    current->source_rate,
                    epoch.source_rate);
            }
            if (mapped_output_tail <= current->mapped_output_tail)
            {
                return FailExactMappedSpanPublication(
                    ExactMappedSpanPublicationFailure::
                    MappedOutputTailNotIncreasing,
                    current->mapped_output_tail,
                    mapped_output_tail);
            }
            epoch = *current;
            epoch.mapped_output_tail = mapped_output_tail;
            if (natural_end)
            {
                epoch.closure = ExactPlaybackClosure::NaturalEnd;
                epoch.closed_source_tail = WholeUnsigned(natural_source_tail);
                if (!epoch.closed_source_tail.has_value())
                {
                    return FailExactMappedSpanPublication(
                        ExactMappedSpanPublicationFailure::
                        NaturalEndTailUnrepresentable,
                        static_cast<std::uint64_t>(
                            std::numeric_limits<std::int64_t>::max()),
                        natural_source_tail);
                }
            }
        }

        const auto slot_index = new_epoch
                                    ? static_cast<std::size_t>(
                                        exact_writer_epoch_count_ % kExactPlaybackEpochCapacity)
                                    : exact_writer_current_slot_;
        std::uint64_t writing{};
        if (!BeginExactPublication(&writing))
        {
            return FailExactMappedSpanPublication(
                ExactMappedSpanPublicationFailure::
                PublicationSequenceUnavailable);
        }
        bool stored = true;
        auto failed_slot = slot_index;
        if (previous.has_value())
        {
            stored = StoreExactSlot(
                exact_writer_current_slot_, *previous);
            if (!stored)
            {
                failed_slot = exact_writer_current_slot_;
            }
        }
        const auto epoch_stored = StoreExactSlot(slot_index, epoch);
        if (!epoch_stored && stored)
        {
            failed_slot = slot_index;
        }
        stored = epoch_stored && stored;
        if (stored && new_epoch)
        {
            ++exact_writer_epoch_count_;
            exact_writer_current_generation_ = playback_generation;
            exact_writer_current_slot_ = slot_index;
            exact_writer_has_current_ = true;
            exact_published_count_.store(
                exact_writer_epoch_count_, detail::kRenderSpanAtomicOrder);
            if (exact_writer_epoch_count_ > kExactPlaybackEpochCapacity)
            {
                exact_prefix_evicted_.store(
                    true, detail::kRenderSpanAtomicOrder);
            }
        }
        EndExactPublication(writing);
        if (!stored)
        {
            return FailExactMappedSpanPublication(
                ExactMappedSpanPublicationFailure::SlotStoreFailed,
                failed_slot);
        }
        return true;
    }

    bool AudioCursorTimeline::CloseExactWriterAfterQuiescence() noexcept
    {
        if (!HasExactPlaybackHistory() || !exact_writer_has_current_)
        {
            return true;
        }
        auto epoch = LoadExactSlot(exact_writer_current_slot_);
        if (!epoch.has_value())
        {
            exact_discontinuous_.store(true, detail::kRenderSpanAtomicOrder);
            return false;
        }
        if (epoch->closure.has_value())
        {
            std::uint64_t writing{};
            if (!BeginExactPublication(&writing))
            {
                exact_discontinuous_.store(true, detail::kRenderSpanAtomicOrder);
                return false;
            }
            exact_requested_generation_.store(
                epoch->playback_generation,
                detail::kRenderSpanAtomicOrder);
            EndExactPublication(writing);
            return true;
        }
        epoch->closure = ExactPlaybackClosure::WriterQuiescedRelease;
        epoch->closed_source_tail = ExactEpochTail(*epoch);
        if (!epoch->closed_source_tail.has_value())
        {
            exact_discontinuous_.store(true, detail::kRenderSpanAtomicOrder);
            return false;
        }
        std::uint64_t writing{};
        if (!BeginExactPublication(&writing))
        {
            exact_discontinuous_.store(true, detail::kRenderSpanAtomicOrder);
            return false;
        }
        const auto stored = StoreExactSlot(exact_writer_current_slot_, *epoch);
        if (stored)
        {
            exact_requested_generation_.store(
                epoch->playback_generation,
                detail::kRenderSpanAtomicOrder);
        }
        EndExactPublication(writing);
        if (!stored)
        {
            exact_discontinuous_.store(true, detail::kRenderSpanAtomicOrder);
        }
        return stored;
    }

    std::size_t AudioCursorTimeline::CopyExactPlaybackEpochs(
        std::span<ExactPlaybackEpoch> output,
        ExactPlaybackHistoryStatus* status) const noexcept
    {
        const auto set_status = [status](
            ExactClockStatus value,
            std::uint64_t sequence,
            bool prefix_evicted) noexcept
        {
            if (status != nullptr)
            {
                *status = {value, sequence, prefix_evicted};
            }
        };
        if (!HasExactPlaybackHistory())
        {
            set_status(ExactClockStatus::NoPlayback, 0, false);
            return 0;
        }

        for (int attempt = 0; attempt < 3; ++attempt)
        {
            const auto before = exact_publication_sequence_.load(
                detail::kRenderSpanAtomicOrder);
            if ((before & 1U) != 0)
            {
                continue;
            }
            const auto published = exact_published_count_.load(
                detail::kRenderSpanAtomicOrder);
            const auto prefix_evicted = exact_prefix_evicted_.load(
                detail::kRenderSpanAtomicOrder);
            const auto discontinuous = exact_discontinuous_.load(
                detail::kRenderSpanAtomicOrder);
            const auto retained = std::min<std::uint64_t>(
                published, kExactPlaybackEpochCapacity);
            const auto copied = std::min<std::uint64_t>(retained, output.size());
            const auto first = published - copied;
            bool stable = true;
            for (std::uint64_t offset = 0; offset < copied; ++offset)
            {
                const auto epoch = LoadExactSlot(static_cast<std::size_t>(
                    (first + offset) % kExactPlaybackEpochCapacity));
                if (!epoch.has_value())
                {
                    stable = false;
                    break;
                }
                output[static_cast<std::size_t>(offset)] = *epoch;
            }
            const auto after = exact_publication_sequence_.load(
                detail::kRenderSpanAtomicOrder);
            if (!stable || before != after || (after & 1U) != 0)
            {
                continue;
            }
            if (published == 0)
            {
                set_status(ExactClockStatus::Pending, after, prefix_evicted);
                return 0;
            }
            if (discontinuous)
            {
                set_status(ExactClockStatus::Discontinuous, after, prefix_evicted);
                return static_cast<std::size_t>(copied);
            }
            if (copied != retained)
            {
                set_status(ExactClockStatus::HistoryLost, after, true);
                return static_cast<std::size_t>(copied);
            }
            set_status(ExactClockStatus::Resolved, after, prefix_evicted);
            return static_cast<std::size_t>(copied);
        }

        set_status(
            ExactClockStatus::TemporarilyUnavailable,
            exact_publication_sequence_.load(detail::kRenderSpanAtomicOrder),
            exact_prefix_evicted_.load(detail::kRenderSpanAtomicOrder));
        return 0;
    }

    ExactSourceFrameResult AudioCursorTimeline::ResolveExactSourceFrame(
        const gc::timing::CheckedRational& output) const noexcept
    {
        std::array<ExactPlaybackEpoch, kExactPlaybackEpochCapacity> epochs{};
        ExactPlaybackHistoryStatus history{};
        const auto count = CopyExactPlaybackEpochs(epochs, &history);
        ExactSourceFrameResult result{.status = history.status};
        if (history.status != ExactClockStatus::Resolved)
        {
            return result;
        }

        const auto requested = exact_requested_generation_.load(
            detail::kRenderSpanAtomicOrder);
        if (count == 0)
        {
            result.status = ExactClockStatus::Pending;
            return result;
        }
        for (std::size_t index = 0; index < count; ++index)
        {
            const auto& epoch = epochs[index];
            const bool has_closure = epoch.closure.has_value();
            const bool has_closed_tail = epoch.closed_source_tail.has_value();
            if (epoch.buffer_instance_id == 0 ||
                epoch.timeline_generation == 0 ||
                epoch.playback_generation == 0 || epoch.output_rate == 0 ||
                epoch.source_rate == 0 ||
                epoch.mapped_output_tail <= epoch.output_origin ||
                has_closure != has_closed_tail ||
                (has_closed_tail &&
                    epoch.closed_source_tail->numerator() < 0))
            {
                result.status = ExactClockStatus::Discontinuous;
                return result;
            }
            if (has_closure &&
                *epoch.closure != ExactPlaybackClosure::NaturalEnd)
            {
                const auto output_tail = WholeUnsigned(epoch.mapped_output_tail);
                const auto derived_tail = output_tail.has_value()
                                              ? SourceAtOutput(epoch, *output_tail)
                                              : std::nullopt;
                if (!derived_tail.has_value() ||
                    derived_tail->Compare(*epoch.closed_source_tail) != 0)
                {
                    result.status = ExactClockStatus::Discontinuous;
                    return result;
                }
            }
            if (index == 0)
            {
                continue;
            }
            const auto& previous = epochs[index - 1];
            if (epoch.buffer_instance_id != previous.buffer_instance_id ||
                epoch.timeline_generation != previous.timeline_generation ||
                epoch.output_rate != previous.output_rate ||
                epoch.source_rate != previous.source_rate ||
                epoch.playback_generation <= previous.playback_generation ||
                epoch.output_origin < previous.output_origin ||
                !previous.closure.has_value() ||
                previous.closure ==
                ExactPlaybackClosure::WriterQuiescedRelease)
            {
                result.status = ExactClockStatus::Discontinuous;
                return result;
            }
        }
        if (epochs[count - 1].closure == ExactPlaybackClosure::LaterEpoch)
        {
            result.status = ExactClockStatus::Discontinuous;
            return result;
        }
        const auto& newest = epochs[count - 1];
        result.buffer_instance_id = newest.buffer_instance_id;
        result.playback_generation = newest.playback_generation;
        const bool generation_pending =
            requested > newest.playback_generation &&
            newest.closure !=
            std::optional<ExactPlaybackClosure>(
                ExactPlaybackClosure::WriterQuiescedRelease);

        std::optional<ExactSourceCoordinate> resolved;
        std::uint64_t resolved_generation{};
        for (std::size_t index = 0; index < count; ++index)
        {
            const auto& epoch = epochs[index];
            const auto origin = WholeUnsigned(epoch.output_origin);
            const auto tail = WholeUnsigned(epoch.mapped_output_tail);
            if (!origin.has_value() || !tail.has_value() ||
                epoch.mapped_output_tail < epoch.output_origin ||
                (index != 0 && epoch.output_origin <
                    epochs[index - 1].output_origin))
            {
                result.status = ExactClockStatus::Discontinuous;
                return result;
            }
            if (output.Compare(*origin) < 0 || output.Compare(*tail) >= 0)
            {
                continue;
            }
            const auto source = SourceAtOutput(epoch, output);
            if (!source.has_value())
            {
                result.status = ExactClockStatus::Discontinuous;
                return result;
            }
            const ExactSourceCoordinate candidate{*source, epoch.source_rate};
            if (resolved.has_value() &&
                (resolved->source_rate != candidate.source_rate ||
                    resolved->source_frame.Compare(candidate.source_frame) != 0))
            {
                result.status = ExactClockStatus::Discontinuous;
                return result;
            }
            resolved = candidate;
            resolved_generation = epoch.playback_generation;
            result.buffer_instance_id = epoch.buffer_instance_id;
        }
        if (resolved.has_value())
        {
            result.status = ExactClockStatus::Resolved;
            result.playback_generation = resolved_generation;
            result.resolved = resolved;
            return result;
        }

        const auto oldest_origin = WholeUnsigned(epochs[0].output_origin);
        if (!oldest_origin.has_value())
        {
            result.status = ExactClockStatus::Discontinuous;
            return result;
        }
        if (output.Compare(*oldest_origin) < 0)
        {
            result.status = history.prefix_evicted
                                ? ExactClockStatus::HistoryLost
                                : ExactClockStatus::OutsidePlayback;
            return result;
        }
        const ExactPlaybackEpoch* preceding{};
        bool bounded_by_later_origin{};
        for (std::size_t index = 0; index < count; ++index)
        {
            const auto tail = WholeUnsigned(epochs[index].mapped_output_tail);
            if (!tail.has_value())
            {
                result.status = ExactClockStatus::Discontinuous;
                return result;
            }
            if (output.Compare(*tail) >= 0)
            {
                preceding = &epochs[index];
            }
            if (index + 1 < count)
            {
                const auto next_origin = WholeUnsigned(
                    epochs[index + 1].output_origin);
                if (!next_origin.has_value())
                {
                    result.status = ExactClockStatus::Discontinuous;
                    return result;
                }
                if (output.Compare(*tail) >= 0 &&
                    output.Compare(*next_origin) < 0)
                {
                    bounded_by_later_origin = true;
                    break;
                }
            }
        }

        const bool closed_after_tail = preceding != nullptr &&
            preceding == &epochs[count - 1] &&
            preceding->closure.has_value();
        if (generation_pending && !bounded_by_later_origin)
        {
            result.status = ExactClockStatus::Pending;
            return result;
        }
        if (!bounded_by_later_origin && !closed_after_tail)
        {
            result.status = ExactClockStatus::Pending;
            return result;
        }

        if (preceding == nullptr || !preceding->closure.has_value())
        {
            result.status = ExactClockStatus::Discontinuous;
            return result;
        }
        const auto frontier = ExactEpochTail(*preceding);
        if (!frontier.has_value())
        {
            result.status = ExactClockStatus::Discontinuous;
            return result;
        }
        result.status = ExactClockStatus::OutsidePlayback;
        result.buffer_instance_id = preceding->buffer_instance_id;
        result.playback_generation = preceding->playback_generation;
        result.closed_frontier = ExactSourceCoordinate{
            *frontier,
            preceding->source_rate,
        };
        return result;
    }

    void EndpointClockMapper::Reset(
        std::uint64_t position,
        std::uint64_t frequency,
        std::uint64_t output_frame,
        std::uint32_t output_sample_rate) noexcept
    {
        origin_position_ = position;
        frequency_ = frequency;
        origin_output_frame_ = output_frame;
        output_sample_rate_ = output_sample_rate;
    }

    std::optional<std::uint64_t> EndpointClockMapper::ToOutputFrame(
        std::uint64_t position) const noexcept
    {
        if (frequency_ == 0 || output_sample_rate_ == 0 ||
            position < origin_position_)
        {
            return std::nullopt;
        }

        const auto elapsed_frames = ScaleFloor(
            position - origin_position_,
            output_sample_rate_,
            frequency_);
        if (!elapsed_frames.has_value() || *elapsed_frames >
            std::numeric_limits<std::uint64_t>::max() - origin_output_frame_)
        {
            return std::nullopt;
        }
        return origin_output_frame_ + *elapsed_frames;
    }

    EndpointClockMapping EndpointClockMapper::mapping() const noexcept
    {
        return {
            origin_position_,
            frequency_,
            origin_output_frame_,
            output_sample_rate_,
        };
    }

    void PresentedClockPublication::Publish(
        std::uint64_t presented_output_frame,
        std::uint64_t sample_qpc_100ns,
        std::uint64_t submitted_output_frame_end) noexcept
    {
        if (submitted_output_frame_end == 0 ||
            presented_output_frame >= submitted_output_frame_end)
        {
            Invalidate();
            return;
        }

        const auto generation = writer_generation_++;
        const auto writing = generation * 2 + 1;
        sequence_.store(writing, detail::kRenderSpanAtomicOrder);
        presented_output_frame_.store(
            presented_output_frame, detail::kRenderSpanAtomicOrder);
        sample_qpc_100ns_.store(
            sample_qpc_100ns, detail::kRenderSpanAtomicOrder);
        submitted_output_frame_end_.store(
            submitted_output_frame_end, detail::kRenderSpanAtomicOrder);
        valid_.store(true, detail::kRenderSpanAtomicOrder);
        sequence_.store(writing + 1, detail::kRenderSpanAtomicOrder);
    }

    void PresentedClockPublication::Invalidate() noexcept
    {
        const auto generation = writer_generation_++;
        const auto writing = generation * 2 + 1;
        sequence_.store(writing, detail::kRenderSpanAtomicOrder);
        valid_.store(false, detail::kRenderSpanAtomicOrder);
        sequence_.store(writing + 1, detail::kRenderSpanAtomicOrder);
    }

    std::optional<PresentedClockSnapshot>
    PresentedClockPublication::ReadStable() const noexcept
    {
        for (int attempt = 0; attempt < 3; ++attempt)
        {
            const auto before =
                sequence_.load(detail::kRenderSpanAtomicOrder);
            if ((before & 1U) != 0)
            {
                continue;
            }

            const auto is_valid =
                valid_.load(detail::kRenderSpanAtomicOrder);
            const PresentedClockSnapshot snapshot{
                presented_output_frame_.load(
                    detail::kRenderSpanAtomicOrder),
                sample_qpc_100ns_.load(
                    detail::kRenderSpanAtomicOrder),
                submitted_output_frame_end_.load(
                    detail::kRenderSpanAtomicOrder),
            };
            const auto after =
                sequence_.load(detail::kRenderSpanAtomicOrder);
            if (before == after && (after & 1U) == 0)
            {
                return is_valid
                           ? std::optional<PresentedClockSnapshot>{snapshot}
                           : std::nullopt;
            }
        }
        return std::nullopt;
    }

    std::optional<std::uint64_t>
    PresentedClockPublication::LastReturned() const noexcept
    {
        if (!has_last_returned_.load(std::memory_order_acquire))
        {
            return std::nullopt;
        }
        return last_returned_.load(std::memory_order_acquire);
    }

    std::uint64_t PresentedClockPublication::RememberMonotonic(
        std::uint64_t frame) noexcept
    {
        auto observed = last_returned_.load(std::memory_order_acquire);
        while (observed < frame &&
            !last_returned_.compare_exchange_weak(
                observed,
                frame,
                std::memory_order_acq_rel,
                std::memory_order_acquire))
        {
        }
        has_last_returned_.store(true, std::memory_order_release);
        return std::max(observed, frame);
    }

    std::optional<std::uint64_t>
    PresentedClockPublication::Project(
        std::uint64_t now_qpc_ticks,
        std::uint64_t qpc_frequency,
        std::uint32_t output_sample_rate) noexcept
    {
        constexpr std::uint64_t kReferenceTimePerSecond = 10'000'000;
        const auto snapshot = ReadStable();
        if (!snapshot.has_value() || qpc_frequency == 0 ||
            output_sample_rate == 0)
        {
            return LastReturned();
        }

        const auto now_qpc_100ns = ScaleFloor(
            now_qpc_ticks,
            kReferenceTimePerSecond,
            qpc_frequency);
        if (!now_qpc_100ns.has_value() ||
            *now_qpc_100ns < snapshot->sample_qpc_100ns)
        {
            return LastReturned();
        }

        const auto elapsed_frames = ScaleFloor(
            *now_qpc_100ns - snapshot->sample_qpc_100ns,
            output_sample_rate,
            kReferenceTimePerSecond);
        std::uint64_t projected{};
        if (!elapsed_frames.has_value() ||
            !CheckedAdd(
                snapshot->presented_output_frame,
                *elapsed_frames,
                &projected) ||
            snapshot->submitted_output_frame_end == 0)
        {
            return LastReturned();
        }

        const auto bounded = std::min(
            projected,
            snapshot->submitted_output_frame_end - 1);
        return RememberMonotonic(bounded);
    }

    std::uint64_t SourceFrameToByte(
        std::uint64_t source_frame,
        std::uint16_t block_alignment) noexcept
    {
        constexpr auto max_direct_sound_bytes =
            std::numeric_limits<std::uint32_t>::max();
        if (block_alignment == 0 ||
            source_frame > max_direct_sound_bytes / block_alignment)
        {
            return 0;
        }
        return source_frame * block_alignment;
    }

    std::uint64_t ProjectWriteCursorFrame(
        std::uint64_t play_frame,
        std::uint32_t endpoint_buffer_frames,
        std::uint32_t output_sample_rate,
        std::uint32_t source_rate,
        std::uint64_t source_length_frames) noexcept
    {
        if (source_length_frames == 0 || output_sample_rate == 0)
        {
            return 0;
        }

        const auto source_frames_ahead = ScaleCeil(
            endpoint_buffer_frames,
            source_rate,
            output_sample_rate);
        if (!source_frames_ahead.has_value())
        {
            return 0;
        }

        return AddModulo(
            play_frame % source_length_frames,
            *source_frames_ahead % source_length_frames,
            source_length_frames).remainder;
    }
} // namespace gc::audio
