#pragma once

#include "Audio/DirectSound/GameplayAudioCursorObservation.h"
#include "Input/Polling/GameplayTransitionJournal.h"

#include <cstdint>
#include <expected>
#include <optional>

namespace gc::absolute_judgement
{
    struct NativeJudgementIdentity
    {
        std::uint64_t stage_generation{};
        std::uintptr_t tune_manager{};
        std::uintptr_t tune{};
        std::uintptr_t judgement_state{};
        std::uintptr_t score_state{};
        std::uintptr_t booster{};
        std::uint32_t player{};
        std::int32_t game_time_offset_ms{};
        std::int32_t hold_safe_frame{};
        std::int32_t slide_hold_safe_frame{};
    };

    enum class JudgementStageError : std::uint8_t
    {
        AlreadyOpen,
        GenerationExhausted,
        TuneManagerMissing,
        InputTransportInactiveAtStageEntry,
        InputSequenceExhausted,
        InputQpcFrequencyInvalidAtStageEntry,
        StageNotOpen,
        StageGenerationChanged,
        TuneManagerChanged,
        TuneMissing,
        JudgementStateMissing,
        ScoreStateMissing,
        BoosterMissing,
        TuneChanged,
        JudgementStateChanged,
        ScoreStateChanged,
        BoosterChanged,
        PlayerChanged,
        TimelineGenerationChanged,
        QpcFrequencyChanged,
        HoldSafeFrameNonZero,
        SlideHoldSafeFrameNonZero,
    };

    class JudgementStage final
    {
    public:
        [[nodiscard]] std::expected<void, JudgementStageError> Begin(
            std::uintptr_t tune_manager,
            const gc::timing::AbsoluteHostTime& stage_entry_time,
            std::int32_t game_time_offset_ms,
            std::int32_t hold_safe_frame,
            std::int32_t slide_hold_safe_frame,
            bool input_transport_required) noexcept;
        [[nodiscard]] std::expected<void, JudgementStageError>
        BindOrValidateNative(const NativeJudgementIdentity& native) noexcept;
        [[nodiscard]] std::expected<void, JudgementStageError>
        BindTimelineOrValidate(
            std::uint64_t timeline_generation,
            std::int64_t timeline_qpc_frequency) noexcept;
        void Activate() noexcept;
        void ActivateLogicalClock() noexcept;
        void OfferLogicalQpcObservation(
            const gc::audio::LogicalQpcCursorObservation&) noexcept;
        void Reset() noexcept;

        [[nodiscard]] bool open() const noexcept;
        [[nodiscard]] bool active() const noexcept;
        [[nodiscard]] bool bound() const noexcept;
        [[nodiscard]] std::uint64_t generation() const noexcept;
        [[nodiscard]] std::uintptr_t tune_manager() const noexcept;
        [[nodiscard]] const NativeJudgementIdentity& native() const noexcept;
        [[nodiscard]] const gc::input::GameplayTransitionCutoff& cutoff()
        const noexcept;
        [[nodiscard]] std::uint64_t timeline_generation() const noexcept;
        [[nodiscard]] bool logical_clock_bound() const noexcept;
        [[nodiscard]] const gc::audio::LogicalQpcPlayAnchor&
        logical_play_anchor() const noexcept;
        [[nodiscard]] std::int32_t entry_game_time_offset_ms() const noexcept;
        [[nodiscard]] std::uint64_t logical_play_order_cutoff() const noexcept;
        [[nodiscard]] const gc::input::GameplayTransitionStatus&
        failure_transport_status() const noexcept;

    private:
        NativeJudgementIdentity native_{};
        gc::input::GameplayTransitionCutoff cutoff_{};
        gc::input::GameplayTransitionStatus failure_transport_status_{};
        std::uint64_t generation_{};
        std::uintptr_t tune_manager_{};
        std::uint64_t timeline_generation_{};
        std::uint64_t logical_play_order_cutoff_{};
        std::optional<gc::audio::LogicalQpcPlayAnchor> logical_play_anchor_;
        std::int32_t entry_game_time_offset_ms_{};
        std::int32_t entry_hold_safe_frame_{};
        std::int32_t entry_slide_hold_safe_frame_{};
        bool open_{};
        bool bound_{};
        bool active_{};
    };
} // namespace gc::absolute_judgement
