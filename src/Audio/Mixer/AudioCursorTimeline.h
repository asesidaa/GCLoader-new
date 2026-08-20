#pragma once

#include "Audio/ExactAudioTime.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>

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

class AudioCursorTimeline {
public:
    void Publish(const AudioRenderSpan&) noexcept;
    AudioCursorResolution ResolveSourceFrame(
        std::uint64_t output_frame,
        std::uint64_t generation,
        std::uint64_t source_length_frames) const noexcept;

private:
    struct Slot {
        std::atomic<std::uint64_t> sequence{};
        mutable AudioRenderSpan span{};
    };

    std::array<Slot, kRenderSpanCapacity> slots_{};
    std::atomic<std::uint64_t> published_generation_{};
    std::uint64_t writer_generation_{};
};

class EndpointClockMapper {
public:
    void Reset(
        std::uint64_t position,
        std::uint64_t frequency,
        std::uint64_t output_frame,
        std::uint32_t output_sample_rate) noexcept;
    std::optional<std::uint64_t> ToOutputFrame(
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
