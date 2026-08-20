#pragma once

#include "Patches/AbsoluteJudgement/AbsoluteJudgementDiagnostics.h"
#include "Patches/AbsoluteJudgement/JudgementClockResolver.h"
#include "Patches/AbsoluteJudgement/JudgementHistory.h"
#include "Patches/AbsoluteJudgement/JudgementStage.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace gc::absolute_judgement {

struct AbsoluteJudgementOuterProbe {
    NativeJudgementIdentity native{};
    bool group2_playing{};
    std::optional<gc::audio::GameplayAudioCursorObservation>
        group2_observation;
    std::shared_ptr<const gc::audio::ExactWasapiClock> endpoint;
    std::int64_t now_qpc{};
};

struct ScheduledJudgementScope {
    JudgementScopeKind kind{};
    JudgementScopeCoordinate coordinate{};
    std::int32_t native_ms{};
    std::int32_t native_frame{};
    const ResolvedGameplayTransition* event{};
    std::uint64_t history_prefix_end_sequence{};
    bool commits_boundary{};
};

class JudgementScheduler final {
public:
    void BeginNativeStage(std::uintptr_t tune_manager) noexcept;
    void EndNativeStage(std::uintptr_t tune_manager) noexcept;
    [[nodiscard]] bool NativeStageOpen() const noexcept;
    [[nodiscard]] std::uint64_t stage_generation() const noexcept;
    [[nodiscard]] const NativeJudgementIdentity& native_identity()
        const noexcept;
    [[nodiscard]] const JudgementHistory& history() const noexcept;
    [[nodiscard]] std::optional<std::int64_t>
    committed_boundary_index() const noexcept;

    void PrepareOuterCall(const AbsoluteJudgementOuterProbe&);
    std::optional<ScheduledJudgementScope> NextScope() noexcept;
    void CommitScope(const ScheduledJudgementScope&) noexcept;
    void FinishOuterCall() noexcept;
    void CheckNativeCallInvariantOrFatal() const noexcept;
    [[nodiscard]] AbsoluteJudgementScoreDeltas
    CheckAndRecordNativeScoreCountersOrFatal(
        const AbsoluteJudgementNativeScoreCounters& counters) const noexcept;
    [[noreturn]] void FailActiveStage(
        AbsoluteJudgementFatalReason reason) const noexcept;

private:
    static constexpr std::size_t kDrainBatchCapacity = 1024;

    void ClearStageOwnedState() noexcept;
    void ValidateStageBindingOrFatal(
        const AbsoluteJudgementOuterProbe& probe) noexcept;
    ObservedPlaybackHistory* RegisterOrValidateObservation(
        const gc::audio::GameplayAudioCursorObservation& observation);
    [[nodiscard]] gc::audio::ExactClockStatus
    UpdatePlaybackDiagnostics() noexcept;
    void DrainTransportOrFatal() noexcept;
    [[nodiscard]] gc::audio::ExactClockStatus
    ResolveUnresolvedPrefixOrFatal(
        gc::audio::ExactClockStatus validation_status) noexcept;
    void TryActivateOrWait(
        gc::audio::ExactClockStatus validation_status) noexcept;
    void SelectOuterHorizonOrFatal(
        const AbsoluteJudgementOuterProbe& probe,
        const ObservedPlaybackHistory* selected) noexcept;
    void SetReadyHorizonOrFatal(
        const gc::timing::CheckedRational& ready,
        bool closed_frontier) noexcept;

    [[nodiscard]] std::uint64_t CurrentHistoryPrefixEnd() const noexcept;
    [[nodiscard]] std::optional<ScheduledJudgementScope>
    MakeEventScope(const ResolvedGameplayTransition& event,
                   const gc::timing::CheckedRational& boundary) noexcept;
    [[nodiscard]] std::optional<ScheduledJudgementScope>
    MakeHeartbeatScope(const gc::timing::CheckedRational& boundary) noexcept;
    [[nodiscard]] bool IsBehindCommittedFrontier(
        const JudgementScopeCoordinate& coordinate) const noexcept;
    [[nodiscard]] std::optional<gc::timing::CheckedRational>
    BoundaryAt(std::int64_t index) const noexcept;
    [[nodiscard]] std::optional<std::pair<std::int32_t, std::int32_t>>
    NativeArguments(
        const gc::timing::CheckedRational& judgement_seconds) const noexcept;

    void AppendUnresolvedOrFatal(
        const gc::input::GameplayTransitionRecord& record) noexcept;
    [[nodiscard]] gc::input::GameplayTransitionRecord&
    UnresolvedFront() noexcept;
    void PopUnresolved() noexcept;
    void ApplyHistoryResultOrFatal(
        const std::expected<void, JudgementHistoryError>& result) noexcept;
    void IncrementOrFatal(std::uint64_t& value) noexcept;
    void FailForClockResult(const JudgementClockResult& result) noexcept;
    [[noreturn]] void Fatal(
        AbsoluteJudgementFatalReason reason) const noexcept;
    [[nodiscard]] AbsoluteJudgementFatalSnapshot FatalSnapshot() const
        noexcept;
    [[nodiscard]] AbsoluteJudgementRuntimeSnapshot RuntimeSnapshot() const
        noexcept;
    [[nodiscard]] std::uint64_t PendingWorkCount() const noexcept;

    JudgementStage stage_;
    JudgementClockResolver clock_resolver_;
    JudgementClockBinding clock_binding_;
    JudgementHistory history_;

    std::array<gc::input::GameplayTransitionRecord,
               gc::input::kGameplayTransitionCapacity>
        unresolved_{};
    std::array<gc::input::GameplayTransitionRecord, kDrainBatchCapacity>
        drain_batch_{};
    std::array<gc::audio::ExactPlaybackEpoch,
               gc::audio::kExactPlaybackEpochCapacity>
        left_epoch_scratch_{};
    std::array<gc::audio::ExactPlaybackEpoch,
               gc::audio::kExactPlaybackEpochCapacity>
        right_epoch_scratch_{};
    std::vector<AbsoluteJudgementPlaybackHistoryDiagnostic>
        history_diagnostics_;

    std::size_t unresolved_read_slot_{};
    std::size_t unresolved_size_{};
    std::uint64_t next_drain_sequence_{};
    std::uint64_t next_delivery_sequence_{};
    std::uint64_t pending_event_count_{};
    std::uint64_t last_selected_buffer_instance_id_{};
    std::uint64_t accumulated_clock_waits_{};

    std::optional<JudgementScopeCoordinate> last_resolved_coordinate_;
    std::optional<JudgementScopeCoordinate> committed_frontier_;
    bool committed_frontier_is_boundary_{};
    std::int64_t committed_boundary_index_{};
    bool has_committed_boundary_index_{};

    std::optional<gc::timing::CheckedRational> outer_horizon_;
    std::optional<ScheduledJudgementScope> outstanding_scope_;
    std::uint64_t outer_scope_count_{};
    bool outer_prepared_{};
    bool outer_uses_closed_frontier_{};

    std::optional<gc::timing::CheckedRational> last_output_frame_;
    std::optional<gc::timing::CheckedRational> last_source_frame_;
    std::optional<gc::timing::CheckedRational> last_j_;
    std::uint64_t last_anchor_sequence_{};
    std::int64_t outer_now_qpc_{};
    std::int64_t last_qpc_{};
};

} // namespace gc::absolute_judgement
