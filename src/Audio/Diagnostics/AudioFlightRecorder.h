#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <type_traits>

namespace gc::audio::diagnostics {

enum class AudioDiagnosticEventKind : std::uint8_t {
    VoiceCreated,
    VoicePlay,
    VoiceStop,
    SeekRequested,
    SeekApplied,
    ConverterReset,
    RenderSpan,
    AudioResync,
};

enum class ConverterResetReason : std::uint8_t {
    Seek,
    OutputDiscontinuity,
};

enum class AudioResyncDecision : std::uint8_t {
    Unreadable,
    SuppressedInMargin,
    AllowedOutOfMargin,
};

struct AudioDiagnosticEvent {
    AudioDiagnosticEventKind kind{};
    std::uint8_t decision{};
    std::uint16_t flags{};
    std::uint32_t signed_value0{};
    std::uint32_t signed_value1{};
    std::uint64_t sequence{};
    std::uint64_t qpc_ticks{};
    std::uint64_t voice_id{};
    std::uint64_t epoch{};
    std::uint64_t generation{};
    std::uint64_t output_frame_begin{};
    std::uint64_t output_frame_end{};
    std::uint64_t source_frame_begin{};
    std::uint64_t source_frame_end{};
    std::uint64_t value0{};
    std::uint64_t value1{};
    std::uint64_t value2{};
    std::uint64_t value3{};
};

static_assert(std::is_trivially_copyable_v<AudioDiagnosticEvent>);
static_assert(sizeof(AudioDiagnosticEvent) <= 128);

struct SubmittedPcmMetadata {
    std::uint64_t endpoint_clock_position{};
    std::uint64_t endpoint_qpc_100ns{};
    std::uint64_t presented_output_frame{};
    std::uint64_t output_frame_begin{};
    std::uint64_t submitted_tail{};
    std::uint64_t discontinuity_frames{};
    std::uint64_t mixer_frames_read{};
    std::int32_t mixer_result{};
    std::uint8_t pacing_kind{};
};

struct AudioFlightRecorderSession {
    std::uint32_t sample_rate{};
    std::uint16_t channels{};
    std::uint16_t bits_per_sample{};
    std::uint32_t frames_per_block{};
    std::uint64_t qpc_frequency{};
};

struct PcmPublishResult {
    std::uint64_t sequence{};
    bool queued{};
};

class IAudioDiagnosticSink {
public:
    virtual ~IAudioDiagnosticSink() = default;

    virtual bool StartSession(
        const AudioFlightRecorderSession&) noexcept = 0;
    virtual void PublishEvent(AudioDiagnosticEvent) noexcept = 0;
    virtual PcmPublishResult PublishSubmittedPcm(
        const SubmittedPcmMetadata&,
        std::span<const std::int16_t>) noexcept = 0;
};

inline std::atomic<IAudioDiagnosticSink*> active_sink{};

inline void ActivateAudioDiagnosticSink(
    IAudioDiagnosticSink* sink) noexcept {
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

inline void DeactivateAudioDiagnosticSink(
    IAudioDiagnosticSink* sink) noexcept {
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

std::uint64_t CaptureAudioDiagnosticQpcTicks() noexcept;

inline void PublishActiveAudioDiagnosticEvent(
    AudioDiagnosticEvent event) noexcept {
    if (auto* const sink =
            active_sink.load(std::memory_order_acquire);
        sink != nullptr) {
        sink->PublishEvent(event);
    }
}

struct AudioFlightRecorderOptions {
    std::filesystem::path root_directory{"audio-diagnostics"};
    std::size_t pcm_queue_blocks{512};
    std::size_t event_queue_records{65'536};
    std::chrono::milliseconds checkpoint_interval{1'000};
    std::uint64_t maximum_seconds{1'800};
};

enum class AudioFlightRecorderState : std::uint8_t {
    Idle,
    Active,
    LimitReached,
    Failed,
    Stopped,
};

struct AudioFlightRecorderStatus {
    AudioFlightRecorderState state{};
    std::filesystem::path session_directory;
    std::uint64_t submitted_blocks{};
    std::uint64_t dropped_pcm_blocks{};
    std::uint64_t lost_events{};
    std::uint64_t checkpointed_blocks{};
    std::string error;
};

class AudioFlightRecorder final : public IAudioDiagnosticSink {
public:
    static std::unique_ptr<AudioFlightRecorder> Create(
        AudioFlightRecorderOptions = {}) noexcept;
    ~AudioFlightRecorder() override;

    AudioFlightRecorder(const AudioFlightRecorder&) = delete;
    AudioFlightRecorder& operator=(const AudioFlightRecorder&) = delete;

    bool StartSession(
        const AudioFlightRecorderSession&) noexcept override;
    void PublishEvent(AudioDiagnosticEvent) noexcept override;
    PcmPublishResult PublishSubmittedPcm(
        const SubmittedPcmMetadata&,
        std::span<const std::int16_t>) noexcept override;
    AudioFlightRecorderStatus status() const;
    void StopAndJoin() noexcept;

private:
    struct Impl;

    explicit AudioFlightRecorder(
        AudioFlightRecorderOptions);

    std::unique_ptr<Impl> impl_;
};

namespace detail {

struct PcmBlockView {
    std::uint64_t sequence{};
    SubmittedPcmMetadata metadata{};
    std::span<const std::int16_t> samples;
};

class SpscPcmBlockQueue final {
public:
    SpscPcmBlockQueue() noexcept;
    ~SpscPcmBlockQueue();

    SpscPcmBlockQueue(const SpscPcmBlockQueue&) = delete;
    SpscPcmBlockQueue& operator=(const SpscPcmBlockQueue&) = delete;

    bool Initialize(
        std::size_t capacity_blocks,
        std::size_t samples_per_block) noexcept;
    PcmPublishResult TryPush(
        const SubmittedPcmMetadata&,
        std::span<const std::int16_t>) noexcept;
    std::optional<PcmBlockView> TryPeek() noexcept;
    void Pop() noexcept;
    std::uint64_t completed_sequence() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

enum class EventReadKind : std::uint8_t {
    Empty,
    Ready,
};

struct EventReadResult {
    EventReadKind kind{};
    AudioDiagnosticEvent event{};
};

enum class EventPublishResult : std::uint8_t {
    Queued,
    Dropped,
};

class MpscAudioEventQueue final {
public:
    MpscAudioEventQueue() noexcept;
    ~MpscAudioEventQueue();

    MpscAudioEventQueue(const MpscAudioEventQueue&) = delete;
    MpscAudioEventQueue& operator=(const MpscAudioEventQueue&) = delete;

    bool Initialize(std::size_t capacity) noexcept;
    EventPublishResult TryPush(AudioDiagnosticEvent) noexcept;
    EventReadResult TryRead() noexcept;
    std::uint64_t lost_events() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace detail

} // namespace gc::audio::diagnostics
