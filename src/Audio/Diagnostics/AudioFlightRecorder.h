#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
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

void ActivateAudioDiagnosticSink(IAudioDiagnosticSink*) noexcept;
void DeactivateAudioDiagnosticSink(IAudioDiagnosticSink*) noexcept;
std::uint64_t CaptureAudioDiagnosticQpcTicks() noexcept;
void PublishActiveAudioDiagnosticEvent(
    AudioDiagnosticEvent) noexcept;

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
