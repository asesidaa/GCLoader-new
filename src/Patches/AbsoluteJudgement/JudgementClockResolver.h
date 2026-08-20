#pragma once

#include "Audio/DirectSound/GameplayAudioCursorObservation.h"
#include "Audio/Wasapi/ExactWasapiClock.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace gc::absolute_judgement {

struct ObservedPlaybackHistory {
    std::uint64_t buffer_instance_id{};
    std::uint64_t endpoint_generation{};
    std::uint64_t last_validated_publication{};
    std::uint64_t validation_candidate_publication{};
    std::shared_ptr<gc::audio::AudioCursorTimeline> history;
};

struct JudgementClockBinding {
    std::uint64_t endpoint_generation{};
    std::shared_ptr<const gc::audio::ExactWasapiClock> endpoint;
    std::vector<ObservedPlaybackHistory> observed_stage_bgm_histories;
};

struct JudgementClockResult {
    gc::audio::ExactClockStatus status{gc::audio::ExactClockStatus::Pending};
    std::optional<gc::timing::CheckedRational> output_frame;
    std::optional<gc::timing::CheckedRational> source_frame;
    std::optional<gc::timing::CheckedRational> judgement_seconds;
    std::optional<gc::timing::CheckedRational> closed_frontier_seconds;
    std::uint64_t buffer_instance_id{};
    std::uint64_t playback_generation{};
    bool checked_arithmetic_failure{};
};

struct JudgementPlaybackOriginResult {
    gc::audio::ExactClockStatus status{gc::audio::ExactClockStatus::Pending};
    std::optional<gc::timing::CheckedRational> judgement_seconds;
    bool checked_arithmetic_failure{};
};

struct JudgementHistoryValidationResult {
    gc::audio::ExactClockStatus status{gc::audio::ExactClockStatus::Pending};
    bool checked_arithmetic_failure{};
};

class JudgementClockResolver final {
public:
    JudgementHistoryValidationResult ValidateRetainedHistories(
        JudgementClockBinding& binding,
        std::array<gc::audio::ExactPlaybackEpoch,
                   gc::audio::kExactPlaybackEpochCapacity>& left_scratch,
        std::array<gc::audio::ExactPlaybackEpoch,
                   gc::audio::kExactPlaybackEpochCapacity>& right_scratch)
        const noexcept;

    JudgementClockResult ResolveHistoricalQpc(
        const JudgementClockBinding& binding,
        std::int64_t qpc_ticks,
        std::int32_t game_time_offset_ms,
        std::array<gc::audio::ExactPlaybackEpoch,
                   gc::audio::kExactPlaybackEpochCapacity>& scratch)
        const noexcept;

    JudgementClockResult ResolveCurrentQpc(
        const JudgementClockBinding& binding,
        const ObservedPlaybackHistory& selected_history,
        std::int64_t qpc_ticks,
        std::int32_t game_time_offset_ms,
        std::array<gc::audio::ExactPlaybackEpoch,
                   gc::audio::kExactPlaybackEpochCapacity>& scratch)
        const noexcept;

    JudgementPlaybackOriginResult FindFirstPlaybackOrigin(
        const JudgementClockBinding& binding,
        std::int32_t game_time_offset_ms,
        std::array<gc::audio::ExactPlaybackEpoch,
                   gc::audio::kExactPlaybackEpochCapacity>& scratch)
        const noexcept;
};

} // namespace gc::absolute_judgement
