#pragma once

#include "Audio/DirectSound/GameplayAudioCursorObservation.h"
#include "Audio/ExactJudgementTimeline.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>

namespace gc::absolute_judgement
{
    struct JudgementStageClockAnchor final
    {
        std::uint64_t stage_generation{};
        std::uint64_t timeline_generation{};
        std::uint64_t buffer_instance_id{};
        std::uint64_t playback_generation{};
        std::uint64_t logical_output_origin{};
        std::uint64_t source_origin{};
        std::uint32_t logical_output_rate{};
        std::uint32_t source_rate{};
        std::int32_t game_time_offset_ms{};
        std::shared_ptr<const gc::audio::ExactJudgementTimeline> timeline;
    };

    enum class JudgementClockStatus : std::uint8_t
    {
        Pending,
        TemporarilyUnavailable,
        Resolved,
        UnsupportedContinuity,
        HistoryLostBeforeBinding,
        CheckedArithmeticFailure,
    };

    enum class JudgementClockFailure : std::uint8_t
    {
        None,
        InvalidStageBinding,
        TimelineProviderChanged,
        TimelineGenerationChanged,
        PlaybackHistoryObjectChangedBeforeAnchor,
        PlaybackHistoryTimelineChangedBeforeAnchor,
        StageOriginHistoryLost,
        TimelineProjectionDiscontinuous,
        InvalidClockRates,
        RationalOperationUnrepresentable,
    };

    struct JudgementClockResult final
    {
        JudgementClockStatus status{JudgementClockStatus::Pending};
        JudgementClockFailure failure{JudgementClockFailure::None};
        std::optional<gc::timing::CheckedRational> output_frame;
        std::optional<gc::timing::CheckedRational> judgement_seconds;
        std::uint64_t provider_anchor_sequence{};
        std::optional<std::uint64_t> provider_position;
    };

    struct JudgementClockBinding final
    {
        std::uint64_t stage_generation{};
        gc::timing::AbsoluteHostTime stage_entry_time{};
        std::int32_t game_time_offset_ms{};
        std::uint64_t pending_buffer_instance_id{};
        std::uint64_t pending_timeline_generation{};
        std::shared_ptr<const gc::audio::ExactJudgementTimeline> pending_timeline;
        std::shared_ptr<gc::audio::AudioCursorTimeline> pending_history;
        std::optional<JudgementStageClockAnchor> anchor;
    };

    class JudgementClockResolver final
    {
    public:
        void Reset(std::uint64_t stage_generation, const gc::timing::AbsoluteHostTime& stage_entry_time,
                   std::int32_t game_time_offset_ms) noexcept;

        [[nodiscard]] bool bound() const noexcept;
        [[nodiscard]] const JudgementStageClockAnchor& anchor() const noexcept;

        [[nodiscard]] JudgementClockResult TryBind(const gc::audio::GameplayAudioCursorObservation& selected,
                                                   const std::shared_ptr<const gc::audio::ExactJudgementTimeline>&
                                                   timeline,
                                                   std::span<gc::audio::ExactPlaybackEpoch> scratch) noexcept;

        [[nodiscard]] JudgementClockResult Resolve(const gc::timing::AbsoluteHostTime& timestamp,
                                                   gc::audio::ExactClockResolveIntent intent) const noexcept;

        [[nodiscard]] static gc::timing::CheckedRational
        ResolveLogicalQpcOrFatal(
            const gc::audio::LogicalQpcPlayAnchor& play_anchor,
            std::int32_t stage_game_time_offset_ms,
            std::int64_t query_qpc) noexcept;

    private:
        JudgementClockBinding binding_{};
    };
} // namespace gc::absolute_judgement
