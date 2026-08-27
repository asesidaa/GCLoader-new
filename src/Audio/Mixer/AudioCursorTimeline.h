#pragma once

#include "Audio/ExactAudioTime.h"

#include <array>
#include <atomic>
// ReSharper disable once CppUnusedIncludeDirective
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

namespace gc::audio {

namespace detail {

// Sequence values and every slot payload scalar share this total order. A
// reader cannot accept two stale sequence observations around newer payload.
inline constexpr std::memory_order kRenderSpanAtomicOrder =
    std::memory_order_seq_cst;

} // namespace detail

inline constexpr std::size_t kRenderSpanCapacity = 32;

struct AudioRenderSpan {
    std::uint64_t output_frame_begin{};
    std::uint64_t output_frame_end{};
    std::uint64_t source_frame_begin_unwrapped{};
    std::uint64_t source_frame_end_unwrapped{};
    std::uint64_t epoch{};
    bool loop_wrapped{};
    bool source_ended{};
};

enum class AudioCursorResolutionKind : std::uint8_t {
    Resolved,
    PendingGeneration,
    Unmapped,
};

struct AudioCursorResolution {
    AudioCursorResolutionKind kind{};
    std::uint64_t source_frame{};
    std::uint64_t source_frame_unwrapped{};
};

enum class ExactPlaybackOrigin : std::uint8_t {
    Play,
    Seek,
};

enum class ExactPlaybackClosure : std::uint8_t {
    LaterEpoch,
    NaturalEnd,
    WriterQuiescedRelease,
};

struct ExactPlaybackEpoch {
    std::uint64_t buffer_instance_id{};
    std::uint64_t endpoint_generation{};
    std::uint64_t playback_generation{};
    ExactPlaybackOrigin origin{};
    std::uint64_t output_origin{};
    std::uint64_t source_origin{};
    std::uint32_t output_rate{};
    std::uint32_t source_rate{};
    std::uint64_t mapped_output_tail{};
    std::optional<ExactPlaybackClosure> closure;
    std::optional<gc::timing::CheckedRational> closed_source_tail;
};

struct ExactSourceCoordinate {
    gc::timing::CheckedRational source_frame;
    std::uint32_t source_rate{};
};

struct ExactSourceFrameResult {
    ExactClockStatus status{};
    std::uint64_t buffer_instance_id{};
    std::uint64_t playback_generation{};
    std::optional<ExactSourceCoordinate> resolved;
    std::optional<ExactSourceCoordinate> closed_frontier;
};

struct ExactPlaybackHistoryStatus
{
    ExactClockStatus status{};
    std::uint64_t publication_sequence{};
    bool prefix_evicted{};
};

enum class ExactMappedSpanPublicationFailure : std::uint8_t
{
    None,
    InvalidArguments,
    NaturalEndTailUnrepresentable,
    EpochCounterOverflow,
    PlaybackGenerationNotIncreasing,
    PreviousEpochUnavailable,
    PreviousEpochAlreadyClosed,
    PreviousEpochTailUnrepresentable,
    CurrentEpochUnavailable,
    CurrentEpochClosed,
    BufferInstanceChanged,
    EndpointGenerationChanged,
    PlaybackGenerationChanged,
    OriginChanged,
    OutputOriginChanged,
    SourceOriginChanged,
    OutputRateChanged,
    SourceRateChanged,
    MappedOutputTailNotIncreasing,
    PublicationSequenceUnavailable,
    SlotStoreFailed,
};

struct ExactMappedSpanPublicationFailureSnapshot
{
    ExactMappedSpanPublicationFailure reason{};
    std::uint64_t expected{};
    std::uint64_t actual{};
};

inline constexpr std::size_t kExactPlaybackEpochCapacity = 256;

class AudioCursorTimeline
{
public:
    void Publish(const AudioRenderSpan&) noexcept;
    AudioCursorResolution ResolveSourceFrame(
        std::uint64_t output_frame,
        std::uint64_t generation,
        std::uint64_t source_length_frames) const noexcept;

    bool ConfigureExactPlaybackHistory(
        std::uint64_t buffer_instance_id,
        std::uint64_t endpoint_generation) noexcept;
    bool AssignBufferInstanceId(std::uint64_t buffer_instance_id) noexcept;
    [[nodiscard]] bool HasExactPlaybackHistory() const noexcept;
    [[nodiscard]] std::uint64_t exact_buffer_instance_id() const noexcept;
    [[nodiscard]] std::uint64_t exact_endpoint_generation() const noexcept;
    bool ExpectExactPlaybackGeneration(
        std::uint64_t playback_generation) noexcept;
    bool PublishExactMappedSpan(
        std::uint64_t playback_generation,
        ExactPlaybackOrigin origin,
        std::uint64_t output_origin,
        std::uint64_t source_origin,
        std::uint32_t output_rate,
        std::uint32_t source_rate,
        std::uint64_t mapped_output_tail,
        bool natural_end,
        std::uint64_t natural_source_tail) noexcept;
    [[nodiscard]] ExactMappedSpanPublicationFailureSnapshot
    exact_mapped_span_publication_failure() const noexcept;
    bool CloseExactWriterAfterQuiescence() noexcept;
    ExactSourceFrameResult ResolveExactSourceFrame(
        const gc::timing::CheckedRational& output) const noexcept;
    std::size_t CopyExactPlaybackEpochs(
        std::span<ExactPlaybackEpoch> output,
        ExactPlaybackHistoryStatus* status) const noexcept;

private:
    bool FailExactMappedSpanPublication(
        ExactMappedSpanPublicationFailure reason,
        std::uint64_t expected = 0,
        std::uint64_t actual = 0) noexcept;
    bool BeginExactPublication(std::uint64_t* writing) noexcept;
    void EndExactPublication(std::uint64_t writing) noexcept;
    bool StoreExactSlot(
        std::size_t index,
        const ExactPlaybackEpoch& epoch) noexcept;
    std::optional<ExactPlaybackEpoch> LoadExactSlot(
        std::size_t index) const noexcept;

    struct Slot
    {
        std::atomic<std::uint64_t> sequence{};
        mutable AudioRenderSpan span{};
    };

    std::array<Slot, kRenderSpanCapacity> slots_{};
    std::atomic<std::uint64_t> published_generation_{};
    std::uint64_t writer_generation_{};

    struct ExactSlot
    {
        std::atomic_uint64_t version{};
        std::atomic_uint64_t buffer_instance_id{};
        std::atomic_uint64_t endpoint_generation{};
        std::atomic_uint64_t playback_generation{};
        std::atomic_uint8_t origin{};
        std::atomic_uint64_t output_origin{};
        std::atomic_uint64_t source_origin{};
        std::atomic_uint32_t output_rate{};
        std::atomic_uint32_t source_rate{};
        std::atomic_uint64_t mapped_output_tail{};
        std::atomic_bool closure_engaged{};
        std::atomic_uint8_t closure{};
        std::atomic_bool closed_tail_engaged{};
        std::atomic_int64_t closed_tail_numerator{};
        std::atomic_uint64_t closed_tail_denominator{1};
    };

    std::unique_ptr<std::array<ExactSlot, kExactPlaybackEpochCapacity>>
    exact_slots_;
    std::atomic_bool exact_configured_{};
    std::atomic_uint64_t exact_buffer_instance_id_{};
    std::atomic_uint64_t exact_endpoint_generation_{};
    std::atomic_uint64_t exact_publication_sequence_{};
    std::atomic_uint64_t exact_published_count_{};
    std::atomic_uint64_t exact_requested_generation_{};
    std::atomic_bool exact_prefix_evicted_{};
    std::atomic_bool exact_discontinuous_{};
    std::atomic_bool exact_mapped_span_failure_claimed_{};
    std::atomic_uint8_t exact_mapped_span_failure_reason_{};
    std::atomic_uint64_t exact_mapped_span_failure_expected_{};
    std::atomic_uint64_t exact_mapped_span_failure_actual_{};
    std::uint64_t exact_writer_epoch_count_{};
    std::uint64_t exact_writer_current_generation_{};
    std::size_t exact_writer_current_slot_{};
    bool exact_writer_has_current_{};
};

class EndpointClockMapper {
public:
    void Reset(
        std::uint64_t position,
        std::uint64_t frequency,
        std::uint64_t output_frame,
        std::uint32_t output_sample_rate) noexcept;
    [[nodiscard]] std::optional<std::uint64_t> ToOutputFrame(
        std::uint64_t position) const noexcept;
    [[nodiscard]] EndpointClockMapping mapping() const noexcept;

private:
    std::uint64_t origin_position_{};
    std::uint64_t frequency_{};
    std::uint64_t origin_output_frame_{};
    std::uint32_t output_sample_rate_{};
};

struct PresentedClockSnapshot {
    std::uint64_t presented_output_frame{};
    std::uint64_t sample_qpc_100ns{};
    std::uint64_t submitted_output_frame_end{};
};

class PresentedClockPublication {
public:
    void Publish(
        std::uint64_t presented_output_frame,
        std::uint64_t sample_qpc_100ns,
        std::uint64_t submitted_output_frame_end) noexcept;
    void Invalidate() noexcept;

    [[nodiscard]] std::optional<std::uint64_t> Project(
        std::uint64_t now_qpc_ticks,
        std::uint64_t qpc_frequency,
        std::uint32_t output_sample_rate) noexcept;

private:
    [[nodiscard]] std::optional<PresentedClockSnapshot>
        ReadStable() const noexcept;
    [[nodiscard]] std::optional<std::uint64_t>
        LastReturned() const noexcept;
    std::uint64_t RememberMonotonic(std::uint64_t frame) noexcept;

    std::atomic_uint64_t sequence_{};
    std::atomic_bool valid_{};
    std::atomic_uint64_t presented_output_frame_{};
    std::atomic_uint64_t sample_qpc_100ns_{};
    std::atomic_uint64_t submitted_output_frame_end_{};
    std::uint64_t writer_generation_{};
    std::atomic_uint64_t last_returned_{};
    std::atomic_bool has_last_returned_{};
};

// DirectSound cursor byte offsets occupy the DWORD domain. Returns zero when
// block_alignment is zero or the converted offset exceeds that domain.
std::uint64_t SourceFrameToByte(
    std::uint64_t source_frame,
    std::uint16_t block_alignment) noexcept;
std::uint64_t ProjectWriteCursorFrame(
    std::uint64_t play_frame,
    std::uint32_t endpoint_buffer_frames,
    std::uint32_t output_sample_rate,
    std::uint32_t source_rate,
    std::uint64_t source_length_frames) noexcept;

} // namespace gc::audio
