#pragma once

#include "Audio/Mixer/AudioCursorTimeline.h"
#include "Timing/CheckedRational.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <variant>

namespace gc::audio
{
    enum class GameplayAudioCursorState : std::uint8_t
    {
        Exact,
        Pending,
        Inactive,
    };

    struct PresentedOutputCursorObservation
    {
        GameplayAudioCursorState state{};
        std::uint64_t source_frame_unwrapped{};
        std::uint32_t source_sample_rate{};
        std::uint64_t buffer_instance_id{};
        std::uint64_t timeline_generation{};
        std::uint64_t playback_generation{};
        ExactPlaybackOrigin origin{};
        std::uint64_t output_frame{};
        std::shared_ptr<AudioCursorTimeline> exact_history;
    };

    struct LogicalQpcPlayAnchor
    {
        std::uint64_t play_order{};
        std::int64_t play_qpc{};
        gc::timing::CheckedRational source_frame{
            gc::timing::CheckedRational::Whole(0)};
        std::uint32_t source_sample_rate{};
        std::int64_t qpc_frequency{};
    };

    struct LogicalQpcCursorObservation
    {
        bool playing{};
        std::optional<LogicalQpcPlayAnchor> current_play;
    };

    using GameplayAudioCursorPayload = std::variant<
        PresentedOutputCursorObservation,
        LogicalQpcCursorObservation>;

    struct GameplayAudioCursorObservation
    {
        std::uint64_t query_serial{};
        GameplayAudioCursorPayload payload;
    };

    class ScopedGameplayAudioCursorQuery final
    {
    public:
        ScopedGameplayAudioCursorQuery() noexcept;
        ~ScopedGameplayAudioCursorQuery();

        ScopedGameplayAudioCursorQuery(
            const ScopedGameplayAudioCursorQuery&) = delete;
        ScopedGameplayAudioCursorQuery& operator=(
            const ScopedGameplayAudioCursorQuery&) = delete;
        ScopedGameplayAudioCursorQuery(
            ScopedGameplayAudioCursorQuery&&) = delete;
        ScopedGameplayAudioCursorQuery& operator=(
            ScopedGameplayAudioCursorQuery&&) = delete;

        [[nodiscard]] std::optional<GameplayAudioCursorObservation>
        Consume() noexcept;

    private:
        std::uint64_t serial_{};
        bool owns_scope_{};
    };

    void PublishGameplayAudioCursorObservation(
        GameplayAudioCursorObservation observation) noexcept;

    [[nodiscard]] std::uint64_t SnapshotLogicalPlayOrder() noexcept;
    [[nodiscard]] std::uint64_t ClaimNextLogicalPlayOrder() noexcept;
} // namespace gc::audio
