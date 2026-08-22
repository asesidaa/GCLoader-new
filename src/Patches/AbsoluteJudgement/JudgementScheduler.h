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

namespace gc::absolute_judgement {

struct AbsoluteJudgementOuterProbe {
    NativeJudgementIdentity native{};
    bool group2_cursor_selected{};
    std::optional<gc::audio::GameplayAudioCursorObservation>
        group2_observation;
    std::shared_ptr<const gc::audio::ExactOutputClock> endpoint;
    gc::timing::AbsoluteHostTime now{};
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
    void BeginSemanticStage(
        std::uintptr_t tune_manager,
        gc::timing::AbsoluteHostTime stage_entry_time,
        std::int32_t game_time_offset_ms,
        std::int32_t hold_safe_frame,
        std::int32_t slide_hold_safe_frame) noexcept;
    void EndSemanticStage(std::uintptr_t tune_manager) noexcept;
    [[nodiscard]] bool SemanticStageOpen() const noexcept;
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
    ObserveNativeScoreCounters(
        const AbsoluteJudgementNativeScoreCounters& counters) const noexcept;
    void AccumulateQueryCounters(
        const AbsoluteJudgementQueryCounters& counters) const noexcept;
    void RecordTransientPublications(
        const AbsoluteJudgementTransientPublications& publications)
        const noexcept;
    [[noreturn]] void FailActiveStage(
        AbsoluteJudgementFatalPredicate predicate,
        AbsoluteJudgementFatalReason category,
        std::initializer_list<std::uint64_t> operands = {}) const noexcept;
    [[noreturn]] void FailHistoryInvariant(
        JudgementHistoryError error) const noexcept;

private:
    static constexpr std::size_t kDrainBatchCapacity = 1024;
    static constexpr std::uint64_t kProtectedReadyEventCount = 32;

    void ClearStageOwnedState() noexcept;
    void ValidateStageBindingOrFatal(
        const AbsoluteJudgementOuterProbe& probe) noexcept;
    void DrainTransportOrFatal() noexcept;
    void AccountCleanupDropsOrFatal() noexcept;
    [[nodiscard]] JudgementClockStatus
    ResolveUnresolvedPrefixOrFatal() noexcept;
    void TryActivateOrWait(
        const JudgementClockResult& entry_clock) noexcept;
    void SelectOuterHorizonOrFatal(
        const AbsoluteJudgementOuterProbe& probe) noexcept;
    void SetReadyHorizonOrFatal(
        const gc::timing::CheckedRational& ready) noexcept;
    void MarkReadyOverloadOrFatal(
        const gc::timing::CheckedRational& ready) noexcept;
    void ConsumeMarkedOverloadOrFatal(
        const ResolvedGameplayTransition& event) noexcept;

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
    void IncrementDiagnostic(std::uint64_t& value) noexcept;
    void FailForClockResult(const JudgementClockResult& result) noexcept;
    [[noreturn]] void Fatal(
        AbsoluteJudgementFatalPredicate predicate,
        AbsoluteJudgementFatalReason category,
        std::initializer_list<std::uint64_t> operands = {}) const noexcept;
    [[noreturn]] void FatalStageError(
        JudgementStageError error,
        const NativeJudgementIdentity* observed = nullptr) const noexcept;
    [[noreturn]] void FatalHistoryError(
        JudgementHistoryError error) const noexcept;
    [[nodiscard]] AbsoluteJudgementFatalSnapshot FatalSnapshot() const
        noexcept;
    [[nodiscard]] AbsoluteJudgementRuntimeSnapshot RuntimeSnapshot() const
        noexcept;
    [[nodiscard]] std::uint64_t PendingWorkCount() const noexcept;

    JudgementStage stage_;
    JudgementClockResolver clock_resolver_;
    JudgementHistory history_;

    std::array<gc::input::GameplayTransitionRecord,
               gc::input::kGameplayTransitionCapacity>
        unresolved_{};
    std::array<gc::input::GameplayTransitionRecord, kDrainBatchCapacity>
        drain_batch_{};
    std::array<gc::audio::ExactPlaybackEpoch,
               gc::audio::kExactPlaybackEpochCapacity>
        left_epoch_scratch_{};

    std::size_t unresolved_read_slot_{};
    std::size_t unresolved_size_{};
    std::uint64_t next_drain_sequence_{};
    std::uint64_t next_delivery_sequence_{};
    std::uint64_t pending_event_count_{};
    std::uint64_t marked_overload_count_{};
    std::uint64_t accumulated_clock_waits_{};

    std::optional<JudgementScopeCoordinate> last_resolved_coordinate_;
    std::optional<JudgementScopeCoordinate> committed_frontier_;
    bool committed_frontier_is_boundary_{};
    std::int64_t committed_boundary_index_{};
    bool has_committed_boundary_index_{};

    std::optional<gc::timing::CheckedRational> outer_horizon_;
    std::optional<ScheduledJudgementScope> outstanding_scope_;
    std::uint64_t outer_scope_count_{};
    std::uint64_t outer_event_scope_count_{};
    std::uint64_t outer_heartbeat_scope_count_{};
    bool outer_prepared_{};
    bool outer_event_barrier_recorded_{};

    std::optional<gc::timing::CheckedRational> last_output_frame_;
    std::optional<gc::timing::CheckedRational> last_j_;
    std::uint64_t last_anchor_sequence_{};
    std::optional<std::uint64_t> last_endpoint_position_;
    std::int64_t outer_now_qpc_{};
    std::int64_t last_qpc_{};
};

} // namespace gc::absolute_judgement
