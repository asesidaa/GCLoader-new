#include "Audio/Diagnostics/AudioFlightRecorder.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <mutex>
#include <new>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace gc::audio::diagnostics {

namespace {

static_assert(std::atomic<IAudioDiagnosticSink*>::is_always_lock_free);
static_assert(std::atomic_uint64_t::is_always_lock_free);
static_assert(
    std::atomic<AudioFlightRecorderState>::is_always_lock_free);

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

namespace {

bool WriteAll(
    HANDLE file,
    const void* data,
    std::size_t byte_count) noexcept {
    auto* next = static_cast<const std::byte*>(data);
    auto remaining = byte_count;
    while (remaining != 0) {
        const auto chunk = static_cast<DWORD>(
            std::min<std::size_t>(
                remaining,
                std::numeric_limits<DWORD>::max()));
        DWORD written = 0;
        if (!WriteFile(file, next, chunk, &written, nullptr) ||
            written != chunk) {
            return false;
        }
        next += written;
        remaining -= written;
    }
    return true;
}

bool WriteAll(
    HANDLE file,
    std::string_view text) noexcept {
    return WriteAll(file, text.data(), text.size());
}

bool SeekFile(HANDLE file, std::uint64_t offset) noexcept {
    if (offset >
        static_cast<std::uint64_t>(
            std::numeric_limits<LONGLONG>::max())) {
        return false;
    }
    LARGE_INTEGER distance{};
    distance.QuadPart = static_cast<LONGLONG>(offset);
    return SetFilePointerEx(
        file,
        distance,
        nullptr,
        FILE_BEGIN) != FALSE;
}

void PutLe16(
    std::array<std::uint8_t, 44>& bytes,
    std::size_t offset,
    std::uint16_t value) noexcept {
    bytes[offset] = static_cast<std::uint8_t>(value);
    bytes[offset + 1] =
        static_cast<std::uint8_t>(value >> 8);
}

void PutLe32(
    std::array<std::uint8_t, 44>& bytes,
    std::size_t offset,
    std::uint32_t value) noexcept {
    bytes[offset] = static_cast<std::uint8_t>(value);
    bytes[offset + 1] =
        static_cast<std::uint8_t>(value >> 8);
    bytes[offset + 2] =
        static_cast<std::uint8_t>(value >> 16);
    bytes[offset + 3] =
        static_cast<std::uint8_t>(value >> 24);
}

void PutFourCc(
    std::array<std::uint8_t, 44>& bytes,
    std::size_t offset,
    char a,
    char b,
    char c,
    char d) noexcept {
    bytes[offset] = static_cast<std::uint8_t>(a);
    bytes[offset + 1] = static_cast<std::uint8_t>(b);
    bytes[offset + 2] = static_cast<std::uint8_t>(c);
    bytes[offset + 3] = static_cast<std::uint8_t>(d);
}

std::array<std::uint8_t, 44> MakeWavHeader(
    const AudioFlightRecorderSession& session,
    std::uint32_t data_bytes) noexcept {
    std::array<std::uint8_t, 44> bytes{};
    const auto block_align = static_cast<std::uint16_t>(
        session.channels * session.bits_per_sample / 8);
    const auto byte_rate = static_cast<std::uint32_t>(
        static_cast<std::uint64_t>(session.sample_rate) *
        block_align);

    PutFourCc(bytes, 0, 'R', 'I', 'F', 'F');
    PutLe32(bytes, 4, 36 + data_bytes);
    PutFourCc(bytes, 8, 'W', 'A', 'V', 'E');
    PutFourCc(bytes, 12, 'f', 'm', 't', ' ');
    PutLe32(bytes, 16, 16);
    PutLe16(bytes, 20, 1);
    PutLe16(bytes, 22, session.channels);
    PutLe32(bytes, 24, session.sample_rate);
    PutLe32(bytes, 28, byte_rate);
    PutLe16(bytes, 32, block_align);
    PutLe16(bytes, 34, session.bits_per_sample);
    PutFourCc(bytes, 36, 'd', 'a', 't', 'a');
    PutLe32(bytes, 40, data_bytes);
    return bytes;
}

std::string FailureText(
    std::string_view operation,
    DWORD error = GetLastError()) noexcept {
    try {
        std::string text(operation);
        text += " failed (win32=";
        text += std::to_string(error);
        text += ')';
        return text;
    } catch (...) {
        return {};
    }
}

const char* EventKindName(
    AudioDiagnosticEventKind kind) noexcept {
    switch (kind) {
    case AudioDiagnosticEventKind::VoiceCreated:
        return "voice_created";
    case AudioDiagnosticEventKind::VoicePlay:
        return "voice_play";
    case AudioDiagnosticEventKind::VoiceStop:
        return "voice_stop";
    case AudioDiagnosticEventKind::SeekRequested:
        return "seek_requested";
    case AudioDiagnosticEventKind::SeekApplied:
        return "seek_applied";
    case AudioDiagnosticEventKind::ConverterReset:
        return "converter_reset";
    case AudioDiagnosticEventKind::RenderSpan:
        return "render_span";
    case AudioDiagnosticEventKind::AudioResync:
        return "audio_resync";
    }
    return "unknown";
}

const char* ResetReasonName(std::uint8_t reason) noexcept {
    switch (static_cast<ConverterResetReason>(reason)) {
    case ConverterResetReason::Seek:
        return "seek";
    case ConverterResetReason::OutputDiscontinuity:
        return "output_discontinuity";
    }
    return "unknown";
}

const char* ResyncDecisionName(std::uint8_t decision) noexcept {
    switch (static_cast<AudioResyncDecision>(decision)) {
    case AudioResyncDecision::Unreadable:
        return "unreadable";
    case AudioResyncDecision::SuppressedInMargin:
        return "suppressed_in_margin";
    case AudioResyncDecision::AllowedOutOfMargin:
        return "allowed_out_of_margin";
    }
    return "unknown";
}

} // namespace

struct AudioFlightRecorder::Impl {
    explicit Impl(AudioFlightRecorderOptions value)
        : options(std::move(value)) {}

    ~Impl() {
        CloseHandles();
    }

    AudioFlightRecorderOptions options;
    AudioFlightRecorderSession session{};
    detail::SpscPcmBlockQueue pcm_queue;
    detail::MpscAudioEventQueue event_queue;
    std::atomic<AudioFlightRecorderState> state{
        AudioFlightRecorderState::Idle};
    std::atomic_uint64_t submitted_blocks{};
    std::atomic_uint64_t dropped_pcm_blocks{};
    std::atomic_uint64_t checkpointed_blocks{};
    std::atomic_bool stop_requested{};
    std::atomic_bool capture_limit_pending{};
    std::filesystem::path session_directory;
    std::uint64_t maximum_blocks{};
    std::size_t samples_per_block{};
    HANDLE wake_event{};
    HANDLE wav_file{INVALID_HANDLE_VALUE};
    HANDLE timeline_file{INVALID_HANDLE_VALUE};
    std::thread writer;
    IAudioDiagnosticSink* owner{};

    mutable std::mutex error_mutex;
    std::string first_error;

    std::uint64_t next_pcm_sequence{};
    std::uint64_t wav_data_bytes{};
    std::uint64_t last_event_sequence{};
    std::uint64_t last_lost_events{};
    std::uint64_t last_output_tail{};
    std::uint64_t last_checkpoint_output_tail{};
    bool has_output_tail{};
    bool has_event_sequence{};
    std::vector<std::int16_t> silent_block;

    void CloseHandles() noexcept {
        if (wav_file != INVALID_HANDLE_VALUE) {
            CloseHandle(wav_file);
            wav_file = INVALID_HANDLE_VALUE;
        }
        if (timeline_file != INVALID_HANDLE_VALUE) {
            CloseHandle(timeline_file);
            timeline_file = INVALID_HANDLE_VALUE;
        }
        if (wake_event != nullptr) {
            CloseHandle(wake_event);
            wake_event = nullptr;
        }
    }

    void SetFailure(std::string error) noexcept {
        {
            const std::scoped_lock lock(error_mutex);
            if (first_error.empty()) {
                try {
                    first_error = std::move(error);
                } catch (...) {
                }
            }
        }
        state.store(
            AudioFlightRecorderState::Failed,
            std::memory_order_release);
        DeactivateAudioDiagnosticSink(owner);
    }

    bool CreateSessionDirectory() {
        std::error_code error;
        std::filesystem::create_directories(
            options.root_directory,
            error);
        if (error ||
            !std::filesystem::is_directory(
                options.root_directory,
                error) ||
            error) {
            SetFailure(
                "audio diagnostic root is not a directory");
            return false;
        }

        SYSTEMTIME local{};
        GetLocalTime(&local);
        for (unsigned suffix = 0; suffix < 10'000; ++suffix) {
            wchar_t name[64]{};
            if (suffix == 0) {
                swprintf_s(
                    name,
                    L"%04u%02u%02u-%02u%02u%02u",
                    local.wYear,
                    local.wMonth,
                    local.wDay,
                    local.wHour,
                    local.wMinute,
                    local.wSecond);
            } else {
                swprintf_s(
                    name,
                    L"%04u%02u%02u-%02u%02u%02u-%02u",
                    local.wYear,
                    local.wMonth,
                    local.wDay,
                    local.wHour,
                    local.wMinute,
                    local.wSecond,
                    suffix);
            }
            auto candidate = options.root_directory / name;
            error.clear();
            if (std::filesystem::create_directory(
                    candidate,
                    error)) {
                session_directory = std::move(candidate);
                return true;
            }
            if (error) {
                SetFailure(
                    "failed to create audio diagnostic session directory");
                return false;
            }
        }
        SetFailure(
            "audio diagnostic session suffix space exhausted");
        return false;
    }

    bool WriteSessionJson() {
        std::ostringstream json;
        json
            << "{\n"
            << "  \"schema_version\": 1,\n"
            << "  \"sample_rate\": " << session.sample_rate << ",\n"
            << "  \"channels\": " << session.channels << ",\n"
            << "  \"bits_per_sample\": "
            << session.bits_per_sample << ",\n"
            << "  \"frames_per_block\": "
            << session.frames_per_block << ",\n"
            << "  \"qpc_frequency\": "
            << session.qpc_frequency << ",\n"
            << "  \"maximum_seconds\": "
            << options.maximum_seconds << "\n"
            << "}\n";
        const auto text = json.str();

        const auto path = session_directory / L"session.json";
        const auto file = CreateFileW(
            path.c_str(),
            GENERIC_WRITE,
            FILE_SHARE_READ,
            nullptr,
            CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            SetFailure(FailureText("CreateFileW(session.json)"));
            return false;
        }
        const auto wrote = WriteAll(file, text);
        const auto flushed =
            wrote && FlushFileBuffers(file) != FALSE;
        const auto close_result = CloseHandle(file);
        if (!wrote || !flushed || !close_result) {
            SetFailure(
                FailureText("write/flush session.json"));
            return false;
        }
        return true;
    }

    bool CreateCaptureFiles() {
        const auto share =
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
        const auto wav_path = session_directory / L"submitted.wav";
        wav_file = CreateFileW(
            wav_path.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            share,
            nullptr,
            CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (wav_file == INVALID_HANDLE_VALUE) {
            SetFailure(FailureText("CreateFileW(submitted.wav)"));
            return false;
        }

        const auto timeline_path =
            session_directory / L"timeline.jsonl";
        timeline_file = CreateFileW(
            timeline_path.c_str(),
            GENERIC_WRITE,
            share,
            nullptr,
            CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (timeline_file == INVALID_HANDLE_VALUE) {
            SetFailure(FailureText("CreateFileW(timeline.jsonl)"));
            return false;
        }

        const auto header = MakeWavHeader(session, 0);
        if (!WriteAll(wav_file, header.data(), header.size())) {
            SetFailure(FailureText("write initial WAV header"));
            return false;
        }
        return true;
    }

    bool Start(
        IAudioDiagnosticSink* recorder,
        const AudioFlightRecorderSession& value) noexcept {
        owner = recorder;
        if (state.load(std::memory_order_acquire) !=
                AudioFlightRecorderState::Idle ||
            value.sample_rate == 0 ||
            value.channels != 2 ||
            value.bits_per_sample != 16 ||
            value.frames_per_block == 0 ||
            value.qpc_frequency == 0 ||
            options.pcm_queue_blocks == 0 ||
            options.event_queue_records == 0 ||
            options.checkpoint_interval.count() <= 0 ||
            options.maximum_seconds >
                std::numeric_limits<std::uint64_t>::max() /
                    value.sample_rate) {
            SetFailure("invalid audio diagnostic session");
            return false;
        }

        const auto sample_count =
            static_cast<std::uint64_t>(value.frames_per_block) *
            value.channels;
        const auto block_bytes =
            sample_count * sizeof(std::int16_t);
        maximum_blocks =
            options.maximum_seconds * value.sample_rate /
            value.frames_per_block;
        if (sample_count >
                std::numeric_limits<std::size_t>::max() ||
            maximum_blocks != 0 &&
                block_bytes >
                    std::numeric_limits<std::uint32_t>::max() /
                        maximum_blocks) {
            SetFailure("audio diagnostic session exceeds RIFF limits");
            return false;
        }

        try {
            session = value;
            samples_per_block =
                static_cast<std::size_t>(sample_count);
            silent_block.assign(samples_per_block, 0);
            if (!pcm_queue.Initialize(
                    options.pcm_queue_blocks,
                    samples_per_block) ||
                !event_queue.Initialize(
                    options.event_queue_records)) {
                SetFailure(
                    "failed to allocate audio diagnostic queues");
                return false;
            }
            if (!CreateSessionDirectory() ||
                !WriteSessionJson() ||
                !CreateCaptureFiles()) {
                return false;
            }

            wake_event = CreateEventW(
                nullptr,
                FALSE,
                FALSE,
                nullptr);
            if (wake_event == nullptr) {
                SetFailure(
                    FailureText("CreateEventW(audio diagnostic)"));
                return false;
            }

            state.store(
                AudioFlightRecorderState::Active,
                std::memory_order_release);
            ActivateAudioDiagnosticSink(owner);
            writer = std::thread([this] { WriterMain(); });
            return true;
        } catch (const std::bad_alloc&) {
            SetFailure(
                "out of memory starting audio diagnostic recorder");
        } catch (const std::system_error&) {
            SetFailure(
                "failed to start audio diagnostic writer thread");
        } catch (...) {
            SetFailure(
                "unexpected audio diagnostic startup failure");
        }
        return false;
    }

    bool WriteTimelineLine(const std::string& line) noexcept {
        return WriteAll(timeline_file, line) &&
            WriteAll(timeline_file, "\n");
    }

    bool WriteEvent(
        const AudioDiagnosticEvent& event) noexcept {
        try {
            std::ostringstream json;
            json
                << "{\"kind\":\""
                << EventKindName(event.kind)
                << "\",\"event_sequence\":" << event.sequence
                << ",\"qpc_ticks\":" << event.qpc_ticks
                << ",\"voice_id\":" << event.voice_id
                << ",\"epoch\":" << event.epoch
                << ",\"generation\":" << event.generation
                << ",\"output_frame_begin\":"
                << event.output_frame_begin
                << ",\"output_frame_end\":"
                << event.output_frame_end
                << ",\"source_frame_begin\":"
                << event.source_frame_begin
                << ",\"source_frame_end\":"
                << event.source_frame_end
                << ",\"flags\":" << event.flags
                << ",\"decision\":" << +event.decision
                << ",\"signed_value0\":"
                << std::bit_cast<std::int32_t>(
                       event.signed_value0)
                << ",\"signed_value1\":"
                << std::bit_cast<std::int32_t>(
                       event.signed_value1)
                << ",\"value0\":" << event.value0
                << ",\"value1\":" << event.value1
                << ",\"value2\":" << event.value2
                << ",\"value3\":" << event.value3;
            if (event.kind ==
                AudioDiagnosticEventKind::ConverterReset) {
                json
                    << ",\"reset_reason\":\""
                    << ResetReasonName(event.decision)
                    << '"';
            } else if (event.kind ==
                       AudioDiagnosticEventKind::AudioResync) {
                json
                    << ",\"drift_ms\":"
                    << std::bit_cast<std::int32_t>(
                           event.signed_value0)
                    << ",\"margin_ms\":"
                    << std::bit_cast<std::int32_t>(
                           event.signed_value1)
                    << ",\"resync_decision\":\""
                    << ResyncDecisionName(event.decision)
                    << '"';
            }
            json << '}';
            return WriteTimelineLine(json.str());
        } catch (...) {
            return false;
        }
    }

    bool WriteEndpointBlock(
        const detail::PcmBlockView& block) noexcept {
        try {
            const auto& value = block.metadata;
            std::ostringstream json;
            json
                << "{\"kind\":\"endpoint_block\""
                << ",\"pcm_sequence\":" << block.sequence
                << ",\"endpoint_clock_position\":"
                << value.endpoint_clock_position
                << ",\"endpoint_qpc_100ns\":"
                << value.endpoint_qpc_100ns
                << ",\"presented_output_frame\":"
                << value.presented_output_frame
                << ",\"output_frame_begin\":"
                << value.output_frame_begin
                << ",\"submitted_tail\":"
                << value.submitted_tail
                << ",\"discontinuity_frames\":"
                << value.discontinuity_frames
                << ",\"mixer_frames_read\":"
                << value.mixer_frames_read
                << ",\"mixer_result\":"
                << value.mixer_result
                << ",\"pacing_kind\":"
                << +value.pacing_kind
                << ",\"pcm_queue_queued\":true"
                << '}';
            return WriteTimelineLine(json.str());
        } catch (...) {
            return false;
        }
    }

    bool WritePcmGap(
        std::uint64_t first_sequence,
        std::uint64_t end_sequence,
        std::uint64_t output_frame_begin) noexcept {
        if (end_sequence <= first_sequence) {
            return true;
        }
        try {
            const auto count = end_sequence - first_sequence;
            std::ostringstream json;
            json
                << "{\"kind\":\"pcm_gap\""
                << ",\"first_sequence\":" << first_sequence
                << ",\"end_sequence\":" << end_sequence
                << ",\"output_frame_begin\":"
                << output_frame_begin
                << ",\"output_frame_end\":"
                << output_frame_begin +
                    count * session.frames_per_block
                << ",\"conclusive\":false}";
            return WriteTimelineLine(json.str());
        } catch (...) {
            return false;
        }
    }

    bool AppendPcm(
        std::span<const std::int16_t> samples) noexcept {
        const auto bytes = samples.size_bytes();
        if (wav_data_bytes >
            std::numeric_limits<std::uint32_t>::max() - bytes) {
            return false;
        }
        if (!WriteAll(wav_file, samples.data(), bytes)) {
            return false;
        }
        wav_data_bytes += bytes;
        return true;
    }

    bool AppendMissingPcm(
        std::uint64_t first_sequence,
        std::uint64_t end_sequence,
        std::uint64_t inferred_output_begin) noexcept {
        if (!WritePcmGap(
                first_sequence,
                end_sequence,
                inferred_output_begin)) {
            return false;
        }
        for (auto sequence = first_sequence;
             sequence < end_sequence;
             ++sequence) {
            if (!AppendPcm(silent_block)) {
                return false;
            }
        }
        return true;
    }

    bool DrainPcm() noexcept {
        for (;;) {
            while (auto block = pcm_queue.TryPeek()) {
                if (block->sequence < next_pcm_sequence) {
                    pcm_queue.Pop();
                    continue;
                }
                if (block->sequence > next_pcm_sequence) {
                    const auto missing =
                        block->sequence - next_pcm_sequence;
                    const auto inferred_begin = has_output_tail
                        ? last_output_tail
                        : block->metadata.output_frame_begin >=
                                missing * session.frames_per_block
                            ? block->metadata.output_frame_begin -
                                missing * session.frames_per_block
                            : 0;
                    if (!AppendMissingPcm(
                            next_pcm_sequence,
                            block->sequence,
                            inferred_begin)) {
                        return false;
                    }
                    next_pcm_sequence = block->sequence;
                }

                if (!AppendPcm(block->samples) ||
                    !WriteEndpointBlock(*block)) {
                    return false;
                }
                has_output_tail = true;
                last_output_tail = block->metadata.submitted_tail;
                next_pcm_sequence = block->sequence + 1;
                pcm_queue.Pop();
            }

            const auto completed =
                pcm_queue.completed_sequence();
            if (pcm_queue.TryPeek().has_value()) {
                continue;
            }
            if (next_pcm_sequence < completed) {
                const auto inferred_begin = has_output_tail
                    ? last_output_tail
                    : next_pcm_sequence *
                        session.frames_per_block;
                if (!AppendMissingPcm(
                        next_pcm_sequence,
                        completed,
                        inferred_begin)) {
                    return false;
                }
                next_pcm_sequence = completed;
                continue;
            }
            return true;
        }
    }

    bool DrainEvents() noexcept {
        for (;;) {
            const auto result = event_queue.TryRead();
            if (result.kind ==
                detail::EventReadKind::Empty) {
                return true;
            }
            if (!WriteEvent(result.event)) {
                return false;
            }
            has_event_sequence = true;
            last_event_sequence = result.event.sequence;
        }
    }

    bool WriteEventGapIfNeeded() noexcept {
        const auto lost = event_queue.lost_events();
        if (lost == last_lost_events) {
            return true;
        }
        try {
            std::ostringstream json;
            json
                << "{\"kind\":\"event_gap\""
                << ",\"lost_events\":"
                << lost - last_lost_events
                << ",\"total_lost_events\":" << lost
                << ",\"output_frame_begin\":"
                << last_checkpoint_output_tail
                << ",\"output_frame_end\":"
                << (has_output_tail
                        ? last_output_tail
                        : last_checkpoint_output_tail)
                << ",\"qpc_ticks\":"
                << CaptureAudioDiagnosticQpcTicks()
                << ",\"conclusive\":false}";
            if (!WriteTimelineLine(json.str())) {
                return false;
            }
            last_lost_events = lost;
            return true;
        } catch (...) {
            return false;
        }
    }

    bool WriteCaptureLimitIfNeeded() noexcept {
        if (!capture_limit_pending.exchange(
                false,
                std::memory_order_acq_rel)) {
            return true;
        }
        try {
            std::ostringstream json;
            json
                << "{\"kind\":\"capture_limit\""
                << ",\"maximum_seconds\":"
                << options.maximum_seconds
                << ",\"submitted_blocks\":"
                << submitted_blocks.load(
                       std::memory_order_relaxed)
                << '}';
            return WriteTimelineLine(json.str());
        } catch (...) {
            return false;
        }
    }

    bool UpdateWavHeader() noexcept {
        if (wav_data_bytes >
            std::numeric_limits<std::uint32_t>::max()) {
            return false;
        }
        const auto header = MakeWavHeader(
            session,
            static_cast<std::uint32_t>(wav_data_bytes));
        return
            SeekFile(wav_file, 0) &&
            WriteAll(wav_file, header.data(), header.size()) &&
            SeekFile(wav_file, header.size() + wav_data_bytes);
    }

    bool Checkpoint() noexcept {
        if (!WriteEventGapIfNeeded() ||
            !UpdateWavHeader() ||
            FlushFileBuffers(wav_file) == FALSE) {
            return false;
        }
        try {
            const auto dropped =
                dropped_pcm_blocks.load(
                    std::memory_order_relaxed);
            const auto lost = event_queue.lost_events();
            std::ostringstream json;
            json
                << "{\"kind\":\"checkpoint\""
                << ",\"pcm_sequence\":"
                << (next_pcm_sequence == 0
                        ? 0
                        : next_pcm_sequence - 1)
                << ",\"event_sequence\":"
                << (has_event_sequence
                        ? last_event_sequence
                        : 0)
                << ",\"wav_data_bytes\":"
                << wav_data_bytes
                << ",\"dropped_pcm_blocks\":"
                << dropped
                << ",\"lost_events\":" << lost
                << ",\"conclusive\":"
                << (dropped == 0 && lost == 0
                        ? "true"
                        : "false")
                << '}';
            if (!WriteTimelineLine(json.str()) ||
                FlushFileBuffers(timeline_file) == FALSE) {
                return false;
            }
            checkpointed_blocks.store(
                next_pcm_sequence,
                std::memory_order_release);
            last_checkpoint_output_tail =
                has_output_tail
                    ? last_output_tail
                    : last_checkpoint_output_tail;
            return true;
        } catch (...) {
            return false;
        }
    }

    void WriterMain() noexcept {
        auto next_checkpoint =
            std::chrono::steady_clock::now() +
            options.checkpoint_interval;
        for (;;) {
            const auto now = std::chrono::steady_clock::now();
            const auto remaining = now >= next_checkpoint
                ? std::chrono::milliseconds{0}
                : std::chrono::duration_cast<
                      std::chrono::milliseconds>(
                      next_checkpoint - now);
            const auto wait_ms = static_cast<DWORD>(
                std::min<std::int64_t>(
                    std::max<std::int64_t>(
                        remaining.count(),
                        0),
                    std::numeric_limits<DWORD>::max() - 1));
            const auto wait =
                WaitForSingleObject(wake_event, wait_ms);
            if (wait == WAIT_FAILED) {
                SetFailure(
                    FailureText(
                        "wait for audio diagnostic writer event"));
                return;
            }

            if (!DrainPcm() ||
                !DrainEvents() ||
                !WriteCaptureLimitIfNeeded()) {
                SetFailure(
                    FailureText(
                        "write audio diagnostic capture"));
                return;
            }

            const auto stopping =
                stop_requested.load(std::memory_order_acquire);
            const auto checkpoint_due =
                std::chrono::steady_clock::now() >=
                    next_checkpoint;
            if (stopping || checkpoint_due) {
                if (!Checkpoint()) {
                    SetFailure(
                        FailureText(
                            "checkpoint audio diagnostic capture"));
                    return;
                }
                next_checkpoint =
                    std::chrono::steady_clock::now() +
                    options.checkpoint_interval;
            }
            if (stopping) {
                state.store(
                    AudioFlightRecorderState::Stopped,
                    std::memory_order_release);
                return;
            }
        }
    }

    void PublishEvent(AudioDiagnosticEvent event) noexcept {
        const auto current =
            state.load(std::memory_order_acquire);
        if (current != AudioFlightRecorderState::Active &&
            current != AudioFlightRecorderState::LimitReached) {
            return;
        }
        static_cast<void>(event_queue.TryPush(event));
        if (wake_event != nullptr) {
            SetEvent(wake_event);
        }
    }

    PcmPublishResult PublishSubmittedPcm(
        const SubmittedPcmMetadata& metadata,
        std::span<const std::int16_t> samples) noexcept {
        if (state.load(std::memory_order_acquire) !=
            AudioFlightRecorderState::Active) {
            return {
                pcm_queue.completed_sequence(),
                false,
            };
        }

        const auto submitted =
            submitted_blocks.load(std::memory_order_relaxed);
        if (submitted >= maximum_blocks) {
            auto expected =
                AudioFlightRecorderState::Active;
            if (state.compare_exchange_strong(
                    expected,
                    AudioFlightRecorderState::LimitReached,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire)) {
                capture_limit_pending.store(
                    true,
                    std::memory_order_release);
                SetEvent(wake_event);
            }
            return {
                pcm_queue.completed_sequence(),
                false,
            };
        }

        const auto result =
            pcm_queue.TryPush(metadata, samples);
        submitted_blocks.store(
            submitted + 1,
            std::memory_order_relaxed);
        if (!result.queued) {
            dropped_pcm_blocks.fetch_add(
                1,
                std::memory_order_relaxed);
        }
        SetEvent(wake_event);
        return result;
    }

    AudioFlightRecorderStatus Status() const {
        AudioFlightRecorderStatus result{
            .state = state.load(std::memory_order_acquire),
            .session_directory = session_directory,
            .submitted_blocks =
                submitted_blocks.load(
                    std::memory_order_relaxed),
            .dropped_pcm_blocks =
                dropped_pcm_blocks.load(
                    std::memory_order_relaxed),
            .lost_events = event_queue.lost_events(),
            .checkpointed_blocks =
                checkpointed_blocks.load(
                    std::memory_order_acquire),
        };
        const std::scoped_lock lock(error_mutex);
        result.error = first_error;
        return result;
    }

    void StopAndJoin() noexcept {
        DeactivateAudioDiagnosticSink(owner);
        stop_requested.store(true, std::memory_order_release);
        if (wake_event != nullptr) {
            SetEvent(wake_event);
        }
        if (writer.joinable()) {
            writer.join();
        }
        CloseHandles();
    }
};

AudioFlightRecorder::AudioFlightRecorder(
    AudioFlightRecorderOptions options)
    : impl_(std::make_unique<Impl>(std::move(options))) {}

std::unique_ptr<AudioFlightRecorder>
AudioFlightRecorder::Create(
    AudioFlightRecorderOptions options) noexcept {
    try {
        return std::unique_ptr<AudioFlightRecorder>(
            new AudioFlightRecorder(std::move(options)));
    } catch (...) {
        return nullptr;
    }
}

AudioFlightRecorder::~AudioFlightRecorder() {
    StopAndJoin();
}

bool AudioFlightRecorder::StartSession(
    const AudioFlightRecorderSession& session) noexcept {
    return impl_ != nullptr &&
        impl_->Start(this, session);
}

void AudioFlightRecorder::PublishEvent(
    AudioDiagnosticEvent event) noexcept {
    if (impl_ != nullptr) {
        impl_->PublishEvent(event);
    }
}

PcmPublishResult AudioFlightRecorder::PublishSubmittedPcm(
    const SubmittedPcmMetadata& metadata,
    std::span<const std::int16_t> samples) noexcept {
    return impl_ == nullptr
        ? PcmPublishResult{}
        : impl_->PublishSubmittedPcm(metadata, samples);
}

AudioFlightRecorderStatus AudioFlightRecorder::status() const {
    return impl_ == nullptr
        ? AudioFlightRecorderStatus{}
        : impl_->Status();
}

void AudioFlightRecorder::StopAndJoin() noexcept {
    if (impl_ != nullptr) {
        impl_->StopAndJoin();
    }
}

} // namespace gc::audio::diagnostics
