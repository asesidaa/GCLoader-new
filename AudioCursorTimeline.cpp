#include "AudioCursorTimeline.h"

#include "WasapiAudioTypes.h"

#include <algorithm>
#include <atomic>
#include <limits>

namespace gc::audio {
namespace {

static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
static_assert(std::atomic_ref<std::uint64_t>::is_always_lock_free);
static_assert(std::atomic_ref<bool>::is_always_lock_free);

constexpr std::uint64_t StableSequence(
    std::uint64_t generation) noexcept {
    return generation * 2 + 2;
}

struct ModularSum {
    std::uint64_t remainder;
    std::uint64_t quotient;
};

struct ModularProduct {
    std::uint64_t quotient;
    std::uint64_t remainder;
};

ModularSum AddModulo(
    std::uint64_t left,
    std::uint64_t right,
    std::uint64_t denominator) noexcept {
    if (left >= denominator - right) {
        return {left - (denominator - right), 1};
    }
    return {left + right, 0};
}

ModularProduct MultiplyProperFractions(
    std::uint64_t left,
    std::uint64_t right,
    std::uint64_t denominator) noexcept {
    if (left == 0 || right == 0) {
        return {};
    }
    if (left <= std::numeric_limits<std::uint64_t>::max() / right) {
        const auto product = left * right;
        return {product / denominator, product % denominator};
    }

    std::uint64_t quotient{};
    std::uint64_t remainder{};
    for (int bit = 63; bit >= 0; --bit) {
        quotient *= 2;
        const auto doubled = AddModulo(remainder, remainder, denominator);
        remainder = doubled.remainder;
        quotient += doubled.quotient;

        if ((left & (std::uint64_t{1} << bit)) != 0) {
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
    std::uint64_t* result) noexcept {
    if (left != 0 &&
        right > std::numeric_limits<std::uint64_t>::max() / left) {
        return false;
    }
    *result = left * right;
    return true;
}

bool CheckedAdd(
    std::uint64_t value,
    std::uint64_t increment,
    std::uint64_t* result) noexcept {
    if (increment > std::numeric_limits<std::uint64_t>::max() - value) {
        return false;
    }
    *result = value + increment;
    return true;
}

std::optional<std::uint64_t> ScaleFloor(
    std::uint64_t value,
    std::uint64_t numerator,
    std::uint64_t denominator) noexcept {
    if (denominator == 0) {
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
        !CheckedAdd(result, term, &result)) {
        return std::nullopt;
    }

    const auto fractional = MultiplyProperFractions(
        value_remainder,
        numerator_remainder,
        denominator);
    if (!CheckedAdd(result, fractional.quotient, &result)) {
        return std::nullopt;
    }
    return result;
}

std::optional<std::uint64_t> ScaleCeil(
    std::uint64_t value,
    std::uint64_t numerator,
    std::uint64_t denominator) noexcept {
    const auto floor = ScaleFloor(value, numerator, denominator);
    if (!floor.has_value() || denominator == 0) {
        return std::nullopt;
    }

    const auto fractional = MultiplyProperFractions(
        value % denominator,
        numerator % denominator,
        denominator);
    if (fractional.remainder == 0) {
        return floor;
    }

    std::uint64_t ceiling{};
    if (!CheckedAdd(*floor, 1, &ceiling)) {
        return std::nullopt;
    }
    return ceiling;
}

template <typename Value>
void StoreSpanValue(Value& destination, Value value) noexcept {
    std::atomic_ref<Value>(destination).store(
        value,
        detail::kRenderSpanAtomicOrder);
}

template <typename Value>
Value LoadSpanValue(Value& source) noexcept {
    return std::atomic_ref<Value>(source).load(
        detail::kRenderSpanAtomicOrder);
}

void StoreSpan(
    AudioRenderSpan& destination,
    const AudioRenderSpan& source) noexcept {
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

AudioRenderSpan LoadSpan(AudioRenderSpan& source) noexcept {
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

} // namespace

void AudioCursorTimeline::Publish(const AudioRenderSpan& span) noexcept {
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

std::optional<std::uint64_t> AudioCursorTimeline::ResolveSourceFrame(
    std::uint64_t output_frame,
    std::uint64_t epoch,
    std::uint64_t source_length_frames) const noexcept {
    if (source_length_frames == 0) {
        return std::nullopt;
    }

    const auto published = published_generation_.load(
        detail::kRenderSpanAtomicOrder);
    const auto available = std::min<std::uint64_t>(
        published,
        kRenderSpanCapacity);

    for (std::uint64_t offset = 0; offset < available; ++offset) {
        const auto generation = published - offset - 1;
        const auto expected_sequence = StableSequence(generation);
        const auto& slot = slots_[generation % kRenderSpanCapacity];

        std::optional<AudioRenderSpan> stable_span;
        for (int attempt = 0; attempt < 3; ++attempt) {
            const auto before =
                slot.sequence.load(detail::kRenderSpanAtomicOrder);
            if (before != expected_sequence || (before & 1U) != 0) {
                continue;
            }

            const auto candidate = LoadSpan(slot.span);
            const auto after =
                slot.sequence.load(detail::kRenderSpanAtomicOrder);
            if (before == after && after == expected_sequence) {
                stable_span = candidate;
                break;
            }
        }

        if (!stable_span.has_value()) {
            continue;
        }

        const auto& span = *stable_span;
        if (span.epoch != epoch ||
            span.output_frame_end <= span.output_frame_begin ||
            span.source_frame_end_unwrapped <
                span.source_frame_begin_unwrapped ||
            output_frame < span.output_frame_begin ||
            output_frame >= span.output_frame_end) {
            continue;
        }

        const auto scaled = ScaleFloor(
            output_frame - span.output_frame_begin,
            span.source_frame_end_unwrapped -
                span.source_frame_begin_unwrapped,
            span.output_frame_end - span.output_frame_begin);
        if (!scaled.has_value() || *scaled >
            std::numeric_limits<std::uint64_t>::max() -
                span.source_frame_begin_unwrapped) {
            return std::nullopt;
        }

        return (span.source_frame_begin_unwrapped + *scaled) %
            source_length_frames;
    }

    return std::nullopt;
}

void EndpointClockMapper::Reset(
    std::uint64_t position,
    std::uint64_t frequency,
    std::uint64_t output_frame) noexcept {
    origin_position_ = position;
    frequency_ = frequency;
    origin_output_frame_ = output_frame;
}

std::optional<std::uint64_t> EndpointClockMapper::ToOutputFrame(
    std::uint64_t position) const noexcept {
    if (frequency_ == 0 || position < origin_position_) {
        return std::nullopt;
    }

    const auto elapsed_frames = ScaleFloor(
        position - origin_position_,
        kOutputSampleRate,
        frequency_);
    if (!elapsed_frames.has_value() || *elapsed_frames >
        std::numeric_limits<std::uint64_t>::max() - origin_output_frame_) {
        return std::nullopt;
    }
    return origin_output_frame_ + *elapsed_frames;
}

std::uint64_t SourceFrameToByte(
    std::uint64_t source_frame,
    std::uint16_t block_alignment) noexcept {
    constexpr auto max_direct_sound_bytes =
        std::numeric_limits<std::uint32_t>::max();
    if (block_alignment == 0 ||
        source_frame > max_direct_sound_bytes / block_alignment) {
        return 0;
    }
    return source_frame * block_alignment;
}

std::uint64_t ProjectWriteCursorFrame(
    std::uint64_t play_frame,
    std::uint32_t endpoint_buffer_frames,
    std::uint32_t source_rate,
    std::uint64_t source_length_frames) noexcept {
    if (source_length_frames == 0) {
        return 0;
    }

    const auto source_frames_ahead = ScaleCeil(
        endpoint_buffer_frames,
        source_rate,
        kOutputSampleRate);
    if (!source_frames_ahead.has_value()) {
        return 0;
    }

    return AddModulo(
        play_frame % source_length_frames,
        *source_frames_ahead % source_length_frames,
        source_length_frames).remainder;
}

} // namespace gc::audio
