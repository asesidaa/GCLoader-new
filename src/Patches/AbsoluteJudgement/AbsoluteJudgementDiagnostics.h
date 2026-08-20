#pragma once

#include "Input/Polling/GameplayTransitionJournal.h"
#include "Timing/CheckedRational.h"

#include <Windows.h>

#include <atomic>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace gc::absolute_judgement {

enum class AbsoluteJudgementFatalReason : std::uint32_t {
    None = 0,
    InputCapabilityUnavailable,
    EndpointCapabilityUnavailable,
    NativeIdentityChanged,
    EndpointGenerationChanged,
    InputGenerationChanged,
    NativeStateMismatch,
    ClockHistoryLost,
    ClockDiscontinuous,
    PlaybackMappingConflict,
    BackwardTime,
    GameTimeOffsetChanged,
    SafeFrameChanged,
    TransportEviction,
    TransportSequenceError,
    TransportEpochLost,
    RetainedHistoryLost,
    CheckedArithmeticFailure,
    CommittedOrderViolation,
    HeartbeatFrontierViolation,
    ScoreCounterRegression,
    NativeCallCountMismatch,
    ScopeThreadMismatch,
    ScopeReceiverMismatch,
    ScopeLifetimeViolation,
    StorageAllocationFailure,
    UnexpectedInternalException,
};

enum class AbsoluteJudgementScopeKind : std::uint8_t {
    Event,
    Heartbeat,
};

struct AbsoluteJudgementNativeIdentityDiagnostic {
    std::uint64_t stage_generation{};
    std::uintptr_t native_manager{};
    std::uintptr_t tune{};
    std::uintptr_t judgement_state{};
    std::uintptr_t score_state{};
    std::uintptr_t booster{};
    std::uint32_t player{};
};

struct AbsoluteJudgementPlaybackHistoryDiagnostic {
    std::uint64_t buffer_instance_id{};
    std::uint64_t endpoint_generation{};
    std::uint64_t last_playback_generation{};
    std::uint64_t play_epoch_count{};
    std::uint64_t seek_epoch_count{};
    std::uint64_t output_origin{};
    std::uint64_t source_origin{};
    std::uint32_t output_rate{};
    std::uint32_t source_rate{};
};

struct AbsoluteJudgementNativeScoreCounters {
    std::uint32_t miss{};
    std::uint32_t good{};
    std::uint32_t cool{};
    std::uint32_t great{};
};

struct AbsoluteJudgementScoreDeltas {
    std::uint64_t miss{};
    std::uint64_t good{};
    std::uint64_t cool{};
    std::uint64_t great{};
};

struct AbsoluteJudgementQueryCounters {
    std::uint64_t pressed_calls{};
    std::uint64_t pressed_true{};
    std::uint64_t held_calls{};
    std::uint64_t held_true{};
    std::uint64_t released_calls{};
    std::uint64_t released_true{};
    std::uint64_t direction_calls{};
    std::uint64_t direction_nonzero{};
    std::uint64_t held_age_calls{};
    std::uint64_t held_age_one{};
    std::uint64_t held_age_two_plus{};
};

struct AbsoluteJudgementTransientPublications {
    bool arrange{};
    bool left_free_tap{};
    bool right_free_tap{};
};

struct AbsoluteJudgementTransientPublicationCounts {
    std::uint64_t arrange{};
    std::uint64_t left_free_tap{};
    std::uint64_t right_free_tap{};
};

enum class AbsoluteJudgementBatchKind : std::uint8_t {
    EventOnly,
    HeartbeatOnly,
};

enum class AbsoluteJudgementEventIsolationDisposition : std::uint8_t {
    EventEndsBatch,
    HeartbeatOnlyBatch,
};

struct AbsoluteJudgementStageCounters {
    std::uint64_t native_stage_opens{};
    std::uint64_t absolute_stage_activations{};
    std::uint64_t native_stage_ends{};

    std::uint64_t transport_records_drained{};
    std::uint64_t transport_rising_controls{};
    std::uint64_t transport_falling_controls{};
    std::uint64_t transport_pending_depth{};
    std::uint64_t transport_max_depth{};
    std::uint64_t late_records{};
    std::uint64_t outside_playback_baseline_records{};
    std::uint64_t sequence_errors{};
    std::uint64_t post_cutoff_records{};
    std::uint64_t overload_drops{};
    std::uint64_t cleanup_drops{};
    std::uint64_t first_overload_drop_sequence{};
    std::uint64_t last_overload_drop_sequence{};

    std::uint64_t exact_clock_reads{};
    std::uint64_t resolved_clock_reads{};
    std::uint64_t unavailable_clock_reads{};
    std::uint64_t endpoint_publication_count{};
    std::uint64_t endpoint_stage_publications{};
    std::uint64_t playback_epochs{};
    std::uint64_t playback_play_epochs{};
    std::uint64_t playback_seek_epochs{};
    std::uint64_t closed_frontier_selections{};

    std::uint64_t outer_calls{};
    std::uint64_t event_scopes{};
    std::uint64_t heartbeat_scopes{};
    std::uint64_t event_only_batches{};
    std::uint64_t heartbeat_only_batches{};
    std::uint64_t mixed_event_batches{};
    std::uint64_t event_barrier_deferrals{};
    std::uint64_t equal_boundary_substitutions{};
    std::uint64_t committed_boundaries{};
    std::uint64_t closed_frontier_catchups{};
    std::uint64_t batches{};
    std::uint64_t maximum_batch{};
    std::uint64_t maximum_backlog{};
    std::uint64_t maximum_event_backlog{};
    std::uint64_t maximum_delivery_delay_qpc{};
    std::uint64_t pending_work{};

    std::uint64_t recognition_calls{};
    std::uint64_t score_calls{};
    AbsoluteJudgementQueryCounters queries{};
    AbsoluteJudgementScoreDeltas score_deltas{};
    AbsoluteJudgementTransientPublicationCounts transient_publications{};
};

struct AbsoluteJudgementCounterSnapshot {
    std::uint64_t native_stage_opens{};
    std::uint64_t absolute_stage_activations{};
    std::uint64_t native_stage_ends{};

    std::uint64_t transport_records_drained{};
    std::uint64_t transport_rising_controls{};
    std::uint64_t transport_falling_controls{};
    std::uint64_t transport_pending_depth{};
    std::uint64_t transport_max_depth{};
    std::uint64_t late_records{};
    std::uint64_t outside_playback_baseline_records{};
    std::uint64_t sequence_errors{};
    std::uint64_t post_cutoff_records{};
    std::uint64_t overload_drops{};
    std::uint64_t cleanup_drops{};
    std::uint64_t first_overload_drop_sequence{};
    std::uint64_t last_overload_drop_sequence{};

    std::uint64_t exact_clock_reads{};
    std::uint64_t resolved_clock_reads{};
    std::uint64_t unavailable_clock_reads{};
    std::uint64_t endpoint_publication_count{};
    std::uint64_t endpoint_stage_publications{};
    std::uint64_t playback_epochs{};
    std::uint64_t playback_play_epochs{};
    std::uint64_t playback_seek_epochs{};
    std::uint64_t closed_frontier_selections{};

    std::uint64_t outer_calls{};
    std::uint64_t event_scopes{};
    std::uint64_t heartbeat_scopes{};
    std::uint64_t event_only_batches{};
    std::uint64_t heartbeat_only_batches{};
    std::uint64_t mixed_event_batches{};
    std::uint64_t event_barrier_deferrals{};
    std::uint64_t equal_boundary_substitutions{};
    std::uint64_t committed_boundaries{};
    std::uint64_t closed_frontier_catchups{};
    std::uint64_t batches{};
    std::uint64_t maximum_batch{};
    std::uint64_t maximum_backlog{};
    std::uint64_t maximum_event_backlog{};
    std::uint64_t maximum_delivery_delay_qpc{};
    std::uint64_t pending_work{};

    std::uint64_t recognition_calls{};
    std::uint64_t score_calls{};
    AbsoluteJudgementQueryCounters queries{};
    AbsoluteJudgementScoreDeltas score_deltas{};
    AbsoluteJudgementTransientPublicationCounts transient_publications{};
};

struct AbsoluteJudgementRuntimeSnapshot {
    std::uint64_t last_endpoint_anchor_sequence{};
    std::optional<std::uint64_t> last_endpoint_position;
    std::optional<gc::timing::CheckedRational> last_output_frame;
    std::optional<gc::timing::CheckedRational> last_source_frame;
    std::int64_t last_qpc{};
    std::optional<gc::timing::CheckedRational> last_j;
    std::optional<gc::timing::CheckedRational> last_closed_frontier;
    std::optional<gc::timing::CheckedRational> frozen_j;
    std::int64_t committed_boundary{};
    std::uint64_t pending_work{};
    std::uint64_t last_sequence{};
    gc::input::GameplayHeldMask held_mask{};
    std::int32_t game_time_offset_ms{};
    std::int32_t hold_safe_frame{};
    std::int32_t slide_hold_safe_frame{};
};

struct AbsoluteJudgementStartupRecord {
    bool enabled{};
    std::uint32_t target_fps{};
    std::uint32_t input_rate_hz{};
    std::string_view backend;
    bool exact_provider_capable{};
    std::uint32_t installed_site_count{};
};

struct AbsoluteJudgementNativeStageOpenRecord {
    std::uint64_t loader_stage_generation{};
    std::uintptr_t native_manager{};
    std::uint64_t input_generation{};
    std::uint64_t cutoff_sequence{};
    std::uint64_t first_eligible_sequence{};
    gc::input::GameplayHeldMask held_baseline{};
    std::uint64_t transport_fault_baseline{};
};

struct AbsoluteJudgementActivationRecord {
    AbsoluteJudgementNativeIdentityDiagnostic native{};
    std::uint64_t input_generation{};
    std::uint64_t endpoint_generation{};
    std::span<const AbsoluteJudgementPlaybackHistoryDiagnostic> histories;
    gc::timing::CheckedRational initial_j =
        gc::timing::CheckedRational::Whole(0);
    std::int64_t committed_boundary_seed{};
    std::int32_t game_time_offset_ms{};
    std::int32_t hold_safe_frame{};
    std::int32_t slide_hold_safe_frame{};
    std::uint64_t accumulated_clock_waits{};
};

struct AbsoluteJudgementNativeStageEndRecord {
    std::uint64_t loader_stage_generation{};
    std::uintptr_t native_manager{};
    bool activated{};
    AbsoluteJudgementRuntimeSnapshot runtime{};
};

struct AbsoluteJudgementScopeRecord {
    AbsoluteJudgementNativeIdentityDiagnostic native{};
    std::uint64_t scope_id{};
    AbsoluteJudgementScopeKind kind{};
    bool equal_boundary_substitution{};
    std::uint64_t journal_sequence{};
    gc::timing::CheckedRational mapped_j =
        gc::timing::CheckedRational::Whole(0);
    std::int32_t native_ms{};
    std::int32_t native_frame{};
    gc::timing::CheckedRational delivery_delay =
        gc::timing::CheckedRational::Whole(0);
    gc::input::GameplayHeldMask held_before{};
    gc::input::GameplayHeldMask held_after{};
    gc::input::GameplayHeldMask rising{};
    gc::input::GameplayHeldMask falling{};
    AbsoluteJudgementQueryCounters queries{};
    bool recognition_completed{};
    bool score_completed{};
    AbsoluteJudgementScoreDeltas score_deltas{};
    AbsoluteJudgementTransientPublications transient_publications{};
    AbsoluteJudgementBatchKind batch_kind{};
    AbsoluteJudgementEventIsolationDisposition isolation_disposition{};
    bool boundary_committed{};
    std::int64_t committed_boundary{};
    std::uint64_t remaining_backlog{};
};

struct AbsoluteJudgementFatalSnapshot {
    bool enabled{};
    std::uint32_t target_fps{};
    AbsoluteJudgementNativeIdentityDiagnostic native{};
    std::uint64_t input_generation{};
    std::uint64_t endpoint_generation{};
    std::uint64_t last_anchor_sequence{};
    std::span<const AbsoluteJudgementPlaybackHistoryDiagnostic> histories;
    AbsoluteJudgementRuntimeSnapshot runtime{};
};

[[noreturn]] void FatalActiveStage(
    AbsoluteJudgementFatalReason reason,
    const AbsoluteJudgementFatalSnapshot& snapshot) noexcept;

class AbsoluteJudgementDiagnostics final {
public:
    [[nodiscard]] AbsoluteJudgementStageCounters& stage_counters() noexcept;
    [[nodiscard]] AbsoluteJudgementCounterSnapshot SnapshotCounters()
        const noexcept;

    void ObserveTransportPendingDepth(std::uint64_t depth) noexcept;
    void RecordBatch(
        std::uint64_t size,
        const AbsoluteJudgementFatalSnapshot& snapshot) noexcept;
    void ObserveBacklog(std::uint64_t depth) noexcept;
    void ObserveEventBacklog(std::uint64_t depth) noexcept;
    void ObserveDeliveryDelayQpc(std::uint64_t delay) noexcept;
    void SetPendingWork(std::uint64_t count) noexcept;
    void SetStartupTargetFps(std::uint32_t target_fps) noexcept;
    [[nodiscard]] std::uint32_t startup_target_fps() const noexcept;

    void LogStartup(const AbsoluteJudgementStartupRecord& record) noexcept;
    void LogNativeStageOpen(
        const AbsoluteJudgementNativeStageOpenRecord& record) noexcept;
    void LogAbsoluteStageActivation(
        const AbsoluteJudgementActivationRecord& record) noexcept;
    void MaybeLogFiveSecondSummary(
        const AbsoluteJudgementRuntimeSnapshot& runtime) noexcept;
    void LogNativeStageEnd(
        const AbsoluteJudgementNativeStageEndRecord& record) noexcept;
    void LogScopeVerbose(
        const AbsoluteJudgementScopeRecord& record) noexcept;

    void CheckNativeCallInvariantOrFatal(
        const AbsoluteJudgementFatalSnapshot& snapshot) noexcept;
    void CheckCompletedBatchInvariantOrFatal(
        const AbsoluteJudgementFatalSnapshot& snapshot) noexcept;
    void CheckFinalTransportIdentityOrFatal(
        const AbsoluteJudgementFatalSnapshot& snapshot) noexcept;
    void AccumulateQueryCountersOrFatal(
        const AbsoluteJudgementQueryCounters& counters,
        const AbsoluteJudgementFatalSnapshot& snapshot) noexcept;
    void RecordTransientPublicationsOrFatal(
        const AbsoluteJudgementTransientPublications& publications,
        const AbsoluteJudgementFatalSnapshot& snapshot) noexcept;
    void CheckAndRecordCommittedOrderOrFatal(
        const gc::timing::CheckedRational& time,
        std::uint64_t sequence,
        const AbsoluteJudgementFatalSnapshot& snapshot) noexcept;
    void SeedHeartbeatIndex(std::int64_t index) noexcept;
    void CheckAndRecordHeartbeatIndexOrFatal(
        std::int64_t index,
        bool due_boundary,
        const AbsoluteJudgementFatalSnapshot& snapshot) noexcept;
    [[nodiscard]] AbsoluteJudgementScoreDeltas
    CheckAndRecordNativeScoreCountersOrFatal(
        const AbsoluteJudgementNativeScoreCounters& counters,
        const AbsoluteJudgementFatalSnapshot& snapshot) noexcept;

    [[nodiscard]] bool recognition_stopped() const noexcept;

private:
    friend AbsoluteJudgementDiagnostics& JudgementDiagnostics() noexcept;
    friend void FatalActiveStage(
        AbsoluteJudgementFatalReason,
        const AbsoluteJudgementFatalSnapshot&) noexcept;

    void ResetStageState() noexcept;
    void LogSummary(
        std::string_view record_name,
        const AbsoluteJudgementRuntimeSnapshot& runtime,
        const AbsoluteJudgementCounterSnapshot& cumulative) noexcept;
    [[nodiscard]] AbsoluteJudgementCounterSnapshot SnapshotIntervalCounters(
        const AbsoluteJudgementCounterSnapshot& cumulative) const noexcept;
    void ResetIntervalMaxima() noexcept;

    struct IntervalMaxima final {
        std::uint64_t transport_depth{};
        std::uint64_t batch{};
        std::uint64_t backlog{};
        std::uint64_t event_backlog{};
        std::uint64_t delivery_delay_qpc{};
    };

    AbsoluteJudgementStageCounters stage_{};
    IntervalMaxima interval_maxima_{};
    AbsoluteJudgementCounterSnapshot last_summary_{};
    std::atomic<AbsoluteJudgementFatalReason> first_fatal_reason_{
        AbsoluteJudgementFatalReason::None};
    std::atomic_bool recognition_stopped_{false};
    std::optional<gc::timing::CheckedRational> last_committed_time_;
    std::uint64_t last_committed_sequence_{};
    bool has_committed_coordinate_{};
    std::int64_t last_heartbeat_index_{};
    bool has_heartbeat_index_{};
    AbsoluteJudgementNativeScoreCounters last_native_score_{};
    bool has_native_score_{};
    std::uint32_t startup_target_fps_{};
    ULONGLONG next_summary_tick_ms_{};
};

AbsoluteJudgementDiagnostics& JudgementDiagnostics() noexcept;

} // namespace gc::absolute_judgement
