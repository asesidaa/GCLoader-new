#include "Audio/Diagnostics/AudioFlightRecorder.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
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

class TemporaryRoot final {
public:
    TemporaryRoot() {
        static std::atomic_uint32_t next_id{};
        path_ =
            std::filesystem::temp_directory_path() /
            ("GCLoader-AudioFlightRecorderTests-" +
             std::to_string(GetCurrentProcessId()) + "-" +
             std::to_string(
                 next_id.fetch_add(1, std::memory_order_relaxed)));
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    ~TemporaryRoot() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_;
};

std::vector<std::uint8_t> ReadBytes(
    const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>(),
    };
}

std::string ReadText(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>(),
    };
}

std::uint16_t ReadLe16(
    const std::vector<std::uint8_t>& bytes,
    std::size_t offset) {
    return static_cast<std::uint16_t>(
        bytes.at(offset) |
        (static_cast<std::uint16_t>(bytes.at(offset + 1)) << 8));
}

std::uint32_t ReadLe32(
    const std::vector<std::uint8_t>& bytes,
    std::size_t offset) {
    return
        static_cast<std::uint32_t>(bytes.at(offset)) |
        (static_cast<std::uint32_t>(bytes.at(offset + 1)) << 8) |
        (static_cast<std::uint32_t>(bytes.at(offset + 2)) << 16) |
        (static_cast<std::uint32_t>(bytes.at(offset + 3)) << 24);
}

constexpr std::uint32_t FourCc(char a, char b, char c, char d) {
    return static_cast<std::uint32_t>(
        static_cast<std::uint8_t>(a)) |
        (static_cast<std::uint32_t>(
             static_cast<std::uint8_t>(b))
         << 8) |
        (static_cast<std::uint32_t>(
             static_cast<std::uint8_t>(c))
         << 16) |
        (static_cast<std::uint32_t>(
             static_cast<std::uint8_t>(d))
         << 24);
}

bool WaitUntil(
    const std::function<bool()>& predicate,
    std::chrono::milliseconds timeout =
        std::chrono::milliseconds{2'000}) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    return predicate();
}

diagnostics::AudioFlightRecorderSession TestSession() {
    return {
        .sample_rate = 48'000,
        .channels = 2,
        .bits_per_sample = 16,
        .frames_per_block = 4,
        .qpc_frequency = 10'000'000,
    };
}

diagnostics::AudioFlightRecorderOptions TestOptions(
    const std::filesystem::path& root) {
    return {
        .root_directory = root,
        .pcm_queue_blocks = 8,
        .event_queue_records = 64,
        .checkpoint_interval = std::chrono::milliseconds{10},
        .maximum_seconds = 1'800,
    };
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

int TestRecorderWritesExactPcm16Wave() {
    TemporaryRoot root;
    auto recorder =
        diagnostics::AudioFlightRecorder::Create(
            TestOptions(root.path()));
    constexpr std::array<std::int16_t, 8> samples{
        1, -1, 2, -2, 3, -3, 4, -4};

    int failures = 0;
    failures += Expect(
        recorder != nullptr && recorder->StartSession(TestSession()),
        "the recorder to start a valid PCM16 stereo session");
    const auto result = recorder->PublishSubmittedPcm({}, samples);
    failures += Expect(
        result.queued && result.sequence == 0,
        "the first submitted block to enter the recorder queue");
    failures += Expect(
        WaitUntil([&] {
            return recorder->status().checkpointed_blocks >= 1;
        }),
        "the writer to checkpoint the submitted block");

    const auto session_directory =
        recorder->status().session_directory;
    recorder->StopAndJoin();
    const auto bytes =
        ReadBytes(session_directory / "submitted.wav");

    failures += Expect(
        bytes.size() == 44 + samples.size() * sizeof(std::int16_t),
        "the WAV to contain one exact interleaved PCM block");
    failures += Expect(
        ReadLe32(bytes, 0) == FourCc('R', 'I', 'F', 'F') &&
            ReadLe32(bytes, 8) == FourCc('W', 'A', 'V', 'E') &&
            ReadLe32(bytes, 12) == FourCc('f', 'm', 't', ' ') &&
            ReadLe16(bytes, 20) == 1 &&
            ReadLe16(bytes, 22) == 2 &&
            ReadLe32(bytes, 24) == 48'000 &&
            ReadLe16(bytes, 34) == 16 &&
            ReadLe32(bytes, 40) ==
                samples.size() * sizeof(std::int16_t),
        "the checkpointed WAV header to describe PCM16 stereo 48 kHz");
    failures += Expect(
        std::equal(
            samples.begin(),
            samples.end(),
            reinterpret_cast<const std::int16_t*>(
                bytes.data() + 44)),
        "the WAV payload to equal the submitted sample bytes");
    return failures;
}

int TestRecorderWritesStableSessionAndTimelineSchema() {
    TemporaryRoot root;
    auto recorder =
        diagnostics::AudioFlightRecorder::Create(
            TestOptions(root.path()));
    constexpr std::array<std::int16_t, 8> samples{
        10, -10, 20, -20, 30, -30, 40, -40};

    int failures = 0;
    failures += Expect(
        recorder != nullptr && recorder->StartSession(TestSession()),
        "the schema test recorder to start");

    diagnostics::AudioDiagnosticEvent event{};
    event.kind = diagnostics::AudioDiagnosticEventKind::SeekApplied;
    event.voice_id = 7;
    event.epoch = 9;
    event.output_frame_begin = 123;
    event.source_frame_begin = 456;
    recorder->PublishEvent(event);

    diagnostics::SubmittedPcmMetadata metadata{};
    metadata.endpoint_clock_position = 1'000;
    metadata.endpoint_qpc_100ns = 2'000;
    metadata.presented_output_frame = 3'000;
    metadata.output_frame_begin = 3'004;
    metadata.submitted_tail = 3'008;
    metadata.discontinuity_frames = 4;
    metadata.mixer_frames_read = 4;
    metadata.mixer_result = 0;
    metadata.pacing_kind = 1;
    static_cast<void>(
        recorder->PublishSubmittedPcm(metadata, samples));
    failures += Expect(
        WaitUntil([&] {
            return recorder->status().checkpointed_blocks >= 1;
        }),
        "the schema test block to become durable");

    const auto session_directory =
        recorder->status().session_directory;
    recorder->StopAndJoin();
    const auto session =
        ReadText(session_directory / "session.json");
    const auto timeline =
        ReadText(session_directory / "timeline.jsonl");
    const std::string expected_session =
        "{\n"
        "  \"schema_version\": 1,\n"
        "  \"sample_rate\": 48000,\n"
        "  \"channels\": 2,\n"
        "  \"bits_per_sample\": 16,\n"
        "  \"frames_per_block\": 4,\n"
        "  \"qpc_frequency\": 10000000,\n"
        "  \"maximum_seconds\": 1800\n"
        "}\n";

    failures += Expect(
        session == expected_session,
        "session.json to use the exact version-1 schema");
    failures += Expect(
        timeline.contains("\"kind\":\"seek_applied\"") &&
            timeline.contains("\"voice_id\":7") &&
            timeline.contains("\"epoch\":9") &&
            timeline.contains("\"kind\":\"endpoint_block\"") &&
            timeline.contains("\"pcm_sequence\":0") &&
            timeline.contains("\"endpoint_clock_position\":1000") &&
            timeline.contains("\"kind\":\"checkpoint\"") &&
            timeline.contains("\"wav_data_bytes\":16"),
        "timeline.jsonl to use stable causal, endpoint, and checkpoint keys");
    return failures;
}

int TestCheckpointHeaderExcludesUncheckpointedTail() {
    TemporaryRoot root;
    auto options = TestOptions(root.path());
    options.checkpoint_interval = std::chrono::seconds{5};
    auto recorder =
        diagnostics::AudioFlightRecorder::Create(std::move(options));
    constexpr std::array<std::int16_t, 8> samples{
        1, 2, 3, 4, 5, 6, 7, 8};

    int failures = 0;
    failures += Expect(
        recorder != nullptr && recorder->StartSession(TestSession()),
        "the delayed-checkpoint recorder to start");
    const auto session_directory =
        recorder->status().session_directory;
    static_cast<void>(
        recorder->PublishSubmittedPcm({}, samples));
    failures += Expect(
        WaitUntil([&] {
            std::error_code error;
            return std::filesystem::file_size(
                       session_directory / "submitted.wav",
                       error) >= 44 + samples.size() * sizeof(std::int16_t);
        }),
        "the writer to append PCM before its delayed checkpoint");

    const auto before =
        ReadBytes(session_directory / "submitted.wav");
    failures += Expect(
        ReadLe32(before, 40) == 0,
        "an uncheckpointed PCM tail to remain outside the WAV header");

    recorder->StopAndJoin();
    const auto after =
        ReadBytes(session_directory / "submitted.wav");
    failures += Expect(
        ReadLe32(after, 40) ==
            samples.size() * sizeof(std::int16_t),
        "orderly stop to include the drained tail in the final header");
    return failures;
}

int TestPcmGapInsertsMarkedSilenceAndInvalidatesRange() {
    TemporaryRoot root;
    auto options = TestOptions(root.path());
    options.pcm_queue_blocks = 1;
    options.checkpoint_interval = std::chrono::seconds{5};
    auto recorder =
        diagnostics::AudioFlightRecorder::Create(std::move(options));
    constexpr std::array<std::int16_t, 8> samples{
        1, -1, 2, -2, 3, -3, 4, -4};

    int failures = 0;
    failures += Expect(
        recorder != nullptr && recorder->StartSession(TestSession()),
        "the one-block recorder to start");

    bool observed_drop = false;
    bool retained_after_drop = false;
    for (std::size_t index = 0;
         index < 20'000 && !retained_after_drop;
         ++index) {
        const auto result =
            recorder->PublishSubmittedPcm({}, samples);
        observed_drop = observed_drop || !result.queued;
        retained_after_drop =
            observed_drop && result.queued;
    }
    failures += Expect(
        observed_drop && retained_after_drop,
        "a saturated queue to expose a dropped middle sequence");

    const auto status_before_stop = recorder->status();
    const auto session_directory =
        status_before_stop.session_directory;
    recorder->StopAndJoin();
    const auto timeline =
        ReadText(session_directory / "timeline.jsonl");
    const auto bytes =
        ReadBytes(session_directory / "submitted.wav");
    const std::array<std::uint8_t, 16> silence{};

    failures += Expect(
        status_before_stop.dropped_pcm_blocks > 0 &&
            timeline.contains("\"kind\":\"pcm_gap\"") &&
            timeline.contains("\"conclusive\":false"),
        "a dropped PCM range to be explicit and inconclusive");
    failures += Expect(
        std::search(
            bytes.begin() + 44,
            bytes.end(),
            silence.begin(),
            silence.end()) != bytes.end(),
        "a dropped block to receive an equal-duration silent placeholder");
    return failures;
}

int TestCaptureLimitStopsRecorderWithoutBlockingPublisher() {
    TemporaryRoot root;
    auto options = TestOptions(root.path());
    options.maximum_seconds = 1;
    auto recorder =
        diagnostics::AudioFlightRecorder::Create(std::move(options));
    auto session = TestSession();
    session.sample_rate = 8;
    constexpr std::array<std::int16_t, 8> samples{
        1, -1, 2, -2, 3, -3, 4, -4};

    int failures = 0;
    failures += Expect(
        recorder != nullptr && recorder->StartSession(session),
        "the short-limit recorder to start");
    const auto first =
        recorder->PublishSubmittedPcm({}, samples);
    const auto second =
        recorder->PublishSubmittedPcm({}, samples);
    const auto limited =
        recorder->PublishSubmittedPcm({}, samples);
    const auto status = recorder->status();
    const auto session_directory = status.session_directory;

    failures += Expect(
        first.queued && second.queued && !limited.queued &&
            status.state ==
                diagnostics::AudioFlightRecorderState::LimitReached &&
            status.submitted_blocks == 2,
        "the safety limit to stop diagnostics after exactly two blocks");
    recorder->StopAndJoin();
    failures += Expect(
        ReadText(session_directory / "timeline.jsonl")
            .contains("\"kind\":\"capture_limit\""),
        "the writer to record one capture-limit marker");
    return failures;
}

int TestWriterFailureLeavesPublishersNonBlocking() {
    TemporaryRoot root;
    {
        std::ofstream file(root.path(), std::ios::binary);
        file << "not a directory";
    }
    auto recorder =
        diagnostics::AudioFlightRecorder::Create(
            TestOptions(root.path()));

    int failures = 0;
    failures += Expect(
        recorder != nullptr && !recorder->StartSession(TestSession()),
        "a regular-file capture root to fail session initialization");
    constexpr std::array<std::int16_t, 8> samples{
        1, -1, 2, -2, 3, -3, 4, -4};
    for (std::size_t index = 0; index < 10'000; ++index) {
        failures += Expect(
            !recorder->PublishSubmittedPcm({}, samples).queued,
            "a failed recorder publisher to return immediately");
        if (failures != 0) {
            break;
        }
    }
    const auto status = recorder->status();
    failures += Expect(
        status.state ==
                diagnostics::AudioFlightRecorderState::Failed &&
            !status.error.empty(),
        "the recorder to retain its first initialization failure");
    return failures;
}

int TestStopDrainsAndFinalizesFiniteTestOwner() {
    TemporaryRoot root;
    auto options = TestOptions(root.path());
    options.checkpoint_interval = std::chrono::seconds{5};
    auto recorder =
        diagnostics::AudioFlightRecorder::Create(std::move(options));
    constexpr std::array<std::int16_t, 8> samples{
        1, -1, 2, -2, 3, -3, 4, -4};

    int failures = 0;
    failures += Expect(
        recorder != nullptr && recorder->StartSession(TestSession()),
        "the finite-owner recorder to start");
    for (std::size_t index = 0; index < 3; ++index) {
        failures += Expect(
            recorder->PublishSubmittedPcm({}, samples).queued,
            "each finite-owner block to queue");
    }
    const auto session_directory =
        recorder->status().session_directory;
    recorder->StopAndJoin();
    recorder->StopAndJoin();

    const auto bytes =
        ReadBytes(session_directory / "submitted.wav");
    const auto timeline =
        ReadText(session_directory / "timeline.jsonl");
    failures += Expect(
        ReadLe32(bytes, 40) ==
            3 * samples.size() * sizeof(std::int16_t),
        "an immediate stop to drain and finalize all published PCM");
    failures += Expect(
        timeline.contains("\"kind\":\"checkpoint\"") &&
            timeline.contains("\"pcm_sequence\":2") &&
            recorder->status().state ==
                diagnostics::AudioFlightRecorderState::Stopped,
        "an immediate stop to persist a final checkpoint exactly once");
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
    failures += TestRecorderWritesExactPcm16Wave();
    failures += TestRecorderWritesStableSessionAndTimelineSchema();
    failures += TestCheckpointHeaderExcludesUncheckpointedTail();
    failures += TestPcmGapInsertsMarkedSilenceAndInvalidatesRange();
    failures += TestCaptureLimitStopsRecorderWithoutBlockingPublisher();
    failures += TestWriterFailureLeavesPublishersNonBlocking();
    failures += TestStopDrainsAndFinalizesFiniteTestOwner();
    return failures == 0 ? 0 : 1;
}
