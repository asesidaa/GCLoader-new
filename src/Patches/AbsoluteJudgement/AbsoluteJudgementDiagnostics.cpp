#include "Patches/AbsoluteJudgement/AbsoluteJudgementDiagnostics.h"

#include "Logging/SessionLog.h"

#include <plog/Log.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <format>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>

namespace gc::absolute_judgement {

namespace {

constexpr ULONGLONG kSummaryCadenceMilliseconds = 5'000;
constexpr ULONGLONG kScopeTraceCadenceMilliseconds = 1'000;
constexpr std::size_t kScopeTraceEntriesPerLine = 16;

std::uint64_t SubtractMonotonic(
    std::uint64_t value,
    std::uint64_t baseline) noexcept {
    return value >= baseline ? value - baseline : 0;
}

void AddSaturating(std::uint64_t& value,
                   const std::uint64_t addend) noexcept {
    value = addend <= (std::numeric_limits<std::uint64_t>::max)() - value
        ? value + addend
        : (std::numeric_limits<std::uint64_t>::max)();
}

void IncrementSaturating(std::uint64_t& value) noexcept {
    AddSaturating(value, 1);
}

AbsoluteJudgementQueryCounters SubtractQueries(
    const AbsoluteJudgementQueryCounters& value,
    const AbsoluteJudgementQueryCounters& baseline) noexcept {
    return {
        .pressed_calls = SubtractMonotonic(
            value.pressed_calls, baseline.pressed_calls),
        .pressed_true = SubtractMonotonic(
            value.pressed_true, baseline.pressed_true),
        .held_calls = SubtractMonotonic(
            value.held_calls, baseline.held_calls),
        .held_true = SubtractMonotonic(
            value.held_true, baseline.held_true),
        .released_calls = SubtractMonotonic(
            value.released_calls, baseline.released_calls),
        .released_true = SubtractMonotonic(
            value.released_true, baseline.released_true),
        .direction_calls = SubtractMonotonic(
            value.direction_calls, baseline.direction_calls),
        .direction_nonzero = SubtractMonotonic(
            value.direction_nonzero, baseline.direction_nonzero),
        .held_age_calls = SubtractMonotonic(
            value.held_age_calls, baseline.held_age_calls),
        .held_age_one = SubtractMonotonic(
            value.held_age_one, baseline.held_age_one),
        .held_age_two_plus = SubtractMonotonic(
            value.held_age_two_plus, baseline.held_age_two_plus),
    };
}

AbsoluteJudgementScoreDeltas SubtractScoreDeltas(
    const AbsoluteJudgementScoreDeltas& value,
    const AbsoluteJudgementScoreDeltas& baseline) noexcept {
    return {
        .miss = SubtractMonotonic(value.miss, baseline.miss),
        .good = SubtractMonotonic(value.good, baseline.good),
        .cool = SubtractMonotonic(value.cool, baseline.cool),
        .great = SubtractMonotonic(value.great, baseline.great),
    };
}

AbsoluteJudgementTransientPublicationCounts SubtractTransientPublications(
    const AbsoluteJudgementTransientPublicationCounts& value,
    const AbsoluteJudgementTransientPublicationCounts& baseline) noexcept {
    return {
        .arrange = SubtractMonotonic(value.arrange, baseline.arrange),
        .left_free_tap = SubtractMonotonic(
            value.left_free_tap, baseline.left_free_tap),
        .right_free_tap = SubtractMonotonic(
            value.right_free_tap, baseline.right_free_tap),
    };
}

AbsoluteJudgementCounterSnapshot SubtractCounters(
    const AbsoluteJudgementCounterSnapshot& value,
    const AbsoluteJudgementCounterSnapshot& baseline) noexcept {
    AbsoluteJudgementCounterSnapshot result{};
#define GC_SUBTRACT_COUNTER(field) \
    result.field = SubtractMonotonic(value.field, baseline.field)
    GC_SUBTRACT_COUNTER(semantic_stage_opens);
    GC_SUBTRACT_COUNTER(absolute_stage_activations);
    GC_SUBTRACT_COUNTER(semantic_stage_ends);
    GC_SUBTRACT_COUNTER(transport_records_drained);
    GC_SUBTRACT_COUNTER(transport_rising_controls);
    GC_SUBTRACT_COUNTER(transport_falling_controls);
    result.transport_pending_depth = value.transport_pending_depth;
    GC_SUBTRACT_COUNTER(late_records);
    GC_SUBTRACT_COUNTER(sequence_errors);
    GC_SUBTRACT_COUNTER(post_cutoff_records);
    GC_SUBTRACT_COUNTER(overload_drops);
    GC_SUBTRACT_COUNTER(cleanup_drops);
    result.first_overload_drop_sequence =
        value.first_overload_drop_sequence;
    result.last_overload_drop_sequence = value.last_overload_drop_sequence;
    GC_SUBTRACT_COUNTER(exact_clock_reads);
    GC_SUBTRACT_COUNTER(pending_clock_reads);
    GC_SUBTRACT_COUNTER(resolved_clock_reads);
    GC_SUBTRACT_COUNTER(unavailable_clock_reads);
    result.endpoint_publication_count = value.endpoint_publication_count;
    GC_SUBTRACT_COUNTER(outer_calls);
    GC_SUBTRACT_COUNTER(event_scopes);
    GC_SUBTRACT_COUNTER(heartbeat_scopes);
    GC_SUBTRACT_COUNTER(event_only_batches);
    GC_SUBTRACT_COUNTER(heartbeat_only_batches);
    GC_SUBTRACT_COUNTER(mixed_event_batches);
    GC_SUBTRACT_COUNTER(event_barrier_deferrals);
    GC_SUBTRACT_COUNTER(equal_boundary_substitutions);
    GC_SUBTRACT_COUNTER(committed_boundaries);
    GC_SUBTRACT_COUNTER(batches);
    result.pending_work = value.pending_work;
    GC_SUBTRACT_COUNTER(recognition_calls);
    GC_SUBTRACT_COUNTER(score_calls);
    result.queries = SubtractQueries(value.queries, baseline.queries);
    GC_SUBTRACT_COUNTER(timing_grade_calls);
    GC_SUBTRACT_COUNTER(timing_grade_records);
    GC_SUBTRACT_COUNTER(timing_grade_drops);
    GC_SUBTRACT_COUNTER(raw_message_queue_age_samples);
    result.maximum_raw_message_queue_age_ms =
        value.maximum_raw_message_queue_age_ms;
    result.score_deltas =
        SubtractScoreDeltas(value.score_deltas, baseline.score_deltas);
    result.transient_publications = SubtractTransientPublications(
        value.transient_publications,
        baseline.transient_publications);
    GC_SUBTRACT_COUNTER(scope_trace_records);
    GC_SUBTRACT_COUNTER(scope_trace_drops);
    GC_SUBTRACT_COUNTER(score_observation_read_failures);
    GC_SUBTRACT_COUNTER(score_counter_regressions);
    GC_SUBTRACT_COUNTER(transient_publication_read_failures);
    GC_SUBTRACT_COUNTER(delivery_delay_conversion_failures);
    GC_SUBTRACT_COUNTER(final_accounting_mismatches);
    GC_SUBTRACT_COUNTER(diagnostic_saturations);
#undef GC_SUBTRACT_COUNTER
    return result;
}

const char* ScopeKindName(AbsoluteJudgementScopeKind kind) noexcept {
    switch (kind) {
    case AbsoluteJudgementScopeKind::Event: return "event";
    case AbsoluteJudgementScopeKind::Heartbeat: return "heartbeat";
    }
    return "unknown";
}

const char* BatchKindName(AbsoluteJudgementBatchKind kind) noexcept {
    switch (kind) {
    case AbsoluteJudgementBatchKind::EventOnly: return "event_only";
    case AbsoluteJudgementBatchKind::HeartbeatOnly:
        return "heartbeat_only";
    }
    return "unknown";
}

const char* IsolationDispositionName(
    AbsoluteJudgementEventIsolationDisposition disposition) noexcept {
    switch (disposition) {
    case AbsoluteJudgementEventIsolationDisposition::EventEndsBatch:
        return "event_ends_batch";
    case AbsoluteJudgementEventIsolationDisposition::HeartbeatOnlyBatch:
        return "heartbeat_only_batch";
    }
    return "unknown";
}

const char* FatalReasonName(
    AbsoluteJudgementFatalReason reason) noexcept {
    switch (reason) {
    case AbsoluteJudgementFatalReason::None: return "none";
    case AbsoluteJudgementFatalReason::InputCapabilityUnavailable:
        return "input_capability_unavailable";
    case AbsoluteJudgementFatalReason::EndpointCapabilityUnavailable:
        return "endpoint_capability_unavailable";
    case AbsoluteJudgementFatalReason::NativeIdentityChanged:
        return "native_identity_changed";
    case AbsoluteJudgementFatalReason::EndpointGenerationChanged:
        return "endpoint_generation_changed";
    case AbsoluteJudgementFatalReason::InputGenerationChanged:
        return "input_generation_changed";
    case AbsoluteJudgementFatalReason::NativeStateMismatch:
        return "native_state_mismatch";
    case AbsoluteJudgementFatalReason::ClockHistoryLost:
        return "clock_history_lost";
    case AbsoluteJudgementFatalReason::ClockDiscontinuous:
        return "clock_discontinuous";
    case AbsoluteJudgementFatalReason::BackwardTime: return "backward_time";
    case AbsoluteJudgementFatalReason::SafeFrameChanged:
        return "safe_frame_changed";
    case AbsoluteJudgementFatalReason::TransportEviction:
        return "transport_eviction";
    case AbsoluteJudgementFatalReason::TransportSequenceError:
        return "transport_sequence_error";
    case AbsoluteJudgementFatalReason::TransportEpochLost:
        return "transport_epoch_lost";
    case AbsoluteJudgementFatalReason::RetainedHistoryLost:
        return "retained_history_lost";
    case AbsoluteJudgementFatalReason::CheckedArithmeticFailure:
        return "checked_arithmetic_failure";
    case AbsoluteJudgementFatalReason::CommittedOrderViolation:
        return "committed_order_violation";
    case AbsoluteJudgementFatalReason::HeartbeatFrontierViolation:
        return "heartbeat_frontier_violation";
    case AbsoluteJudgementFatalReason::ScoreCounterRegression:
        return "score_counter_regression";
    case AbsoluteJudgementFatalReason::NativeCallCountMismatch:
        return "native_call_count_mismatch";
    case AbsoluteJudgementFatalReason::ScopeThreadMismatch:
        return "scope_thread_mismatch";
    case AbsoluteJudgementFatalReason::ScopeReceiverMismatch:
        return "scope_receiver_mismatch";
    case AbsoluteJudgementFatalReason::ScopeLifetimeViolation:
        return "scope_lifetime_violation";
    }
    return "unknown";
}

struct FatalPredicateDescriptor final {
    std::string_view name;
    std::string_view expression;
    std::array<std::string_view, 8> operand_labels;
};

FatalPredicateDescriptor FatalPredicateDescriptorFor(
    const AbsoluteJudgementFatalPredicate predicate) noexcept {
    using P = AbsoluteJudgementFatalPredicate;
#define GC_FATAL_PREDICATE(name, expression, ...) \
    case P::name: return {#name, expression, {__VA_ARGS__}}
    switch (predicate) {
    GC_FATAL_PREDICATE(None, "no terminating predicate selected");
    GC_FATAL_PREDICATE(StartupSitePrefixMismatch,
        "observed native bytes do not equal the supported prefix",
        "site", "rva");
    GC_FATAL_PREDICATE(StartupHookCreateFailed,
        "hook creation returned failure", "site", "rva");
    GC_FATAL_PREDICATE(StartupHookEnableFailed,
        "hook enable returned failure", "site", "rva");
    GC_FATAL_PREDICATE(StartupHookTransactionInvalid,
        "install failure stage or site is outside its closed enum",
        "stage", "site");
    GC_FATAL_PREDICATE(GameImageAddressInvalid,
        "checked game-image address derivation or read failed",
        "base_or_address", "offset_or_rva");
    GC_FATAL_PREDICATE(GameConfigurationMissing,
        "native configuration accessor returned null");
    GC_FATAL_PREDICATE(GameConfigurationReadFailed,
        "a required native judgement configuration field was unreadable",
        "config", "field_offset");
    GC_FATAL_PREDICATE(GlobalStateMissing,
        "native global-state accessor returned null or unreadable player storage",
        "global");
    GC_FATAL_PREDICATE(GameplaySoundManagerMissing,
        "native sound-manager accessor returned null for the owned gameplay loop");
    GC_FATAL_PREDICATE(SemanticStageAlreadyOpen,
        "semantic entry observed while a stage is already open",
        "open_generation", "entry_receiver");
    GC_FATAL_PREDICATE(SemanticStageMissingAtOwnedLoop,
        "owned judgement loop observed while no semantic stage is open");
    GC_FATAL_PREDICATE(SemanticStageExitWithoutOpen,
        "semantic exit observed while no semantic stage is open",
        "exit_receiver");
    GC_FATAL_PREDICATE(SemanticStageReceiverMismatch,
        "semantic exit receiver differs from semantic entry receiver",
        "expected_receiver", "actual_receiver");
    GC_FATAL_PREDICATE(CleanupWhileSemanticStageOpen,
        "stage-owned cleanup began while a semantic stage remained open",
        "stage_generation");
    GC_FATAL_PREDICATE(StageGenerationExhausted,
        "next stage generation is zero or UINT64_MAX",
        "candidate_generation");
    GC_FATAL_PREDICATE(QueryPerformanceCounterFailed,
        "QueryPerformanceCounter returned FALSE or a nonpositive tick",
        "api_result", "qpc_ticks");
    GC_FATAL_PREDICATE(AudioBackendUnsupportedForAbsoluteJudgement,
        "absolute-time judgement is enabled with a backend other than WASAPI exclusive or ASIO",
        "configured_backend");
    GC_FATAL_PREDICATE(ExactAudioHookRouteUnavailable,
        "the audio hook required to create the configured exact output clock was not committed before judgement startup");
    GC_FATAL_PREDICATE(ExactOutputProviderMissing,
        "an owned judgement call has no exact output-clock provider for the configured backend",
        "expected_domain");
    GC_FATAL_PREDICATE(ExactOutputProviderDomainMismatch,
        "the active exact output-clock provider domain differs from the configured backend",
        "expected_domain", "actual_domain");
    GC_FATAL_PREDICATE(InputTransportRateNot1000,
        "configured input transport rate is not exactly 1000 Hz",
        "configured_rate_hz");
    GC_FATAL_PREDICATE(InputTransportInactiveAtStageEntry,
        "stage-entry cutoff reports transport disabled or inactive",
        "enabled", "active");
    GC_FATAL_PREDICATE(InputTransportWorkerBecameInactive,
        "an open stage observed its input worker inactive",
        "enabled", "active");
    GC_FATAL_PREDICATE(InputTransportEpochChanged,
        "transport epoch differs from the stage-entry epoch",
        "expected_epoch", "actual_epoch");
    GC_FATAL_PREDICATE(InputQpcFrequencyInvalidAtStageEntry,
        "stage-entry transport QPC frequency is nonpositive",
        "qpc_frequency");
    GC_FATAL_PREDICATE(InputQpcFrequencyChanged,
        "transport QPC frequency differs from stage entry",
        "expected_frequency", "actual_frequency");
    GC_FATAL_PREDICATE(InputManagerMissing,
        "native input-manager accessor returned null or unreadable storage",
        "input_manager");
    GC_FATAL_PREDICATE(BoosterMissing,
        "native input manager has no readable booster device",
        "input_manager", "booster");
    GC_FATAL_PREDICATE(TuneMissing,
        "owned judgement loop has no readable tune pointer",
        "slot", "tune");
    GC_FATAL_PREDICATE(JudgementStateMissing,
        "native judgement-state collection has no selected player entry",
        "tune", "player");
    GC_FATAL_PREDICATE(ScoreStateMissing,
        "native score-state collection has no selected player entry",
        "tune", "player");
    GC_FATAL_PREDICATE(PlayerIndexInvalid,
        "native player index is unreadable or outside the fixed two-player topology",
        "player");
    GC_FATAL_PREDICATE(TuneIdentityChanged,
        "bound tune pointer changed during one semantic stage",
        "expected_tune", "actual_tune");
    GC_FATAL_PREDICATE(JudgementStateIdentityChanged,
        "bound judgement-state pointer changed during one semantic stage",
        "expected_state", "actual_state");
    GC_FATAL_PREDICATE(ScoreStateIdentityChanged,
        "bound score-state pointer changed during one semantic stage",
        "expected_state", "actual_state");
    GC_FATAL_PREDICATE(PlayerIdentityChanged,
        "bound player index changed during one semantic stage",
        "expected_player", "actual_player");
    GC_FATAL_PREDICATE(BoosterIdentityChanged,
        "bound booster pointer changed during one semantic stage",
        "expected_booster", "actual_booster");
    GC_FATAL_PREDICATE(HoldSafeFrameNonZero,
        "HoldSafeFrame is nonzero; only native default judgement is supported",
        "value");
    GC_FATAL_PREDICATE(SlideHoldSafeFrameNonZero,
        "SlideHoldSafeFrame is nonzero; only native default judgement is supported",
        "value");
    GC_FATAL_PREDICATE(EndpointProviderMissingAtStageExit,
        "semantic stage exited without ever observing an exact endpoint provider");
    GC_FATAL_PREDICATE(StageOriginUnboundAtStageExit,
        "semantic stage exited before its absolute origin could bind");
    GC_FATAL_PREDICATE(EndpointGenerationChanged,
        "exact endpoint generation changed during one semantic stage",
        "expected_generation", "actual_generation");
    GC_FATAL_PREDICATE(EndpointProviderIdentityChanged,
        "exact endpoint provider object changed during one semantic stage",
        "expected_provider", "actual_provider");
    GC_FATAL_PREDICATE(EndpointPublicationSequenceRegressed,
        "endpoint publication sequence moved backwards",
        "previous_sequence", "current_sequence");
    GC_FATAL_PREDICATE(EndpointQpcFrequencyMismatch,
        "endpoint QPC frequency differs from input QPC frequency",
        "input_frequency", "endpoint_frequency");
    GC_FATAL_PREDICATE(EndpointProjectionDiscontinuous,
        "fixed endpoint projection cannot represent a requested QPC coordinate",
        "qpc_ticks");
    GC_FATAL_PREDICATE(StageOriginHistoryLost,
        "required Play epoch preceding stage entry is no longer retained");
    GC_FATAL_PREDICATE(PlaybackHistoryObjectChangedBeforeAnchor,
        "pending playback history provider changed before anchor binding",
        "expected_provider", "actual_provider");
    GC_FATAL_PREDICATE(PlaybackHistoryEndpointChangedBeforeAnchor,
        "pending endpoint generation changed before anchor binding",
        "expected_generation", "actual_generation");
    GC_FATAL_PREDICATE(TransportEvicted,
        "transport eviction count changed during the semantic stage",
        "entry_evictions", "current_evictions");
    GC_FATAL_PREDICATE(TransportSequenceDiscontinuous,
        "transport sequence is not the exact next promised sequence",
        "expected_sequence", "actual_sequence");
    GC_FATAL_PREDICATE(TransportMaskMismatch,
        "transport held/rising/falling masks contradict retained state",
        "expected_mask", "actual_mask");
    GC_FATAL_PREDICATE(TransportDrainContradiction,
        "transport drain result contradicts its published status",
        "drained", "requested", "depth", "next_sequence");
    GC_FATAL_PREDICATE(UnresolvedCapacityExhausted,
        "unresolved fixed-capacity queue has no free slot",
        "capacity", "pending");
    GC_FATAL_PREDICATE(HistoryCapacityExhausted,
        "judgement history fixed-capacity queue has no free slot",
        "capacity", "retained");
    GC_FATAL_PREDICATE(SequenceExhausted,
        "a sequence counter reached UINT64_MAX",
        "sequence");
    GC_FATAL_PREDICATE(RationalOperationUnrepresentable,
        "checked rational or native integer conversion is unrepresentable",
        "operation", "operand0", "operand1");
    GC_FATAL_PREDICATE(HistoryNotInitialized,
        "judgement history was queried before Reset established a baseline");
    GC_FATAL_PREDICATE(HistoryPrefixBeyondNext,
        "requested history prefix exceeds the next retained sequence",
        "prefix_end", "next_sequence");
    GC_FATAL_PREDICATE(HistoryPromisedEntryMissing,
        "history promised a retained entry but lookup did not find it",
        "sequence", "base_sequence", "next_sequence");
    GC_FATAL_PREDICATE(HistoryBaselineMaskInvalid,
        "stage-entry held baseline contains unsupported bits",
        "baseline_mask", "allowed_mask");
    GC_FATAL_PREDICATE(HistoryControlInvalid,
        "an internal history query used a control outside the native logical range",
        "control", "logical_control_count");
    GC_FATAL_PREDICATE(ResolvedCoordinateRegressed,
        "resolved timestamp/sequence coordinate is not strictly increasing",
        "previous_sequence", "current_sequence");
    GC_FATAL_PREDICATE(DeliveryOrderViolated,
        "delivery cursor or outstanding scope contradicts the selected event",
        "expected_sequence", "actual_sequence");
    GC_FATAL_PREDICATE(UnresolvedFrontEmpty,
        "unresolved front requested while unresolved size is zero");
    GC_FATAL_PREDICATE(ScopeAlreadyActive,
        "a query scope was installed while another scope was active",
        "active_thread", "installing_thread");
    GC_FATAL_PREDICATE(ScopeTlsOwnerMismatch,
        "query scope TLS/global owner differs from the installing game thread",
        "expected_thread", "actual_thread");
    GC_FATAL_PREDICATE(ScopeGenerationMismatch,
        "query hook stage generation differs from the active scope",
        "expected_generation", "actual_generation");
    GC_FATAL_PREDICATE(ScopeReceiverMismatch,
        "query hook receiver differs from the active booster",
        "expected_receiver", "actual_receiver");
    GC_FATAL_PREDICATE(ScopeLifetimeMismatch,
        "scope commit/destruction does not match the outstanding scope");
    GC_FATAL_PREDICATE(PressedFrameMismatch,
        "native pressed query requested a frame other than the active native frame",
        "expected_frame", "requested_frame");
    GC_FATAL_PREDICATE(DirectionOutputNull,
        "native direction query supplied a null x or y output",
        "x", "y");
    GC_FATAL_PREDICATE(RecognitionScoreTopologyMismatch,
        "recognition, score, and committed scope counts are not one-to-one",
        "recognition", "score", "event_scopes", "heartbeat_scopes");
    GC_FATAL_PREDICATE(CommitTopologyMismatch,
        "batch/heartbeat/commit topology contradicts scheduler ownership",
        "value0", "value1", "value2", "value3");
    GC_FATAL_PREDICATE(FatalRecordInvalid,
        "fatal emitter supplied None or an out-of-range predicate",
        "supplied_predicate");
    GC_FATAL_PREDICATE(StartupFatalPublisherReturned,
        "startup fatal publisher returned instead of terminating");
    GC_FATAL_PREDICATE(TerminateProcessReturned,
        "TerminateProcess returned to the caller",
        "return_value", "last_error");
    case P::Count:
        break;
    }
#undef GC_FATAL_PREDICATE
    return {"invalid_predicate", "predicate value is outside its closed enum", {}};
}

AbsoluteJudgementFailureClass FailureClassFor(
    const AbsoluteJudgementFatalPredicate predicate) noexcept {
    using C = AbsoluteJudgementFailureClass;
    using P = AbsoluteJudgementFatalPredicate;
    switch (predicate) {
    case P::AudioBackendUnsupportedForAbsoluteJudgement:
    case P::ExactAudioHookRouteUnavailable:
    case P::ExactOutputProviderMissing:
    case P::ExactOutputProviderDomainMismatch:
    case P::InputTransportRateNot1000:
    case P::InputTransportInactiveAtStageEntry:
    case P::InputTransportWorkerBecameInactive:
    case P::InputTransportEpochChanged:
    case P::InputQpcFrequencyInvalidAtStageEntry:
    case P::InputQpcFrequencyChanged:
    case P::InputManagerMissing:
    case P::BoosterMissing:
    case P::HoldSafeFrameNonZero:
    case P::SlideHoldSafeFrameNonZero:
    case P::EndpointProviderMissingAtStageExit:
    case P::StageOriginUnboundAtStageExit:
    case P::EndpointGenerationChanged:
    case P::EndpointProviderIdentityChanged:
    case P::EndpointQpcFrequencyMismatch:
    case P::EndpointProjectionDiscontinuous:
    case P::StageOriginHistoryLost:
    case P::PlaybackHistoryObjectChangedBeforeAnchor:
    case P::PlaybackHistoryEndpointChangedBeforeAnchor:
        return C::ExplicitlyUnsupported;
    case P::StageGenerationExhausted:
    case P::TransportEvicted:
    case P::UnresolvedCapacityExhausted:
    case P::HistoryCapacityExhausted:
    case P::SequenceExhausted:
    case P::RationalOperationUnrepresentable:
        return C::ResourceLimit;
    default:
        return C::ProvenInternalInvariant;
    }
}

std::string_view FailureClassName(
    const AbsoluteJudgementFailureClass classification) noexcept {
    switch (classification) {
    case AbsoluteJudgementFailureClass::ExplicitlyUnsupported:
        return "explicitly_unsupported";
    case AbsoluteJudgementFailureClass::ResourceLimit:
        return "resource_limit";
    case AbsoluteJudgementFailureClass::ProvenInternalInvariant:
        return "proven_internal_invariant";
    }
    return "invalid_failure_class";
}

void AppendRational(
    std::string& message,
    std::string_view name,
    const std::optional<gc::timing::CheckedRational>& value) {
    if (!value) {
        std::format_to(
            std::back_inserter(message), " {}=none", name);
        return;
    }
    std::format_to(
        std::back_inserter(message),
        " {}={}/{}",
        name,
        value->numerator(),
        value->denominator());
}

void AppendRational(
    std::string& message,
    std::string_view name,
    const gc::timing::CheckedRational& value) {
    std::format_to(
        std::back_inserter(message),
        " {}={}/{}",
        name,
        value.numerator(),
        value.denominator());
}

void AppendQueries(
    std::string& message,
    std::string_view prefix,
    const AbsoluteJudgementQueryCounters& counters) {
    std::format_to(
        std::back_inserter(message),
        " {}query_pressed_calls={} {}query_pressed_true={}"
        " {}query_held_calls={} {}query_held_true={}"
        " {}query_released_calls={} {}query_released_true={}"
        " {}query_direction_calls={} {}query_direction_nonzero={}"
        " {}query_held_age_calls={} {}query_held_age_1={}"
        " {}query_held_age_2plus={}",
        prefix,
        counters.pressed_calls,
        prefix,
        counters.pressed_true,
        prefix,
        counters.held_calls,
        prefix,
        counters.held_true,
        prefix,
        counters.released_calls,
        prefix,
        counters.released_true,
        prefix,
        counters.direction_calls,
        prefix,
        counters.direction_nonzero,
        prefix,
        counters.held_age_calls,
        prefix,
        counters.held_age_one,
        prefix,
        counters.held_age_two_plus);
}

void AppendTimingGradeObservations(
    std::string& message,
    const std::string_view prefix,
    const AbsoluteJudgementTimingGradeObservations& observations) {
    const auto recorded = (std::min)(
        observations.size, observations.records.size());
    std::format_to(
        std::back_inserter(message),
        " {}timing_grade_calls={} {}timing_grade_records={}"
        " {}timing_grade_drops={}",
        prefix,
        observations.calls,
        prefix,
        recorded,
        prefix,
        observations.drops);
    for (std::size_t index = 0; index < recorded; ++index) {
        const auto& observation = observations.records[index];
        std::format_to(
            std::back_inserter(message),
            " {}timing_begin index={} note={:#x} recognition_ms={}"
            " note_target_ms={} signed_error_ms={} native_grade={}"
            " {}timing_end",
            prefix,
            index,
            observation.note_address,
            observation.recognition_ms,
            observation.note_target_ms,
            observation.signed_error_ms,
            observation.native_grade,
            prefix);
    }
}

void AppendScoreDeltas(
    std::string& message,
    std::string_view prefix,
    const AbsoluteJudgementScoreDeltas& deltas) {
    std::format_to(
        std::back_inserter(message),
        " {}score_miss_delta={} {}score_good_delta={}"
        " {}score_cool_delta={} {}score_great_delta={}",
        prefix,
        deltas.miss,
        prefix,
        deltas.good,
        prefix,
        deltas.cool,
        prefix,
        deltas.great);
}

void AppendTransientPublicationCounts(
    std::string& message,
    std::string_view prefix,
    const AbsoluteJudgementTransientPublicationCounts& counts) {
    std::format_to(
        std::back_inserter(message),
        " {}transient_arrange={} {}transient_left_free_tap={}"
        " {}transient_right_free_tap={}",
        prefix,
        counts.arrange,
        prefix,
        counts.left_free_tap,
        prefix,
        counts.right_free_tap);
}

bool HasScoreDelta(
    const AbsoluteJudgementScoreDeltas& deltas) noexcept {
    return deltas.miss != 0 || deltas.good != 0 || deltas.cool != 0 ||
        deltas.great != 0;
}

bool HasTransientPublication(
    const AbsoluteJudgementTransientPublications& publications) noexcept {
    return publications.arrange || publications.left_free_tap ||
        publications.right_free_tap;
}

bool IsScopeTraceRelevant(
    const AbsoluteJudgementScopeRecord& record) noexcept {
    return record.kind == AbsoluteJudgementScopeKind::Event ||
        record.timing_grades.calls != 0 ||
        HasScoreDelta(record.score_deltas) ||
        HasTransientPublication(record.transient_publications);
}

void AppendScopeTraceEntry(
    std::string& message,
    const AbsoluteJudgementScopeRecord& record) {
    std::format_to(
        std::back_inserter(message),
        " entry_begin scope_id={} kind={} equal_boundary_substitution={}",
        record.scope_id,
        ScopeKindName(record.kind),
        record.equal_boundary_substitution ? 1 : 0);
    if (record.kind == AbsoluteJudgementScopeKind::Event) {
        std::format_to(
            std::back_inserter(message),
            " journal_sequence={}",
            record.journal_sequence);
    } else {
        std::format_to(
            std::back_inserter(message), " journal_sequence=none");
    }
    AppendRational(message, "mapped_j", record.mapped_j);
    std::format_to(
        std::back_inserter(message),
        " native_ms={} native_frame={}",
        record.native_ms,
        record.native_frame);
    AppendRational(message, "delivery_delay", record.delivery_delay);
    std::format_to(
        std::back_inserter(message),
        " raw_message_queue_age_available={}"
        " raw_message_queue_age_ms={}"
        " held_before={:#x} held_after={:#x} rise_mask={:#x}"
        " fall_mask={:#x}",
        record.raw_message_queue_age_available ? 1 : 0,
        record.raw_message_queue_age_ms,
        record.held_before,
        record.held_after,
        record.rising,
        record.falling);
    AppendQueries(message, "scope_", record.queries);
    AppendTimingGradeObservations(
        message, "scope_", record.timing_grades);
    AppendScoreDeltas(message, "scope_", record.score_deltas);
    std::format_to(
        std::back_inserter(message),
        " scope_transient_arrange={} scope_transient_left_free_tap={}"
        " scope_transient_right_free_tap={} boundary_committed={}"
        " committed_boundary={} remaining_backlog={} entry_end",
        record.transient_publications.arrange ? 1 : 0,
        record.transient_publications.left_free_tap ? 1 : 0,
        record.transient_publications.right_free_tap ? 1 : 0,
        record.boundary_committed ? 1 : 0,
        record.committed_boundary,
        record.remaining_backlog);
}

void AppendCounters(
    std::string& message,
    std::string_view prefix,
    const AbsoluteJudgementCounterSnapshot& counters) {
    std::format_to(
        std::back_inserter(message),
        " {}semantic_stage_opens={} {}absolute_stage_activations={}"
        " {}semantic_stage_ends={} {}transport_records_drained={}"
        " {}transport_rising_controls={} {}transport_falling_controls={}"
        " {}transport_pending={} {}transport_max_depth={}"
        " {}late_records={} {}sequence_errors={} {}post_cutoff_records={}"
        " {}overload_drops={} {}cleanup_drops={}"
        " {}first_overload_drop_sequence={}"
        " {}last_overload_drop_sequence={}",
        prefix,
        counters.semantic_stage_opens,
        prefix,
        counters.absolute_stage_activations,
        prefix,
        counters.semantic_stage_ends,
        prefix,
        counters.transport_records_drained,
        prefix,
        counters.transport_rising_controls,
        prefix,
        counters.transport_falling_controls,
        prefix,
        counters.transport_pending_depth,
        prefix,
        counters.transport_max_depth,
        prefix,
        counters.late_records,
        prefix,
        counters.sequence_errors,
        prefix,
        counters.post_cutoff_records,
        prefix,
        counters.overload_drops,
        prefix,
        counters.cleanup_drops,
        prefix,
        counters.first_overload_drop_sequence,
        prefix,
        counters.last_overload_drop_sequence);
    std::format_to(
        std::back_inserter(message),
        " {}clock_reads={} {}clock_pending={} {}clock_resolved={}"
        " {}clock_unavailable={}"
        " {}endpoint_publication_count={} {}rounded_fallback=0",
        prefix,
        counters.exact_clock_reads,
        prefix,
        counters.pending_clock_reads,
        prefix,
        counters.resolved_clock_reads,
        prefix,
        counters.unavailable_clock_reads,
        prefix,
        counters.endpoint_publication_count,
        prefix);
    std::format_to(
        std::back_inserter(message),
        " {}outer_calls={} {}event_scopes={} {}heartbeat_scopes={}"
        " {}event_only_batches={} {}heartbeat_only_batches={}"
        " {}mixed_event_batches={} {}event_barrier_deferrals={}"
        " {}equal_boundary_substitutions={} {}committed_boundaries={}"
        " {}batches={}"
        " {}maximum_batch={} {}maximum_backlog={}"
        " {}maximum_event_backlog={}"
        " {}maximum_delivery_delay_qpc={} {}pending_work={}"
        " {}recognition_calls={} {}score_calls={}",
        prefix,
        counters.outer_calls,
        prefix,
        counters.event_scopes,
        prefix,
        counters.heartbeat_scopes,
        prefix,
        counters.event_only_batches,
        prefix,
        counters.heartbeat_only_batches,
        prefix,
        counters.mixed_event_batches,
        prefix,
        counters.event_barrier_deferrals,
        prefix,
        counters.equal_boundary_substitutions,
        prefix,
        counters.committed_boundaries,
        prefix,
        counters.batches,
        prefix,
        counters.maximum_batch,
        prefix,
        counters.maximum_backlog,
        prefix,
        counters.maximum_event_backlog,
        prefix,
        counters.maximum_delivery_delay_qpc,
        prefix,
        counters.pending_work,
        prefix,
        counters.recognition_calls,
        prefix,
        counters.score_calls);
    AppendQueries(message, prefix, counters.queries);
    std::format_to(
        std::back_inserter(message),
        " {}timing_grade_calls={} {}timing_grade_records={}"
        " {}timing_grade_drops={}"
        " {}raw_message_queue_age_samples={}"
        " {}maximum_raw_message_queue_age_ms={}",
        prefix,
        counters.timing_grade_calls,
        prefix,
        counters.timing_grade_records,
        prefix,
        counters.timing_grade_drops,
        prefix,
        counters.raw_message_queue_age_samples,
        prefix,
        counters.maximum_raw_message_queue_age_ms);
    AppendScoreDeltas(message, prefix, counters.score_deltas);
    AppendTransientPublicationCounts(
        message, prefix, counters.transient_publications);
    std::format_to(
        std::back_inserter(message),
        " {}scope_trace_records={} {}scope_trace_drops={}"
        " {}score_observation_read_failures={}"
        " {}score_counter_regressions={}"
        " {}transient_publication_read_failures={}"
        " {}delivery_delay_conversion_failures={}"
        " {}final_accounting_mismatches={}"
        " {}diagnostic_saturations={}",
        prefix,
        counters.scope_trace_records,
        prefix,
        counters.scope_trace_drops,
        prefix,
        counters.score_observation_read_failures,
        prefix,
        counters.score_counter_regressions,
        prefix,
        counters.transient_publication_read_failures,
        prefix,
        counters.delivery_delay_conversion_failures,
        prefix,
        counters.final_accounting_mismatches,
        prefix,
        counters.diagnostic_saturations);
}

void AppendRuntime(
    std::string& message,
    const AbsoluteJudgementRuntimeSnapshot& runtime) {
    std::format_to(
        std::back_inserter(message),
        " last_endpoint_anchor_sequence={}",
        runtime.last_endpoint_anchor_sequence);
    if (runtime.last_endpoint_position) {
        std::format_to(
            std::back_inserter(message),
            " last_endpoint_position={}",
            *runtime.last_endpoint_position);
    } else {
        std::format_to(
            std::back_inserter(message),
            " last_endpoint_position=none");
    }
    AppendRational(message, "last_output_frame", runtime.last_output_frame);
    std::format_to(
        std::back_inserter(message), " last_qpc={}", runtime.last_qpc);
    AppendRational(message, "last_j", runtime.last_j);
    std::format_to(
        std::back_inserter(message),
        " committed_boundary={} pending_work={} last_sequence={}"
        " held_mask={} game_time_offset_ms={} hold_safe_frame={}"
        " slide_hold_safe_frame={}",
        runtime.committed_boundary,
        runtime.pending_work,
        runtime.last_sequence,
        runtime.held_mask,
        runtime.game_time_offset_ms,
        runtime.hold_safe_frame,
        runtime.slide_hold_safe_frame);
}

} // namespace

AbsoluteJudgementFatalRecord MakeAbsoluteJudgementFatalRecord(
    const AbsoluteJudgementFatalPredicate predicate,
    const std::uint64_t stage_generation,
    const AbsoluteJudgementFatalReason category,
    const std::initializer_list<std::uint64_t> operands) noexcept {
    AbsoluteJudgementFatalRecord record{
        .predicate = predicate,
        .classification = FailureClassFor(predicate),
        .stage_generation = stage_generation,
        .category = category,
    };
    for (const auto operand : operands) {
        if (record.operand_count == record.operands.size()) {
            break;
        }
        record.operands[record.operand_count++] = operand;
    }
    return record;
}

std::string_view AbsoluteJudgementFatalPredicateName(
    const AbsoluteJudgementFatalPredicate predicate) noexcept {
    return FatalPredicateDescriptorFor(predicate).name;
}

AbsoluteJudgementDiagnostics& JudgementDiagnostics() noexcept {
    static AbsoluteJudgementDiagnostics diagnostics;
    return diagnostics;
}

AbsoluteJudgementStageCounters&
AbsoluteJudgementDiagnostics::stage_counters() noexcept {
    return stage_;
}

void AbsoluteJudgementDiagnostics::ObserveTransportPendingDepth(
    std::uint64_t depth) noexcept {
    stage_.transport_pending_depth = depth;
    stage_.transport_max_depth =
        (std::max)(stage_.transport_max_depth, depth);
    interval_maxima_.transport_depth =
        (std::max)(interval_maxima_.transport_depth, depth);
}

void AbsoluteJudgementDiagnostics::RecordBatch(
    const std::uint64_t size) noexcept {
    IncrementSaturating(stage_.batches);
    stage_.maximum_batch = (std::max)(stage_.maximum_batch, size);
    interval_maxima_.batch = (std::max)(interval_maxima_.batch, size);
}

void AbsoluteJudgementDiagnostics::ObserveEventBacklog(
    const std::uint64_t depth) noexcept {
    stage_.maximum_event_backlog =
        (std::max)(stage_.maximum_event_backlog, depth);
    interval_maxima_.event_backlog =
        (std::max)(interval_maxima_.event_backlog, depth);
}

void AbsoluteJudgementDiagnostics::ObserveBacklog(
    std::uint64_t depth) noexcept {
    stage_.maximum_backlog = (std::max)(stage_.maximum_backlog, depth);
    interval_maxima_.backlog =
        (std::max)(interval_maxima_.backlog, depth);
}

void AbsoluteJudgementDiagnostics::ObserveDeliveryDelayQpc(
    std::uint64_t delay) noexcept {
    stage_.maximum_delivery_delay_qpc =
        (std::max)(stage_.maximum_delivery_delay_qpc, delay);
    interval_maxima_.delivery_delay_qpc =
        (std::max)(interval_maxima_.delivery_delay_qpc, delay);
}

void AbsoluteJudgementDiagnostics::SetPendingWork(
    std::uint64_t count) noexcept {
    stage_.pending_work = count;
}

AbsoluteJudgementCounterSnapshot
AbsoluteJudgementDiagnostics::SnapshotCounters() const noexcept {
    return {
        .semantic_stage_opens = stage_.semantic_stage_opens,
        .absolute_stage_activations = stage_.absolute_stage_activations,
        .semantic_stage_ends = stage_.semantic_stage_ends,
        .transport_records_drained = stage_.transport_records_drained,
        .transport_rising_controls = stage_.transport_rising_controls,
        .transport_falling_controls = stage_.transport_falling_controls,
        .transport_pending_depth = stage_.transport_pending_depth,
        .transport_max_depth = stage_.transport_max_depth,
        .late_records = stage_.late_records,
        .sequence_errors = stage_.sequence_errors,
        .post_cutoff_records = stage_.post_cutoff_records,
        .overload_drops = stage_.overload_drops,
        .cleanup_drops = stage_.cleanup_drops,
        .first_overload_drop_sequence =
            stage_.first_overload_drop_sequence,
        .last_overload_drop_sequence =
            stage_.last_overload_drop_sequence,
        .exact_clock_reads = stage_.exact_clock_reads,
        .pending_clock_reads = stage_.pending_clock_reads,
        .resolved_clock_reads = stage_.resolved_clock_reads,
        .unavailable_clock_reads = stage_.unavailable_clock_reads,
        .endpoint_publication_count = stage_.endpoint_publication_count,
        .outer_calls = stage_.outer_calls,
        .event_scopes = stage_.event_scopes,
        .heartbeat_scopes = stage_.heartbeat_scopes,
        .event_only_batches = stage_.event_only_batches,
        .heartbeat_only_batches = stage_.heartbeat_only_batches,
        .mixed_event_batches = stage_.mixed_event_batches,
        .event_barrier_deferrals = stage_.event_barrier_deferrals,
        .equal_boundary_substitutions =
            stage_.equal_boundary_substitutions,
        .committed_boundaries = stage_.committed_boundaries,
        .batches = stage_.batches,
        .maximum_batch = stage_.maximum_batch,
        .maximum_backlog = stage_.maximum_backlog,
        .maximum_event_backlog = stage_.maximum_event_backlog,
        .maximum_delivery_delay_qpc =
            stage_.maximum_delivery_delay_qpc,
        .pending_work = stage_.pending_work,
        .recognition_calls = stage_.recognition_calls,
        .score_calls = stage_.score_calls,
        .queries = stage_.queries,
        .timing_grade_calls = stage_.timing_grade_calls,
        .timing_grade_records = stage_.timing_grade_records,
        .timing_grade_drops = stage_.timing_grade_drops,
        .raw_message_queue_age_samples =
            stage_.raw_message_queue_age_samples,
        .maximum_raw_message_queue_age_ms =
            stage_.maximum_raw_message_queue_age_ms,
        .score_deltas = stage_.score_deltas,
        .transient_publications = stage_.transient_publications,
        .scope_trace_records = stage_.scope_trace_records,
        .scope_trace_drops = stage_.scope_trace_drops,
        .score_observation_read_failures =
            stage_.score_observation_read_failures,
        .score_counter_regressions = stage_.score_counter_regressions,
        .transient_publication_read_failures =
            stage_.transient_publication_read_failures,
        .delivery_delay_conversion_failures =
            stage_.delivery_delay_conversion_failures,
        .final_accounting_mismatches =
            stage_.final_accounting_mismatches,
        .diagnostic_saturations = stage_.diagnostic_saturations,
    };
}

AbsoluteJudgementCounterSnapshot
AbsoluteJudgementDiagnostics::SnapshotIntervalCounters(
    const AbsoluteJudgementCounterSnapshot& cumulative) const noexcept {
    auto interval = SubtractCounters(cumulative, last_summary_);
    interval.transport_max_depth = interval_maxima_.transport_depth;
    interval.maximum_batch = interval_maxima_.batch;
    interval.maximum_backlog = interval_maxima_.backlog;
    interval.maximum_event_backlog = interval_maxima_.event_backlog;
    interval.maximum_delivery_delay_qpc =
        interval_maxima_.delivery_delay_qpc;
    interval.maximum_raw_message_queue_age_ms =
        interval_maxima_.raw_message_queue_age_ms;
    return interval;
}

void AbsoluteJudgementDiagnostics::ResetIntervalMaxima() noexcept {
    interval_maxima_ = {};
}

void AbsoluteJudgementDiagnostics::ResetStageState() noexcept {
    stage_ = {};
    ResetIntervalMaxima();
    last_summary_ = {};
    last_committed_time_.reset();
    last_committed_sequence_ = 0;
    has_committed_coordinate_ = false;
    last_heartbeat_index_ = 0;
    has_heartbeat_index_ = false;
    last_native_score_ = {};
    has_native_score_ = false;
    scope_trace_size_ = 0;
    scope_trace_drops_since_flush_ = 0;
    scope_trace_window_ = 0;
    recognition_stopped_.store(false, std::memory_order_release);
    first_fatal_predicate_.store(
        AbsoluteJudgementFatalPredicate::None,
        std::memory_order_release);
    const auto now = GetTickCount64();
    next_scope_trace_tick_ms_ = now + kScopeTraceCadenceMilliseconds;
    next_summary_tick_ms_ = now + kSummaryCadenceMilliseconds;
}

void AbsoluteJudgementDiagnostics::SetStartupTargetFps(
    const std::uint32_t target_fps) noexcept {
    startup_target_fps_ = target_fps;
}

std::uint32_t AbsoluteJudgementDiagnostics::startup_target_fps()
    const noexcept {
    return startup_target_fps_;
}

void AbsoluteJudgementDiagnostics::LogStartup(
    const AbsoluteJudgementStartupRecord& record) noexcept {
    PLOG_INFO << std::format(
        "AbsoluteJudgement: startup mode={} target_fps={} input_rate_hz={}"
        " backend={} audio_hook_committed={} rounded_fallback=0 sites={}"
        " timing_grade_diagnostic_hook={}",
        record.enabled ? "absolute" : "stock",
        record.target_fps,
        record.input_rate_hz,
        record.backend,
        record.audio_hook_committed ? 1 : 0,
        record.installed_site_count,
        record.timing_grade_diagnostic_hook ? 1 : 0);
}

void AbsoluteJudgementDiagnostics::LogSemanticStageOpen(
    const AbsoluteJudgementSemanticStageOpenRecord& record) noexcept {
    ResetStageState();
    stage_.semantic_stage_opens = 1;
    PLOG_INFO << std::format(
        "AbsoluteJudgement: semantic-stage-open stage_generation={}"
        " native_manager={} input_generation={} cutoff_sequence={}"
        " first_eligible_sequence={} held_baseline={}"
        " transport_fault_baseline={} stage_entry_qpc={}"
        " stage_entry_multimedia_time_ms={}"
        " stage_entry_handoff_drops={}",
        record.loader_stage_generation,
        record.native_manager,
        record.input_generation,
        record.cutoff_sequence,
        record.first_eligible_sequence,
        record.held_baseline,
        record.transport_fault_baseline,
        record.stage_entry_qpc,
        record.stage_entry_multimedia_time_ms,
        record.stage_entry_handoff_drops);
}

void AbsoluteJudgementDiagnostics::LogAbsoluteStageActivation(
    const AbsoluteJudgementActivationRecord& record) noexcept {
    IncrementSaturating(stage_.absolute_stage_activations);
    auto message = std::format(
        "AbsoluteJudgement: absolute-stage-activation stage_generation={}"
        " native_manager={} tune={} judgement_state={} score_state={}"
        " booster={} player={} input_generation={} endpoint_generation={}"
        " provider_domain={} endpoint_qpc_frequency={}"
        " provider_output_rate={} provider_period_frames={}"
        " provider_output_latency_frames={}"
        " provider_timestamp_quantum_ns={}"
        " provider_publication_count={}"
        " buffer_instance_id={} playback_generation={} output_origin={}"
        " source_origin={} output_rate={} source_rate={}",
        record.native.stage_generation,
        record.native.native_manager,
        record.native.tune,
        record.native.judgement_state,
        record.native.score_state,
        record.native.booster,
        record.native.player,
        record.input_generation,
        record.endpoint_generation,
        record.provider_domain,
        record.endpoint_qpc_frequency,
        record.provider_output_rate,
        record.provider_period_frames,
        record.provider_output_latency_frames,
        record.provider_timestamp_quantum_ns,
        record.provider_publication_count,
        record.buffer_instance_id,
        record.playback_generation,
        record.output_origin,
        record.source_origin,
        record.output_rate,
        record.source_rate);
    AppendRational(message, "initial_j", record.initial_j);
    std::format_to(
        std::back_inserter(message),
        " committed_boundary_seed={} first_pending_boundary_index={}",
        record.committed_boundary_seed,
        record.first_pending_boundary_index);
    AppendRational(
        message,
        "first_pending_boundary_j",
        record.first_pending_boundary_j);
    if (record.first_pending_boundary_native_ms &&
        record.first_pending_boundary_native_frame) {
        std::format_to(
            std::back_inserter(message),
            " first_pending_boundary_native_ms={}"
            " first_pending_boundary_native_frame={}",
            *record.first_pending_boundary_native_ms,
            *record.first_pending_boundary_native_frame);
    } else {
        std::format_to(
            std::back_inserter(message),
            " first_pending_boundary_native_ms=none"
            " first_pending_boundary_native_frame=none");
    }
    std::format_to(
        std::back_inserter(message),
        " pending_negative_boundary_count={} game_time_offset_ms={}"
        " hold_safe_frame={} slide_hold_safe_frame={}"
        " accumulated_clock_waits={}",
        record.pending_negative_boundary_count,
        record.game_time_offset_ms,
        record.hold_safe_frame,
        record.slide_hold_safe_frame,
        record.accumulated_clock_waits);
    PLOG_INFO << message;
}

void AbsoluteJudgementDiagnostics::FlushScopeTrace(
    const std::string_view reason) noexcept {
    if (scope_trace_size_ == 0 &&
        scope_trace_drops_since_flush_ == 0) {
        return;
    }

    IncrementSaturating(scope_trace_window_);
    const auto part_count = (scope_trace_size_ +
        kScopeTraceEntriesPerLine - 1) / kScopeTraceEntriesPerLine;
    for (std::size_t part = 0; part < part_count; ++part) {
        const auto begin = part * kScopeTraceEntriesPerLine;
        const auto end = (std::min)(
            begin + kScopeTraceEntriesPerLine, scope_trace_size_);
        auto message = std::format(
            "AbsoluteJudgement: scope-trace reason={} stage_generation={}"
            " window={} part={}/{} entries_total={} entries_in_part={}"
            " dropped_since_last_flush={}",
            reason,
            scope_trace_[begin].native.stage_generation,
            scope_trace_window_,
            part + 1,
            part_count,
            scope_trace_size_,
            end - begin,
            scope_trace_drops_since_flush_);
        for (auto index = begin; index < end; ++index) {
            AppendScopeTraceEntry(message, scope_trace_[index]);
        }
        PLOG_INFO << message;
    }
    scope_trace_size_ = 0;
    scope_trace_drops_since_flush_ = 0;
}

void AbsoluteJudgementDiagnostics::LogSummary(
    std::string_view record_name,
    const AbsoluteJudgementRuntimeSnapshot& runtime,
    const AbsoluteJudgementCounterSnapshot& cumulative) noexcept {
    const auto interval = SnapshotIntervalCounters(cumulative);
    auto message = std::format("AbsoluteJudgement: {}", record_name);
    AppendRuntime(message, runtime);
    AppendCounters(message, "interval_", interval);
    AppendCounters(message, "cumulative_", cumulative);
    PLOG_INFO << message;
    last_summary_ = cumulative;
    ResetIntervalMaxima();
}

void AbsoluteJudgementDiagnostics::MaybeLogPeriodicDiagnostics(
    const AbsoluteJudgementRuntimeSnapshot& runtime) noexcept {
    const auto now = GetTickCount64();
    if (now >= next_scope_trace_tick_ms_) {
        next_scope_trace_tick_ms_ = now + kScopeTraceCadenceMilliseconds;
        FlushScopeTrace("periodic");
    }
    if (now < next_summary_tick_ms_) {
        return;
    }
    next_summary_tick_ms_ = now + kSummaryCadenceMilliseconds;
    const auto cumulative = SnapshotCounters();
    LogSummary("five-second-summary", runtime, cumulative);
}

void AbsoluteJudgementDiagnostics::LogSemanticStageEnd(
    const AbsoluteJudgementSemanticStageEndRecord& record) noexcept {
    IncrementSaturating(stage_.semantic_stage_ends);
    FlushScopeTrace("stage-end");
    const auto cumulative = SnapshotCounters();
    const auto interval = SnapshotIntervalCounters(cumulative);
    auto message = std::format(
        "AbsoluteJudgement: semantic-stage-end stage_generation={}"
        " native_manager={} activated={}",
        record.loader_stage_generation,
        record.native_manager,
        record.activated ? 1 : 0);
    AppendRuntime(message, record.runtime);
    AppendCounters(message, "interval_", interval);
    AppendCounters(message, "cumulative_", cumulative);
    PLOG_INFO << message;
    last_summary_ = cumulative;
    ResetIntervalMaxima();
}

void AbsoluteJudgementDiagnostics::ObserveScope(
    const AbsoluteJudgementScopeRecord& record) noexcept {
    const auto timing_grade_records = static_cast<std::uint64_t>(
        (std::min)(
            record.timing_grades.size,
            record.timing_grades.records.size()));
    AddSaturating(
        stage_.timing_grade_calls, record.timing_grades.calls);
    AddSaturating(stage_.timing_grade_records, timing_grade_records);
    AddSaturating(
        stage_.timing_grade_drops, record.timing_grades.drops);
    if (record.raw_message_queue_age_available) {
        IncrementSaturating(stage_.raw_message_queue_age_samples);
        stage_.maximum_raw_message_queue_age_ms = (std::max)(
            stage_.maximum_raw_message_queue_age_ms,
            static_cast<std::uint64_t>(
                record.raw_message_queue_age_ms));
        interval_maxima_.raw_message_queue_age_ms = (std::max)(
            interval_maxima_.raw_message_queue_age_ms,
            static_cast<std::uint64_t>(
                record.raw_message_queue_age_ms));
    }
    if (IsScopeTraceRelevant(record)) {
        if (scope_trace_size_ < scope_trace_.size()) {
            scope_trace_[scope_trace_size_] = record;
            ++scope_trace_size_;
            IncrementSaturating(stage_.scope_trace_records);
        } else {
            IncrementSaturating(scope_trace_drops_since_flush_);
            IncrementSaturating(stage_.scope_trace_drops);
        }
    }

    auto* logger = plog::get();
    if (logger == nullptr || !logger->checkSeverity(plog::verbose)) {
        return;
    }
    auto message = std::format(
        "AbsoluteJudgement: scope stage_generation={} native_manager={}"
        " scope_id={} kind={} equal_boundary_substitution={}"
        " journal_sequence={}",
        record.native.stage_generation,
        record.native.native_manager,
        record.scope_id,
        ScopeKindName(record.kind),
        record.equal_boundary_substitution ? 1 : 0,
        record.journal_sequence);
    AppendRational(message, "mapped_j", record.mapped_j);
    std::format_to(
        std::back_inserter(message),
        " native_ms={} native_frame={}",
        record.native_ms,
        record.native_frame);
    AppendRational(message, "delivery_delay", record.delivery_delay);
    std::format_to(
        std::back_inserter(message),
        " raw_message_queue_age_available={}"
        " raw_message_queue_age_ms={}"
        " held_before={} held_after={} rise_mask={} fall_mask={}",
        record.raw_message_queue_age_available ? 1 : 0,
        record.raw_message_queue_age_ms,
        record.held_before,
        record.held_after,
        record.rising,
        record.falling);
    AppendQueries(message, "", record.queries);
    AppendTimingGradeObservations(
        message, "", record.timing_grades);
    std::format_to(
        std::back_inserter(message),
        " recognition_completed={} score_completed={}",
        record.recognition_completed ? 1 : 0,
        record.score_completed ? 1 : 0);
    AppendScoreDeltas(message, "", record.score_deltas);
    std::format_to(
        std::back_inserter(message),
        " transient_arrange={} transient_left_free_tap={}"
        " transient_right_free_tap={} batch_kind={}"
        " isolation_disposition={} boundary_committed={}"
        " committed_boundary={} remaining_backlog={}",
        record.transient_publications.arrange ? 1 : 0,
        record.transient_publications.left_free_tap ? 1 : 0,
        record.transient_publications.right_free_tap ? 1 : 0,
        BatchKindName(record.batch_kind),
        IsolationDispositionName(record.isolation_disposition),
        record.boundary_committed ? 1 : 0,
        record.committed_boundary,
        record.remaining_backlog);
    PLOG_VERBOSE << message;
}

void AbsoluteJudgementDiagnostics::CheckNativeCallInvariantOrFatal(
    const AbsoluteJudgementFatalSnapshot& snapshot) noexcept {
    if (stage_.event_scopes >
        (std::numeric_limits<std::uint64_t>::max)() -
            stage_.heartbeat_scopes) {
        const bool first = stage_.diagnostic_saturations == 0;
        IncrementSaturating(stage_.diagnostic_saturations);
        if (first) {
            PLOG_WARNING << "AbsoluteJudgement: diagnostic anomaly="
                            "native_topology_counter_saturated";
        }
        return;
    }
    const auto scopes = stage_.event_scopes + stage_.heartbeat_scopes;
    if (stage_.recognition_calls != stage_.score_calls ||
        stage_.recognition_calls != scopes) {
        FatalActiveStage(
            MakeAbsoluteJudgementFatalRecord(
                AbsoluteJudgementFatalPredicate::
                    RecognitionScoreTopologyMismatch,
                snapshot.native.stage_generation,
                AbsoluteJudgementFatalReason::NativeCallCountMismatch,
                {stage_.recognition_calls,
                 stage_.score_calls,
                 stage_.event_scopes,
                 stage_.heartbeat_scopes}),
            snapshot);
    }
}

void AbsoluteJudgementDiagnostics::CheckCompletedBatchInvariantOrFatal(
    const AbsoluteJudgementFatalSnapshot& snapshot) noexcept {
    if (stage_.event_scopes != stage_.event_only_batches ||
        stage_.mixed_event_batches != 0) {
        FatalActiveStage(
            MakeAbsoluteJudgementFatalRecord(
                AbsoluteJudgementFatalPredicate::CommitTopologyMismatch,
                snapshot.native.stage_generation,
                AbsoluteJudgementFatalReason::NativeCallCountMismatch,
                {stage_.event_scopes,
                 stage_.event_only_batches,
                 stage_.mixed_event_batches,
                 stage_.batches}),
            snapshot);
    }
}

void AbsoluteJudgementDiagnostics::CheckFinalTransportIdentity() noexcept {
    std::uint64_t classified{};
    const auto add = [&classified](const std::uint64_t value) noexcept {
        if (value > (std::numeric_limits<std::uint64_t>::max)() -
                classified) {
            return false;
        }
        classified += value;
        return true;
    };
    if (!add(stage_.event_scopes) ||
        !add(stage_.late_records) ||
        !add(stage_.overload_drops) ||
        !add(stage_.cleanup_drops)) {
        IncrementSaturating(stage_.diagnostic_saturations);
        IncrementSaturating(stage_.final_accounting_mismatches);
        PLOG_WARNING << "AbsoluteJudgement: diagnostic anomaly="
                        "final_transport_accounting_overflow";
        return;
    }
    if (stage_.post_cutoff_records != classified) {
        IncrementSaturating(stage_.final_accounting_mismatches);
        PLOG_WARNING << std::format(
            "AbsoluteJudgement: diagnostic anomaly="
            "final_transport_accounting_mismatch post_cutoff_records={}"
            " classified_records={}",
            stage_.post_cutoff_records,
            classified);
    }
}

void AbsoluteJudgementDiagnostics::AccumulateQueryCounters(
    const AbsoluteJudgementQueryCounters& counters) noexcept {
    const auto add = [](std::uint64_t& total,
                        const std::uint64_t value) noexcept {
        AddSaturating(total, value);
    };
    add(stage_.queries.pressed_calls, counters.pressed_calls);
    add(stage_.queries.pressed_true, counters.pressed_true);
    add(stage_.queries.held_calls, counters.held_calls);
    add(stage_.queries.held_true, counters.held_true);
    add(stage_.queries.released_calls, counters.released_calls);
    add(stage_.queries.released_true, counters.released_true);
    add(stage_.queries.direction_calls, counters.direction_calls);
    add(stage_.queries.direction_nonzero, counters.direction_nonzero);
    add(stage_.queries.held_age_calls, counters.held_age_calls);
    add(stage_.queries.held_age_one, counters.held_age_one);
    add(stage_.queries.held_age_two_plus, counters.held_age_two_plus);
}

void AbsoluteJudgementDiagnostics::RecordTransientPublications(
    const AbsoluteJudgementTransientPublications& publications) noexcept {
    const auto increment = [](std::uint64_t& total,
                              const bool published) noexcept {
        if (!published) {
            return;
        }
        IncrementSaturating(total);
    };
    increment(
        stage_.transient_publications.arrange, publications.arrange);
    increment(stage_.transient_publications.left_free_tap,
              publications.left_free_tap);
    increment(stage_.transient_publications.right_free_tap,
              publications.right_free_tap);
}

void AbsoluteJudgementDiagnostics::RecordScoreObservationReadFailure()
    noexcept {
    IncrementSaturating(stage_.score_observation_read_failures);
}

void AbsoluteJudgementDiagnostics::RecordTransientPublicationReadFailure()
    noexcept {
    IncrementSaturating(stage_.transient_publication_read_failures);
}

void AbsoluteJudgementDiagnostics::RecordDeliveryDelayConversionFailure()
    noexcept {
    IncrementSaturating(stage_.delivery_delay_conversion_failures);
}

void AbsoluteJudgementDiagnostics::CheckAndRecordCommittedOrderOrFatal(
    const gc::timing::CheckedRational& time,
    std::uint64_t sequence,
    const AbsoluteJudgementFatalSnapshot& snapshot) noexcept {
    if (has_committed_coordinate_) {
        const auto order = time.Compare(*last_committed_time_);
        if (order < 0 ||
            (order == 0 && sequence <= last_committed_sequence_)) {
            FatalActiveStage(
                MakeAbsoluteJudgementFatalRecord(
                    AbsoluteJudgementFatalPredicate::
                        ResolvedCoordinateRegressed,
                    snapshot.native.stage_generation,
                    AbsoluteJudgementFatalReason::CommittedOrderViolation,
                    {last_committed_sequence_, sequence}),
                snapshot);
        }
    }
    last_committed_time_ = time;
    last_committed_sequence_ = sequence;
    has_committed_coordinate_ = true;
}

void AbsoluteJudgementDiagnostics::SeedHeartbeatIndex(
    std::int64_t index) noexcept {
    last_heartbeat_index_ = index;
    has_heartbeat_index_ = true;
}

void AbsoluteJudgementDiagnostics::CheckAndRecordHeartbeatIndexOrFatal(
    std::int64_t index,
    bool due_boundary,
    const AbsoluteJudgementFatalSnapshot& snapshot) noexcept {
    bool valid = has_heartbeat_index_;
    if (valid && due_boundary) {
        valid = last_heartbeat_index_ !=
                    (std::numeric_limits<std::int64_t>::max)() &&
                index == last_heartbeat_index_ + 1;
    } else if (valid) {
        valid = index == last_heartbeat_index_;
    }
    if (!valid) {
        FatalActiveStage(
            MakeAbsoluteJudgementFatalRecord(
                AbsoluteJudgementFatalPredicate::CommitTopologyMismatch,
                snapshot.native.stage_generation,
                AbsoluteJudgementFatalReason::HeartbeatFrontierViolation,
                {static_cast<std::uint64_t>(last_heartbeat_index_),
                 static_cast<std::uint64_t>(index),
                 due_boundary ? 1u : 0u,
                 has_heartbeat_index_ ? 1u : 0u}),
            snapshot);
    }
    last_heartbeat_index_ = index;
    has_heartbeat_index_ = true;
}

AbsoluteJudgementScoreDeltas AbsoluteJudgementDiagnostics::
ObserveNativeScoreCounters(
    const AbsoluteJudgementNativeScoreCounters& counters) noexcept {
    AbsoluteJudgementScoreDeltas deltas{};
    if (has_native_score_) {
        if (counters.miss < last_native_score_.miss ||
            counters.good < last_native_score_.good ||
            counters.cool < last_native_score_.cool ||
            counters.great < last_native_score_.great) {
            IncrementSaturating(stage_.score_counter_regressions);
            last_native_score_ = counters;
            PLOG_WARNING << "AbsoluteJudgement: diagnostic anomaly="
                            "native_score_counter_regression";
            return deltas;
        }
        deltas = {
            .miss = counters.miss - last_native_score_.miss,
            .good = counters.good - last_native_score_.good,
            .cool = counters.cool - last_native_score_.cool,
            .great = counters.great - last_native_score_.great,
        };
        const auto can_add = [](std::uint64_t value,
                                std::uint64_t delta) noexcept {
            return delta <=
                (std::numeric_limits<std::uint64_t>::max)() - value;
        };
        if (!can_add(stage_.score_deltas.miss, deltas.miss) ||
            !can_add(stage_.score_deltas.good, deltas.good) ||
            !can_add(stage_.score_deltas.cool, deltas.cool) ||
            !can_add(stage_.score_deltas.great, deltas.great)) {
            IncrementSaturating(stage_.diagnostic_saturations);
        }
        AddSaturating(stage_.score_deltas.miss, deltas.miss);
        AddSaturating(stage_.score_deltas.good, deltas.good);
        AddSaturating(stage_.score_deltas.cool, deltas.cool);
        AddSaturating(stage_.score_deltas.great, deltas.great);
    }
    last_native_score_ = counters;
    has_native_score_ = true;
    return deltas;
}

bool AbsoluteJudgementDiagnostics::recognition_stopped() const noexcept {
    return recognition_stopped_.load(std::memory_order_acquire);
}

[[noreturn]] void FatalActiveStage(
    const AbsoluteJudgementFatalRecord& supplied_record,
    const AbsoluteJudgementFatalSnapshot& snapshot) noexcept {
    auto& diagnostics = JudgementDiagnostics();
    auto record = supplied_record;
    const auto supplied_predicate_value =
        static_cast<std::uint64_t>(record.predicate);
    if (record.predicate == AbsoluteJudgementFatalPredicate::None ||
        record.predicate >= AbsoluteJudgementFatalPredicate::Count) {
        record = MakeAbsoluteJudgementFatalRecord(
            AbsoluteJudgementFatalPredicate::FatalRecordInvalid,
            snapshot.native.stage_generation,
            AbsoluteJudgementFatalReason::NativeStateMismatch,
            {supplied_predicate_value});
    }

    auto expected = AbsoluteJudgementFatalPredicate::None;
    const bool first =
        diagnostics.first_fatal_predicate_.compare_exchange_strong(
        expected,
        record.predicate,
        std::memory_order_acq_rel,
        std::memory_order_acquire);
    diagnostics.recognition_stopped_.store(true, std::memory_order_release);

    const auto terminate_after_log = []() noexcept -> void {
        SetLastError(ERROR_SUCCESS);
        const auto terminated = TerminateProcess(GetCurrentProcess(), 0xA7);
        const auto last_error = GetLastError();
        std::array<char, 512> emergency{};
        const auto result = std::format_to_n(
            emergency.data(),
            emergency.size() - 1,
            "AbsoluteJudgement: emergency-fatal predicate_id={}"
            " predicate=TerminateProcessReturned"
            " expression=TerminateProcess_returned_to_caller"
            " return_value={} last_error={}",
            static_cast<unsigned>(
                AbsoluteJudgementFatalPredicate::TerminateProcessReturned),
            terminated,
            last_error);
        const auto size = (std::min)(
            static_cast<std::size_t>(result.size), emergency.size() - 1);
        emergency[size] = '\0';
        PLOG_FATAL << std::string_view(emergency.data(), size);
        gc::session_log::FlushActiveProcessLog();
        RaiseFailFastException(nullptr, nullptr, 0);
        std::abort();
    };

    if (!first) {
        std::array<char, 768> emergency{};
        const auto descriptor = FatalPredicateDescriptorFor(record.predicate);
        const auto result = std::format_to_n(
            emergency.data(),
            emergency.size() - 1,
            "AbsoluteJudgement: concurrent-active-stage-fatal"
            " first_predicate_id={} second_predicate_id={}"
            " second_predicate={} second_expression={}"
            " stage_generation={}",
            static_cast<unsigned>(expected),
            static_cast<unsigned>(record.predicate),
            descriptor.name,
            descriptor.expression,
            record.stage_generation);
        const auto size = (std::min)(
            static_cast<std::size_t>(result.size), emergency.size() - 1);
        emergency[size] = '\0';
        PLOG_FATAL << std::string_view(emergency.data(), size);
        gc::session_log::FlushActiveProcessLog();
        terminate_after_log();
    }

    const auto counters = diagnostics.SnapshotCounters();
    const auto descriptor = FatalPredicateDescriptorFor(record.predicate);
    const auto last_j_present = snapshot.runtime.last_j.has_value();
    const auto last_j_numerator = last_j_present
        ? snapshot.runtime.last_j->numerator()
        : 0;
    const auto last_j_denominator = last_j_present
        ? snapshot.runtime.last_j->denominator()
        : 1;
    const auto output_present = snapshot.runtime.last_output_frame.has_value();
    const auto output_numerator = output_present
        ? snapshot.runtime.last_output_frame->numerator()
        : 0;
    const auto output_denominator = output_present
        ? snapshot.runtime.last_output_frame->denominator()
        : 1;
    std::array<char, 8192> message{};
    const auto formatted = std::format_to_n(
        message.data(),
        message.size() - 1,
        "AbsoluteJudgement: active-stage-fatal"
        " predicate_id={} predicate={} expression={} class={} category={}"
        " record_stage_generation={} operand_count={}"
        " operand0_label={} operand0={} operand1_label={} operand1={}"
        " operand2_label={} operand2={} operand3_label={} operand3={}"
        " operand4_label={} operand4={} operand5_label={} operand5={}"
        " operand6_label={} operand6={} operand7_label={} operand7={}"
        " mode={} target_fps={} snapshot_stage_generation={}"
        " native_manager={} tune={} judgement_state={} score_state={}"
        " booster={} player={} input_generation={} endpoint_generation={}"
        " last_anchor_sequence={} last_endpoint_position_present={}"
        " last_endpoint_position={} last_output_frame_present={}"
        " last_output_frame={}/{} last_qpc={} last_j_present={}"
        " last_j={}/{} committed_boundary={} pending_work={}"
        " last_sequence={} held_mask={} game_time_offset_ms={}"
        " hold_safe_frame={} slide_hold_safe_frame={}"
        " recognition_calls={} score_calls={} event_scopes={}"
        " heartbeat_scopes={} post_cutoff_records={} late_records={}"
        " overload_drops={} cleanup_drops={} recognition_stopped=1",
        static_cast<unsigned>(record.predicate),
        descriptor.name,
        descriptor.expression,
        FailureClassName(record.classification),
        FatalReasonName(record.category),
        record.stage_generation,
        static_cast<unsigned>(record.operand_count),
        descriptor.operand_labels[0], record.operands[0],
        descriptor.operand_labels[1], record.operands[1],
        descriptor.operand_labels[2], record.operands[2],
        descriptor.operand_labels[3], record.operands[3],
        descriptor.operand_labels[4], record.operands[4],
        descriptor.operand_labels[5], record.operands[5],
        descriptor.operand_labels[6], record.operands[6],
        descriptor.operand_labels[7], record.operands[7],
        snapshot.enabled ? "absolute" : "stock",
        diagnostics.startup_target_fps(),
        snapshot.native.stage_generation,
        snapshot.native.native_manager,
        snapshot.native.tune,
        snapshot.native.judgement_state,
        snapshot.native.score_state,
        snapshot.native.booster,
        snapshot.native.player,
        snapshot.input_generation,
        snapshot.endpoint_generation,
        snapshot.last_anchor_sequence,
        snapshot.runtime.last_endpoint_position.has_value() ? 1 : 0,
        snapshot.runtime.last_endpoint_position.value_or(0),
        output_present ? 1 : 0,
        output_numerator,
        output_denominator,
        snapshot.runtime.last_qpc,
        last_j_present ? 1 : 0,
        last_j_numerator,
        last_j_denominator,
        snapshot.runtime.committed_boundary,
        snapshot.runtime.pending_work,
        snapshot.runtime.last_sequence,
        snapshot.runtime.held_mask,
        snapshot.runtime.game_time_offset_ms,
        snapshot.runtime.hold_safe_frame,
        snapshot.runtime.slide_hold_safe_frame,
        counters.recognition_calls,
        counters.score_calls,
        counters.event_scopes,
        counters.heartbeat_scopes,
        counters.post_cutoff_records,
        counters.late_records,
        counters.overload_drops,
        counters.cleanup_drops);
    const auto message_size = (std::min)(
        static_cast<std::size_t>(formatted.size), message.size() - 1);
    message[message_size] = '\0';
    PLOG_FATAL << std::string_view(message.data(), message_size);
    if (static_cast<std::size_t>(formatted.size) >= message.size()) {
        PLOG_FATAL << "AbsoluteJudgement: fatal_record_truncated=1";
    }

    gc::session_log::FlushActiveProcessLog();
    std::array<wchar_t, 512> dialog{};
    const auto dialog_result = std::format_to_n(
        dialog.data(),
        dialog.size() - 1,
        L"GCLoader stopped absolute-time judgement because a proven "
        L"contract failed.\n\nPredicate ID: {}\nStage generation: {}\n\n"
        L"Keep loader-log.txt; it contains the exact predicate, operands, "
        L"and runtime snapshot.",
        static_cast<unsigned>(record.predicate),
        record.stage_generation);
    const auto dialog_size = (std::min)(
        static_cast<std::size_t>(dialog_result.size), dialog.size() - 1);
    dialog[dialog_size] = L'\0';
    MessageBoxW(
        nullptr,
        dialog.data(),
        L"GCLoader absolute-time judgement fatal error",
        MB_OK | MB_ICONERROR | MB_SYSTEMMODAL | MB_SETFOREGROUND);
    terminate_after_log();
}

} // namespace gc::absolute_judgement
