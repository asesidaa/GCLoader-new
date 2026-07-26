#include "Audio/Diagnostics/AudioFlightRecorder.h"

#include <Windows.h>

#include <algorithm>
#include <limits>
#include <new>
#include <utility>

namespace gc::audio::diagnostics {

namespace {

static_assert(std::atomic<IAudioDiagnosticSink*>::is_always_lock_free);

std::atomic<IAudioDiagnosticSink*> active_sink{};

} // namespace

void ActivateAudioDiagnosticSink(IAudioDiagnosticSink* sink) noexcept {
    if (sink == nullptr) {
        return;
    }

    IAudioDiagnosticSink* expected = nullptr;
    static_cast<void>(active_sink.compare_exchange_strong(
        expected,
        sink,
        std::memory_order_release,
        std::memory_order_relaxed));
}

void DeactivateAudioDiagnosticSink(IAudioDiagnosticSink* sink) noexcept {
    if (sink == nullptr) {
        return;
    }

    auto* expected = sink;
    static_cast<void>(active_sink.compare_exchange_strong(
        expected,
        nullptr,
        std::memory_order_acq_rel,
        std::memory_order_acquire));
}

std::uint64_t CaptureAudioDiagnosticQpcTicks() noexcept {
    LARGE_INTEGER ticks{};
    if (!QueryPerformanceCounter(&ticks) || ticks.QuadPart < 0) {
        return 0;
    }
    return static_cast<std::uint64_t>(ticks.QuadPart);
}

void PublishActiveAudioDiagnosticEvent(
    AudioDiagnosticEvent event) noexcept {
    if (auto* const sink =
            active_sink.load(std::memory_order_acquire);
        sink != nullptr) {
        sink->PublishEvent(event);
    }
}

namespace detail {

struct SpscPcmBlockQueue::Impl {
    struct Header {
        std::uint64_t sequence{};
        SubmittedPcmMetadata metadata{};
    };

    std::size_t capacity{};
    std::size_t samples_per_block{};
    std::unique_ptr<Header[]> headers;
    std::unique_ptr<std::int16_t[]> samples;
    std::uint64_t producer_position{};
    std::uint64_t consumer_position{};
    std::uint64_t next_sequence{};
    std::atomic_uint64_t published_position{};
    std::atomic_uint64_t consumed_position{};
    std::atomic_uint64_t completed_sequence{};
};

SpscPcmBlockQueue::SpscPcmBlockQueue() noexcept = default;
SpscPcmBlockQueue::~SpscPcmBlockQueue() = default;

bool SpscPcmBlockQueue::Initialize(
    std::size_t capacity_blocks,
    std::size_t samples_per_block) noexcept {
    if (capacity_blocks == 0 ||
        samples_per_block == 0 ||
        capacity_blocks >
            std::numeric_limits<std::size_t>::max() /
                samples_per_block) {
        return false;
    }

    auto next = std::unique_ptr<Impl>(new (std::nothrow) Impl);
    if (next == nullptr) {
        return false;
    }
    next->headers.reset(new (std::nothrow) Impl::Header[capacity_blocks]);
    next->samples.reset(new (std::nothrow)
        std::int16_t[capacity_blocks * samples_per_block]);
    if (next->headers == nullptr || next->samples == nullptr) {
        return false;
    }
    next->capacity = capacity_blocks;
    next->samples_per_block = samples_per_block;
    impl_ = std::move(next);
    return true;
}

PcmPublishResult SpscPcmBlockQueue::TryPush(
    const SubmittedPcmMetadata& metadata,
    std::span<const std::int16_t> samples) noexcept {
    if (impl_ == nullptr) {
        return {};
    }

    const auto sequence = impl_->next_sequence++;
    const auto complete = [this, sequence]() noexcept {
        impl_->completed_sequence.store(
            sequence + 1,
            std::memory_order_release);
    };

    if (samples.size() != impl_->samples_per_block) {
        complete();
        return {sequence, false};
    }

    const auto consumed =
        impl_->consumed_position.load(std::memory_order_acquire);
    if (impl_->producer_position - consumed >= impl_->capacity) {
        complete();
        return {sequence, false};
    }

    const auto slot =
        static_cast<std::size_t>(
            impl_->producer_position % impl_->capacity);
    impl_->headers[slot] = {sequence, metadata};
    std::copy(
        samples.begin(),
        samples.end(),
        impl_->samples.get() +
            static_cast<std::ptrdiff_t>(
                slot * impl_->samples_per_block));
    ++impl_->producer_position;
    impl_->published_position.store(
        impl_->producer_position,
        std::memory_order_release);
    complete();
    return {sequence, true};
}

std::optional<PcmBlockView> SpscPcmBlockQueue::TryPeek() noexcept {
    if (impl_ == nullptr ||
        impl_->consumer_position >=
            impl_->published_position.load(std::memory_order_acquire)) {
        return std::nullopt;
    }

    const auto slot =
        static_cast<std::size_t>(
            impl_->consumer_position % impl_->capacity);
    const auto& header = impl_->headers[slot];
    return PcmBlockView{
        header.sequence,
        header.metadata,
        std::span<const std::int16_t>(
            impl_->samples.get() +
                static_cast<std::ptrdiff_t>(
                    slot * impl_->samples_per_block),
            impl_->samples_per_block),
    };
}

void SpscPcmBlockQueue::Pop() noexcept {
    if (impl_ == nullptr ||
        impl_->consumer_position >=
            impl_->published_position.load(std::memory_order_acquire)) {
        return;
    }

    ++impl_->consumer_position;
    impl_->consumed_position.store(
        impl_->consumer_position,
        std::memory_order_release);
}

std::uint64_t SpscPcmBlockQueue::completed_sequence() const noexcept {
    return impl_ == nullptr
        ? 0
        : impl_->completed_sequence.load(std::memory_order_acquire);
}

struct MpscAudioEventQueue::Impl {
    struct Slot {
        std::atomic_uint64_t sequence{};
        AudioDiagnosticEvent event{};
    };

    std::size_t capacity{};
    std::unique_ptr<Slot[]> slots;
    std::atomic_uint64_t enqueue_position{};
    std::uint64_t dequeue_position{};
    std::atomic_uint64_t lost{};
};

MpscAudioEventQueue::MpscAudioEventQueue() noexcept = default;
MpscAudioEventQueue::~MpscAudioEventQueue() = default;

bool MpscAudioEventQueue::Initialize(std::size_t capacity) noexcept {
    if (capacity == 0 ||
        capacity >
            static_cast<std::size_t>(
                std::numeric_limits<std::int64_t>::max())) {
        return false;
    }

    auto next = std::unique_ptr<Impl>(new (std::nothrow) Impl);
    if (next == nullptr) {
        return false;
    }
    next->slots.reset(new (std::nothrow) Impl::Slot[capacity]);
    if (next->slots == nullptr) {
        return false;
    }
    next->capacity = capacity;
    for (std::size_t index = 0; index < capacity; ++index) {
        next->slots[index].sequence.store(
            index,
            std::memory_order_relaxed);
    }
    impl_ = std::move(next);
    return true;
}

EventPublishResult MpscAudioEventQueue::TryPush(
    AudioDiagnosticEvent event) noexcept {
    if (impl_ == nullptr) {
        return EventPublishResult::Dropped;
    }

    auto position =
        impl_->enqueue_position.load(std::memory_order_relaxed);
    for (;;) {
        auto& slot =
            impl_->slots[
                static_cast<std::size_t>(
                    position % impl_->capacity)];
        const auto sequence =
            slot.sequence.load(std::memory_order_acquire);
        const auto difference =
            static_cast<std::int64_t>(sequence - position);
        if (difference == 0) {
            if (impl_->enqueue_position.compare_exchange_weak(
                    position,
                    position + 1,
                    std::memory_order_relaxed,
                    std::memory_order_relaxed)) {
                event.sequence = position;
                slot.event = event;
                slot.sequence.store(
                    position + 1,
                    std::memory_order_release);
                return EventPublishResult::Queued;
            }
            continue;
        }
        if (difference < 0) {
            impl_->lost.fetch_add(1, std::memory_order_relaxed);
            return EventPublishResult::Dropped;
        }
        position =
            impl_->enqueue_position.load(std::memory_order_relaxed);
    }
}

EventReadResult MpscAudioEventQueue::TryRead() noexcept {
    if (impl_ == nullptr) {
        return {};
    }

    const auto position = impl_->dequeue_position;
    auto& slot =
        impl_->slots[
            static_cast<std::size_t>(
                position % impl_->capacity)];
    const auto sequence =
        slot.sequence.load(std::memory_order_acquire);
    if (static_cast<std::int64_t>(
            sequence - (position + 1)) != 0) {
        return {};
    }

    const auto event = slot.event;
    slot.sequence.store(
        position + impl_->capacity,
        std::memory_order_release);
    ++impl_->dequeue_position;
    return {EventReadKind::Ready, event};
}

std::uint64_t MpscAudioEventQueue::lost_events() const noexcept {
    return impl_ == nullptr
        ? 0
        : impl_->lost.load(std::memory_order_relaxed);
}

} // namespace detail

} // namespace gc::audio::diagnostics
