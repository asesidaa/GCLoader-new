#pragma once

#include "Audio/DirectSound/GameplayAudioCursorObservation.h"
#include "Audio/Wasapi/ExactWasapiClock.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>

namespace gc::absolute_judgement {

struct JudgementStageClockAnchor final {
    std::uint64_t stage_generation{};
    std::uint64_t endpoint_generation{};
    std::uint64_t buffer_instance_id{};
    std::uint64_t playback_generation{};
    std::uint64_t output_origin{};
    std::uint64_t source_origin{};
    std::uint32_t output_rate{};
    std::uint32_t source_rate{};
    std::int32_t game_time_offset_ms{};
    std::shared_ptr<const gc::audio::ExactWasapiClock> endpoint;
};

enum class JudgementClockStatus : std::uint8_t {
    Pending,
    TemporarilyUnavailable,
    Resolved,
    UnsupportedContinuity,
    HistoryLostBeforeBinding,
    CheckedArithmeticFailure,
};

struct JudgementClockResult final {
    JudgementClockStatus status{JudgementClockStatus::Pending};
    std::optional<gc::timing::CheckedRational> output_frame;
    std::optional<gc::timing::CheckedRational> judgement_seconds;
    std::uint64_t endpoint_anchor_sequence{};
    std::optional<std::uint64_t> endpoint_position;
};

struct JudgementClockBinding final {
    std::uint64_t stage_generation{};
    std::int64_t stage_entry_qpc{};
    std::int32_t game_time_offset_ms{};
    std::uint64_t pending_buffer_instance_id{};
    std::uint64_t pending_endpoint_generation{};
    std::shared_ptr<const gc::audio::ExactWasapiClock> pending_endpoint;
    std::shared_ptr<gc::audio::AudioCursorTimeline> pending_history;
    std::optional<JudgementStageClockAnchor> anchor;
};

class JudgementClockResolver final {
public:
    void Reset(
        std::uint64_t stage_generation,
        std::int64_t stage_entry_qpc,
        std::int32_t game_time_offset_ms) noexcept;

    [[nodiscard]] bool bound() const noexcept;
    [[nodiscard]] const JudgementStageClockAnchor& anchor() const noexcept;

    [[nodiscard]] JudgementClockResult TryBind(
        const gc::audio::GameplayAudioCursorObservation& selected,
        std::shared_ptr<const gc::audio::ExactWasapiClock> endpoint,
        std::span<gc::audio::ExactPlaybackEpoch> scratch) noexcept;

    [[nodiscard]] JudgementClockResult ResolveQpc(
        std::int64_t qpc_ticks) const noexcept;

private:
    JudgementClockBinding binding_{};
};

} // namespace gc::absolute_judgement
