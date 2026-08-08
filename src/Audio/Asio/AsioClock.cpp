// SPDX-License-Identifier: CC0-1.0

#include "Audio/Asio/AsioClock.h"

#include <algorithm>
#include <atomic>
#include <limits>
#include <type_traits>

namespace gc::audio {
namespace {

constexpr std::uint64_t kNanosecondsPerMillisecond = 1'000'000;
constexpr std::uint64_t kOutputFramesPerMillisecond = 48;
constexpr auto kClockAtomicOrder = std::memory_order_seq_cst;

static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
static_assert(std::atomic<bool>::is_always_lock_free);
static_assert(std::atomic_ref<std::uint64_t>::is_always_lock_free);
static_assert(std::atomic_ref<std::uint32_t>::is_always_lock_free);
static_assert(std::atomic_ref<bool>::is_always_lock_free);

std::uint32_t ToWrappingMilliseconds(
    std::uint64_t system_time_ns) noexcept {
    return static_cast<std::uint32_t>(
        system_time_ns / kNanosecondsPerMillisecond);
}

bool IsForwardClockDelta(std::uint32_t elapsed_ms) noexcept {
    return elapsed_ms <=
        static_cast<std::uint32_t>(
            std::numeric_limits<std::int32_t>::max());
}

template <typename Value>
void StoreAnchorValue(Value& destination, Value value) noexcept {
    std::atomic_ref<Value>(destination).store(value, kClockAtomicOrder);
}

template <typename Value>
Value LoadAnchorValue(Value& source) noexcept {
    return std::atomic_ref<Value>(source).load(kClockAtomicOrder);
}

} // namespace

void AsioClockTracker::Reset(
    std::uint32_t buffer_frames,
    std::uint32_t output_latency_frames) noexcept {
    buffer_frames_ = buffer_frames;
    output_latency_frames_ = output_latency_frames;
    previous_sample_position_ = 0;
    previous_system_time_ms_ = 0;
    observation_count_ = 0;
    configured_ = buffer_frames != 0;
    faulted_ = !configured_;
}

AsioClockDecision AsioClockTracker::Fault() noexcept {
    faulted_ = true;
    return {};
}

AsioClockDecision AsioClockTracker::Observe(
    std::uint64_t sample_position,
    std::uint64_t system_time_ns) noexcept {
    if (!configured_ || faulted_ || system_time_ns == 0 ||
        sample_position % buffer_frames_ != 0) {
        return Fault();
    }

    const auto system_time_ms =
        ToWrappingMilliseconds(system_time_ns);
    if (observation_count_ != 0) {
        if (sample_position <= previous_sample_position_ ||
            sample_position - previous_sample_position_ != buffer_frames_) {
            return Fault();
        }

        const auto elapsed_ms =
            system_time_ms - previous_system_time_ms_;
        if (!IsForwardClockDelta(elapsed_ms) ||
            (observation_count_ >= 2 && elapsed_ms == 0)) {
            return Fault();
        }
    }

    if (sample_position >
        std::numeric_limits<std::uint64_t>::max() -
            output_latency_frames_) {
        return Fault();
    }

    previous_sample_position_ = sample_position;
    previous_system_time_ms_ = system_time_ms;
    if (observation_count_ < 3) {
        ++observation_count_;
    }

    const AsioClockDecision decision{
        observation_count_ >= 3
            ? AsioClockDecisionKind::stable
            : AsioClockDecisionKind::priming,
        sample_position,
        sample_position + output_latency_frames_,
        system_time_ns,
    };
    return decision;
}

AsioPresentedClockPublication::AsioPresentedClockPublication(
    AsioClockNowActions actions) noexcept
    : actions_(actions) {}

void AsioPresentedClockPublication::StoreAnchor(
    const Anchor& anchor) noexcept {
    const auto generation = writer_generation_++;
    const auto writing = generation * 2 + 1;
    sequence_.store(writing, kClockAtomicOrder);
    StoreAnchorValue(
        anchor_.presented_output_frame,
        anchor.presented_output_frame);
    StoreAnchorValue(
        anchor_.submitted_output_tail,
        anchor.submitted_output_tail);
    StoreAnchorValue(anchor_.system_time_ms, anchor.system_time_ms);
    StoreAnchorValue(anchor_.valid, anchor.valid);
    sequence_.store(writing + 1, kClockAtomicOrder);
}

void AsioPresentedClockPublication::Publish(
    const AsioClockDecision& decision,
    std::uint64_t submitted_output_tail) noexcept {
    if (decision.kind != AsioClockDecisionKind::stable ||
        decision.system_time_ns == 0 ||
        submitted_output_tail < decision.presented_output_frame) {
        Invalidate();
        return;
    }

    StoreAnchor({
        decision.presented_output_frame,
        submitted_output_tail,
        ToWrappingMilliseconds(decision.system_time_ns),
        true,
    });
}

void AsioPresentedClockPublication::Invalidate() noexcept {
    Anchor invalid{};
    invalid.valid = false;
    StoreAnchor(invalid);
}

std::optional<AsioPresentedClockPublication::Anchor>
AsioPresentedClockPublication::ReadStable() const noexcept {
    static_assert(std::is_trivially_copyable_v<Anchor>);
    for (int attempt = 0; attempt < 3; ++attempt) {
        const auto before = sequence_.load(kClockAtomicOrder);
        if ((before & 1U) != 0) {
            continue;
        }

        const Anchor candidate{
            LoadAnchorValue(anchor_.presented_output_frame),
            LoadAnchorValue(anchor_.submitted_output_tail),
            LoadAnchorValue(anchor_.system_time_ms),
            LoadAnchorValue(anchor_.valid),
        };
        const auto after = sequence_.load(kClockAtomicOrder);
        if (before == after && (after & 1U) == 0) {
            return candidate.valid
                ? std::optional<Anchor>{candidate}
                : std::nullopt;
        }
    }
    return std::nullopt;
}

std::optional<std::uint64_t>
AsioPresentedClockPublication::LastReturned() const noexcept {
    if (!has_last_returned_.load(std::memory_order_acquire)) {
        return std::nullopt;
    }
    return last_returned_.load(std::memory_order_acquire);
}

std::uint64_t AsioPresentedClockPublication::RememberMonotonic(
    std::uint64_t frame) noexcept {
    auto observed = last_returned_.load(std::memory_order_acquire);
    while (observed < frame &&
           !last_returned_.compare_exchange_weak(
               observed,
               frame,
               std::memory_order_acq_rel,
               std::memory_order_acquire)) {
    }
    has_last_returned_.store(true, std::memory_order_release);
    return std::max(observed, frame);
}

std::optional<std::uint64_t>
AsioPresentedClockPublication::CurrentOutputFrame() noexcept {
    const auto anchor = ReadStable();
    if (!anchor.has_value() ||
        actions_.time_get_time_ms == nullptr) {
        return LastReturned();
    }

    const auto now_ms = actions_.time_get_time_ms(actions_.context);
    auto elapsed_ms = now_ms - anchor->system_time_ms;
    if (!IsForwardClockDelta(elapsed_ms)) {
        elapsed_ms = 0;
    }

    const auto available_frames =
        anchor->submitted_output_tail - anchor->presented_output_frame;
    const auto projected_frames = std::min<std::uint64_t>(
        static_cast<std::uint64_t>(elapsed_ms) *
            kOutputFramesPerMillisecond,
        available_frames);
    return RememberMonotonic(
        anchor->presented_output_frame + projected_frames);
}

} // namespace gc::audio
