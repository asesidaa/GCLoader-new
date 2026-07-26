#include "Audio/Diagnostics/AudioFlightRecorder.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>
#include <thread>
#include <type_traits>
#include <vector>

namespace {

namespace diagnostics = gc::audio::diagnostics;

int Expect(bool condition, std::string_view name) {
    if (condition) {
        return 0;
    }

    std::cerr << "Expected " << name << '\n';
    return 1;
}

class FakeSink final : public diagnostics::IAudioDiagnosticSink {
public:
    bool StartSession(
        const diagnostics::AudioFlightRecorderSession&) noexcept override {
        return true;
    }

    void PublishEvent(
        diagnostics::AudioDiagnosticEvent event) noexcept override {
        last_event = event;
        event_count.fetch_add(1, std::memory_order_relaxed);
    }

    diagnostics::PcmPublishResult PublishSubmittedPcm(
        const diagnostics::SubmittedPcmMetadata&,
        std::span<const std::int16_t>) noexcept override {
        return {};
    }

    diagnostics::AudioDiagnosticEvent last_event{};
    std::atomic_uint32_t event_count{};
};

int TestPcmQueuePreservesSamplesAndSequence() {
    diagnostics::detail::SpscPcmBlockQueue queue;
    constexpr std::array<std::int16_t, 8> first{
        1, -1, 2, -2, 3, -3, 4, -4};
    constexpr std::array<std::int16_t, 8> second{
        5, -5, 6, -6, 7, -7, 8, -8};

    int failures = 0;
    failures += Expect(
        queue.Initialize(2, first.size()),
        "the PCM queue to initialize");

    diagnostics::SubmittedPcmMetadata first_metadata{};
    first_metadata.endpoint_clock_position = 100;
    diagnostics::SubmittedPcmMetadata second_metadata{};
    second_metadata.endpoint_clock_position = 200;

    const auto first_result = queue.TryPush(first_metadata, first);
    const auto second_result = queue.TryPush(second_metadata, second);
    failures += Expect(
        first_result.queued && first_result.sequence == 0 &&
            second_result.queued && second_result.sequence == 1,
        "successful PCM blocks to receive ordered attempt sequences");

    auto view = queue.TryPeek();
    failures += Expect(
        view.has_value() && view->sequence == 0 &&
            view->metadata.endpoint_clock_position == 100 &&
            std::ranges::equal(view->samples, first),
        "the first queued block to preserve metadata and samples");
    queue.Pop();

    view = queue.TryPeek();
    failures += Expect(
        view.has_value() && view->sequence == 1 &&
            view->metadata.endpoint_clock_position == 200 &&
            std::ranges::equal(view->samples, second),
        "the second queued block to preserve metadata and samples");
    queue.Pop();
    failures += Expect(
        !queue.TryPeek().has_value() && queue.completed_sequence() == 2,
        "the drained queue to expose the completed attempt bound");
    return failures;
}

int TestPcmQueueReportsSequenceGapWithoutBlocking() {
    diagnostics::detail::SpscPcmBlockQueue queue;
    constexpr std::array<std::int16_t, 8> first{
        1, -1, 2, -2, 3, -3, 4, -4};
    constexpr std::array<std::int16_t, 8> second{
        5, -5, 6, -6, 7, -7, 8, -8};

    int failures = 0;
    failures += Expect(
        queue.Initialize(1, first.size()),
        "the one-block PCM queue to initialize");
    const auto accepted =
        queue.TryPush(diagnostics::SubmittedPcmMetadata{}, first);
    const auto dropped =
        queue.TryPush(diagnostics::SubmittedPcmMetadata{}, second);
    failures += Expect(
        accepted.queued && accepted.sequence == 0 &&
            !dropped.queued && dropped.sequence == 1 &&
            queue.completed_sequence() == 2,
        "a full PCM queue to reject immediately while reserving sequence");

    queue.Pop();
    const auto resumed =
        queue.TryPush(diagnostics::SubmittedPcmMetadata{}, second);
    const auto view = queue.TryPeek();
    failures += Expect(
        resumed.queued && resumed.sequence == 2 &&
            view.has_value() && view->sequence == 2 &&
            std::ranges::equal(view->samples, second) &&
            queue.completed_sequence() == 3,
        "the next retained block to expose the missing sequence");
    return failures;
}

int TestEventRingPreservesMultipleProducerSequences() {
    diagnostics::detail::MpscAudioEventQueue queue;
    constexpr std::size_t producer_count = 4;
    constexpr std::size_t events_per_producer = 1'024;
    constexpr std::size_t total_events =
        producer_count * events_per_producer;

    int failures = 0;
    failures += Expect(
        queue.Initialize(8'192),
        "the multi-producer event queue to initialize");

    std::array<std::thread, producer_count> producers;
    for (std::size_t producer = 0;
         producer < producer_count;
         ++producer) {
        producers[producer] = std::thread([producer, &queue] {
            for (std::size_t index = 0;
                 index < events_per_producer;
                 ++index) {
                diagnostics::AudioDiagnosticEvent event{};
                event.kind =
                    diagnostics::AudioDiagnosticEventKind::RenderSpan;
                event.value0 = static_cast<std::uint64_t>(producer);
                event.value1 = static_cast<std::uint64_t>(index);
                static_cast<void>(queue.TryPush(event));
            }
        });
    }
    for (auto& producer : producers) {
        producer.join();
    }

    std::vector<bool> sequences(total_events);
    std::size_t retained = 0;
    for (;;) {
        const auto result = queue.TryRead();
        if (result.kind == diagnostics::detail::EventReadKind::Empty) {
            break;
        }
        const auto& event = result.event;
        const auto payload_valid =
            event.value0 < static_cast<std::uint64_t>(producer_count) &&
            event.value1 <
                static_cast<std::uint64_t>(events_per_producer);
        const auto sequence_valid =
            event.sequence < static_cast<std::uint64_t>(total_events);
        failures += Expect(
            payload_valid && sequence_valid,
            "every retained event to have valid producer payload");
        if (sequence_valid) {
            failures += Expect(
                !sequences[static_cast<std::size_t>(event.sequence)],
                "every retained event sequence to be unique");
            sequences[static_cast<std::size_t>(event.sequence)] = true;
        }
        ++retained;
    }

    failures += Expect(
        retained == total_events &&
            queue.lost_events() == 0 &&
            std::ranges::all_of(sequences, [](bool seen) { return seen; }),
        "all multi-producer events to be retained exactly once");
    return failures;
}

int TestEventQueueReportsDroppedRecords() {
    diagnostics::detail::MpscAudioEventQueue queue;
    int failures = 0;
    failures += Expect(
        queue.Initialize(8),
        "the bounded event queue to initialize");

    for (std::uint64_t index = 0; index < 8; ++index) {
        diagnostics::AudioDiagnosticEvent event{};
        event.value0 = index;
        failures += Expect(
            queue.TryPush(event) ==
                diagnostics::detail::EventPublishResult::Queued,
            "an event within capacity to queue");
    }
    failures += Expect(
        queue.TryPush({}) ==
                diagnostics::detail::EventPublishResult::Dropped &&
            queue.lost_events() == 1,
        "an event beyond capacity to be counted and dropped");

    std::uint64_t retained = 0;
    while (queue.TryRead().kind ==
           diagnostics::detail::EventReadKind::Ready) {
        ++retained;
    }
    failures += Expect(
        retained == 8,
        "dropping a new event to preserve all unconsumed payloads");
    failures += Expect(
        queue.TryPush({}) ==
            diagnostics::detail::EventPublishResult::Queued,
        "a drained slot to accept a later event");
    return failures;
}

int TestActiveSinkPublishesAndClears() {
    FakeSink first;
    FakeSink second;
    diagnostics::AudioDiagnosticEvent event{};
    event.kind = diagnostics::AudioDiagnosticEventKind::AudioResync;
    event.value0 = 17;

    diagnostics::ActivateAudioDiagnosticSink(&first);
    diagnostics::PublishActiveAudioDiagnosticEvent(event);
    diagnostics::DeactivateAudioDiagnosticSink(&first);
    diagnostics::ActivateAudioDiagnosticSink(&second);
    diagnostics::DeactivateAudioDiagnosticSink(&first);
    diagnostics::PublishActiveAudioDiagnosticEvent(event);
    diagnostics::DeactivateAudioDiagnosticSink(&second);
    diagnostics::PublishActiveAudioDiagnosticEvent(event);

    int failures = 0;
    failures += Expect(
        first.event_count.load(std::memory_order_relaxed) == 1 &&
            first.last_event.value0 == 17,
        "the first active sink to receive one real event");
    failures += Expect(
        second.event_count.load(std::memory_order_relaxed) == 1 &&
            second.last_event.value0 == 17,
        "a stale owner not to clear the replacement sink");
    return failures;
}

int TestEventRecordStaysFixedAndTriviallyCopyable() {
    int failures = 0;
    failures += Expect(
        std::is_trivially_copyable_v<
            diagnostics::AudioDiagnosticEvent>,
        "the diagnostic event to remain trivially copyable");
    failures += Expect(
        sizeof(diagnostics::AudioDiagnosticEvent) <= 128,
        "the diagnostic event to remain at most 128 bytes");
    return failures;
}

} // namespace

int main() {
    int failures = 0;
    failures += TestPcmQueuePreservesSamplesAndSequence();
    failures += TestPcmQueueReportsSequenceGapWithoutBlocking();
    failures += TestEventRingPreservesMultipleProducerSequences();
    failures += TestEventQueueReportsDroppedRecords();
    failures += TestActiveSinkPublishesAndClears();
    failures += TestEventRecordStaysFixedAndTriviallyCopyable();
    return failures == 0 ? 0 : 1;
}
