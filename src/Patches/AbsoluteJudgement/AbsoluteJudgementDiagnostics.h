#pragma once

#include "Input/Polling/GameplayTransitionJournal.h"
#include "Timing/CheckedRational.h"

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
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
    BackwardTime,
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
};

// Stable, exact predicates are the authority for terminating the process.
// AbsoluteJudgementFatalReason is retained only as a broad log category.
enum class AbsoluteJudgementFatalPredicate : std::uint16_t {
    None = 0,
    StartupSitePrefixMismatch,
    StartupHookCreateFailed,
    StartupHookEnableFailed,
    StartupHookTransactionInvalid,
    GameImageAddressInvalid,
    GameConfigurationMissing,
    GameConfigurationReadFailed,
    GlobalStateMissing,
    GameplaySoundManagerMissing,
    SemanticStageAlreadyOpen,
    SemanticStageMissingAtOwnedLoop,
    SemanticStageExitWithoutOpen,
    SemanticStageReceiverMismatch,
    CleanupWhileSemanticStageOpen,
    StageGenerationExhausted,
    QueryPerformanceCounterFailed,
    AudioBackendNotWasapiExclusive,
    ExactWasapiRouteUnavailable,
    InputTransportRateNot1000,
    InputTransportInactiveAtStageEntry,
    InputTransportWorkerBecameInactive,
    InputTransportEpochChanged,
    InputQpcFrequencyInvalidAtStageEntry,
    InputQpcFrequencyChanged,
    InputManagerMissing,
    BoosterMissing,
    TuneMissing,
    JudgementStateMissing,
    ScoreStateMissing,
    PlayerIndexInvalid,
    TuneIdentityChanged,
    JudgementStateIdentityChanged,
    ScoreStateIdentityChanged,
    PlayerIdentityChanged,
    BoosterIdentityChanged,
    HoldSafeFrameNonZero,
    SlideHoldSafeFrameNonZero,
    EndpointProviderMissingAtStageExit,
    StageOriginUnboundAtStageExit,
    EndpointGenerationChanged,
    EndpointProviderIdentityChanged,
    EndpointPublicationSequenceRegressed,
    EndpointQpcFrequencyMismatch,
    EndpointProjectionDiscontinuous,
    StageOriginHistoryLost,
    PlaybackHistoryObjectChangedBeforeAnchor,
    PlaybackHistoryEndpointChangedBeforeAnchor,
    TransportEvicted,
    TransportSequenceDiscontinuous,
    TransportMaskMismatch,
    TransportDrainContradiction,
    UnresolvedCapacityExhausted,
    HistoryCapacityExhausted,
    SequenceExhausted,
    RationalOperationUnrepresentable,
    HistoryNotInitialized,
    HistoryPrefixBeyondNext,
    HistoryPromisedEntryMissing,
    HistoryBaselineMaskInvalid,
    HistoryControlInvalid,
    ResolvedCoordinateRegressed,
    DeliveryOrderViolated,
    UnresolvedFrontEmpty,
    ScopeAlreadyActive,
    ScopeTlsOwnerMismatch,
    ScopeGenerationMismatch,
    ScopeReceiverMismatch,
    ScopeLifetimeMismatch,
    PressedFrameMismatch,
    DirectionOutputNull,
    RecognitionScoreTopologyMismatch,
    CommitTopologyMismatch,
    FatalRecordInvalid,
    StartupFatalPublisherReturned,
    TerminateProcessReturned,
    Count,
};

enum class AbsoluteJudgementFailureClass : std::uint8_t {
    ExplicitlyUnsupported,
    ResourceLimit,
    ProvenInternalInvariant,
};

struct AbsoluteJudgementFatalRecord {
    AbsoluteJudgementFatalPredicate predicate{
        AbsoluteJudgementFatalPredicate::None};
    AbsoluteJudgementFailureClass classification{
        AbsoluteJudgementFailureClass::ProvenInternalInvariant};
    std::uint64_t stage_generation{};
    AbsoluteJudgementFatalReason category{
        AbsoluteJudgementFatalReason::None};
    std::array<std::uint64_t, 8> operands{};
    std::uint8_t operand_count{};
};

[[nodiscard]] AbsoluteJudgementFatalRecord MakeAbsoluteJudgementFatalRecord(
    AbsoluteJudgementFatalPredicate predicate,
    std::uint64_t stage_generation,
    AbsoluteJudgementFatalReason category,
    std::initializer_list<std::uint64_t> operands = {}) noexcept;
[[nodiscard]] std::string_view AbsoluteJudgementFatalPredicateName(
    AbsoluteJudgementFatalPredicate predicate) noexcept;

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
    std::uint64_t semantic_stage_opens{};
    std::uint64_t absolute_stage_activations{};
    std::uint64_t semantic_stage_ends{};

    std::uint64_t transport_records_drained{};
    std::uint64_t transport_rising_controls{};
    std::uint64_t transport_falling_controls{};
    std::uint64_t transport_pending_depth{};
    std::uint64_t transport_max_depth{};
    std::uint64_t late_records{};
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

    std::uint64_t outer_calls{};
    std::uint64_t event_scopes{};
    std::uint64_t heartbeat_scopes{};
    std::uint64_t event_only_batches{};
    std::uint64_t heartbeat_only_batches{};
    std::uint64_t mixed_event_batches{};
    std::uint64_t event_barrier_deferrals{};
    std::uint64_t equal_boundary_substitutions{};
    std::uint64_t committed_boundaries{};
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
    std::uint64_t scope_trace_records{};
    std::uint64_t scope_trace_drops{};
    std::uint64_t score_observation_read_failures{};
    std::uint64_t score_counter_regressions{};
    std::uint64_t transient_publication_read_failures{};
    std::uint64_t delivery_delay_conversion_failures{};
    std::uint64_t final_accounting_mismatches{};
    std::uint64_t diagnostic_saturations{};
};

struct AbsoluteJudgementCounterSnapshot {
    std::uint64_t semantic_stage_opens{};
    std::uint64_t absolute_stage_activations{};
    std::uint64_t semantic_stage_ends{};

    std::uint64_t transport_records_drained{};
    std::uint64_t transport_rising_controls{};
    std::uint64_t transport_falling_controls{};
    std::uint64_t transport_pending_depth{};
    std::uint64_t transport_max_depth{};
    std::uint64_t late_records{};
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

    std::uint64_t outer_calls{};
    std::uint64_t event_scopes{};
    std::uint64_t heartbeat_scopes{};
    std::uint64_t event_only_batches{};
    std::uint64_t heartbeat_only_batches{};
    std::uint64_t mixed_event_batches{};
    std::uint64_t event_barrier_deferrals{};
    std::uint64_t equal_boundary_substitutions{};
    std::uint64_t committed_boundaries{};
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
    std::uint64_t scope_trace_records{};
    std::uint64_t scope_trace_drops{};
    std::uint64_t score_observation_read_failures{};
    std::uint64_t score_counter_regressions{};
    std::uint64_t transient_publication_read_failures{};
    std::uint64_t delivery_delay_conversion_failures{};
    std::uint64_t final_accounting_mismatches{};
    std::uint64_t diagnostic_saturations{};
};

struct AbsoluteJudgementRuntimeSnapshot {
    std::uint64_t last_endpoint_anchor_sequence{};
    std::optional<std::uint64_t> last_endpoint_position;
    std::optional<gc::timing::CheckedRational> last_output_frame;
    std::int64_t last_qpc{};
    std::optional<gc::timing::CheckedRational> last_j;
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

struct AbsoluteJudgementSemanticStageOpenRecord {
    std::uint64_t loader_stage_generation{};
    std::uintptr_t native_manager{};
    std::uint64_t input_generation{};
    std::uint64_t cutoff_sequence{};
    std::uint64_t first_eligible_sequence{};
    gc::input::GameplayHeldMask held_baseline{};
    std::uint64_t transport_fault_baseline{};
    std::int64_t stage_entry_qpc{};
    std::uint64_t stage_entry_handoff_drops{};
};

struct AbsoluteJudgementActivationRecord {
    AbsoluteJudgementNativeIdentityDiagnostic native{};
    std::uint64_t input_generation{};
    std::uint64_t endpoint_generation{};
    std::uint64_t buffer_instance_id{};
    std::uint64_t playback_generation{};
    std::uint64_t output_origin{};
    std::uint64_t source_origin{};
    std::uint32_t output_rate{};
    std::uint32_t source_rate{};
    gc::timing::CheckedRational initial_j =
        gc::timing::CheckedRational::Whole(0);
    std::int64_t committed_boundary_seed{};
    std::int64_t first_pending_boundary_index{};
    std::optional<gc::timing::CheckedRational> first_pending_boundary_j;
    std::optional<std::int32_t> first_pending_boundary_native_ms;
    std::optional<std::int32_t> first_pending_boundary_native_frame;
    std::uint64_t pending_negative_boundary_count{};
    std::int32_t game_time_offset_ms{};
    std::int32_t hold_safe_frame{};
    std::int32_t slide_hold_safe_frame{};
    std::uint64_t accumulated_clock_waits{};
};

struct AbsoluteJudgementSemanticStageEndRecord {
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
    AbsoluteJudgementNativeIdentityDiagnostic native{};
    std::uint64_t input_generation{};
    std::uint64_t endpoint_generation{};
    std::uint64_t last_anchor_sequence{};
    AbsoluteJudgementRuntimeSnapshot runtime{};
};

[[noreturn]] void FatalActiveStage(
    const AbsoluteJudgementFatalRecord& record,
    const AbsoluteJudgementFatalSnapshot& snapshot) noexcept;

class AbsoluteJudgementDiagnostics final {
public:
    [[nodiscard]] AbsoluteJudgementStageCounters& stage_counters() noexcept;
    [[nodiscard]] AbsoluteJudgementCounterSnapshot SnapshotCounters()
        const noexcept;

    void ObserveTransportPendingDepth(std::uint64_t depth) noexcept;
    void RecordBatch(std::uint64_t size) noexcept;
    void ObserveBacklog(std::uint64_t depth) noexcept;
    void ObserveEventBacklog(std::uint64_t depth) noexcept;
    void ObserveDeliveryDelayQpc(std::uint64_t delay) noexcept;
    void SetPendingWork(std::uint64_t count) noexcept;
    void SetStartupTargetFps(std::uint32_t target_fps) noexcept;
    [[nodiscard]] std::uint32_t startup_target_fps() const noexcept;

    void LogStartup(const AbsoluteJudgementStartupRecord& record) noexcept;
    void LogSemanticStageOpen(
        const AbsoluteJudgementSemanticStageOpenRecord& record) noexcept;
    void LogAbsoluteStageActivation(
        const AbsoluteJudgementActivationRecord& record) noexcept;
    void MaybeLogPeriodicDiagnostics(
        const AbsoluteJudgementRuntimeSnapshot& runtime) noexcept;
    void LogSemanticStageEnd(
        const AbsoluteJudgementSemanticStageEndRecord& record) noexcept;
    void ObserveScope(
        const AbsoluteJudgementScopeRecord& record) noexcept;

    void CheckNativeCallInvariantOrFatal(
        const AbsoluteJudgementFatalSnapshot& snapshot) noexcept;
    void CheckCompletedBatchInvariantOrFatal(
        const AbsoluteJudgementFatalSnapshot& snapshot) noexcept;
    void CheckFinalTransportIdentity() noexcept;
    void AccumulateQueryCounters(
        const AbsoluteJudgementQueryCounters& counters) noexcept;
    void RecordTransientPublications(
        const AbsoluteJudgementTransientPublications& publications) noexcept;
    void RecordScoreObservationReadFailure() noexcept;
    void RecordTransientPublicationReadFailure() noexcept;
    void RecordDeliveryDelayConversionFailure() noexcept;
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
    ObserveNativeScoreCounters(
        const AbsoluteJudgementNativeScoreCounters& counters) noexcept;

    [[nodiscard]] bool recognition_stopped() const noexcept;

private:
    friend AbsoluteJudgementDiagnostics& JudgementDiagnostics() noexcept;
    friend void FatalActiveStage(
        const AbsoluteJudgementFatalRecord&,
        const AbsoluteJudgementFatalSnapshot&) noexcept;

    void ResetStageState() noexcept;
    void FlushScopeTrace(std::string_view reason) noexcept;
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

    // Diagnostic-only storage. This exceeds the scheduler's maximum number
    // of completed scopes in one second at the supported 240-FPS target.
    static constexpr std::size_t kScopeTraceCapacity = 1024;

    AbsoluteJudgementStageCounters stage_{};
    IntervalMaxima interval_maxima_{};
    AbsoluteJudgementCounterSnapshot last_summary_{};
    std::atomic<AbsoluteJudgementFatalPredicate> first_fatal_predicate_{
        AbsoluteJudgementFatalPredicate::None};
    std::atomic_bool recognition_stopped_{false};
    std::optional<gc::timing::CheckedRational> last_committed_time_;
    std::uint64_t last_committed_sequence_{};
    bool has_committed_coordinate_{};
    std::int64_t last_heartbeat_index_{};
    bool has_heartbeat_index_{};
    AbsoluteJudgementNativeScoreCounters last_native_score_{};
    bool has_native_score_{};
    std::array<AbsoluteJudgementScopeRecord, kScopeTraceCapacity>
        scope_trace_{};
    std::size_t scope_trace_size_{};
    std::uint64_t scope_trace_drops_since_flush_{};
    std::uint64_t scope_trace_window_{};
    std::uint32_t startup_target_fps_{};
    ULONGLONG next_scope_trace_tick_ms_{};
    ULONGLONG next_summary_tick_ms_{};
};

AbsoluteJudgementDiagnostics& JudgementDiagnostics() noexcept;

} // namespace gc::absolute_judgement
