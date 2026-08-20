#include "Patches/AbsoluteJudgement/AbsoluteJudgementDiagnostics.h"

#include "Logging/SessionLog.h"

#include <plog/Log.h>

#include <algorithm>
#include <cstdlib>
#include <format>
#include <iterator>
#include <limits>
#include <string>

namespace gc::absolute_judgement {

namespace {

constexpr ULONGLONG kSummaryCadenceMilliseconds = 5'000;

std::uint64_t SubtractMonotonic(
    std::uint64_t value,
    std::uint64_t baseline) noexcept {
    if (value < baseline) {
        std::abort();
    }
    return value - baseline;
}

AbsoluteJudgementCounterSnapshot LoadSharedAbsolute(
    const AbsoluteJudgementSharedCounters& shared) noexcept {
    AbsoluteJudgementCounterSnapshot result{};
    result.transport_records =
        shared.transport_records.load(std::memory_order_relaxed);
    result.transport_rise_masks =
        shared.transport_rise_masks.load(std::memory_order_relaxed);
    result.transport_fall_masks =
        shared.transport_fall_masks.load(std::memory_order_relaxed);
    result.transport_evictions =
        shared.transport_evictions.load(std::memory_order_relaxed);
    result.endpoint_anchors =
        shared.endpoint_anchors.load(std::memory_order_relaxed);
    result.playback_epochs =
        shared.playback_epochs.load(std::memory_order_relaxed);
    result.playback_play_epochs =
        shared.playback_play_epochs.load(std::memory_order_relaxed);
    result.playback_seek_epochs =
        shared.playback_seek_epochs.load(std::memory_order_relaxed);
    result.history_errors =
        shared.history_errors.load(std::memory_order_relaxed);
    result.discontinuity_errors =
        shared.discontinuity_errors.load(std::memory_order_relaxed);
    return result;
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

AbsoluteJudgementCounterSnapshot SubtractCounters(
    const AbsoluteJudgementCounterSnapshot& value,
    const AbsoluteJudgementCounterSnapshot& baseline) noexcept {
    AbsoluteJudgementCounterSnapshot result{};
#define GC_SUBTRACT_COUNTER(field) \
    result.field = SubtractMonotonic(value.field, baseline.field)
    GC_SUBTRACT_COUNTER(native_stage_opens);
    GC_SUBTRACT_COUNTER(absolute_stage_activations);
    GC_SUBTRACT_COUNTER(native_stage_ends);
    GC_SUBTRACT_COUNTER(transport_records);
    GC_SUBTRACT_COUNTER(transport_rise_masks);
    GC_SUBTRACT_COUNTER(transport_fall_masks);
    result.transport_pending_depth = value.transport_pending_depth;
    GC_SUBTRACT_COUNTER(late_records);
    GC_SUBTRACT_COUNTER(outside_playback_baseline_records);
    GC_SUBTRACT_COUNTER(transport_evictions);
    GC_SUBTRACT_COUNTER(sequence_errors);
    GC_SUBTRACT_COUNTER(exact_clock_reads);
    GC_SUBTRACT_COUNTER(resolved_clock_reads);
    GC_SUBTRACT_COUNTER(unavailable_clock_reads);
    GC_SUBTRACT_COUNTER(endpoint_anchors);
    GC_SUBTRACT_COUNTER(playback_epochs);
    GC_SUBTRACT_COUNTER(playback_play_epochs);
    GC_SUBTRACT_COUNTER(playback_seek_epochs);
    GC_SUBTRACT_COUNTER(history_errors);
    GC_SUBTRACT_COUNTER(discontinuity_errors);
    GC_SUBTRACT_COUNTER(outer_calls);
    GC_SUBTRACT_COUNTER(event_scopes);
    GC_SUBTRACT_COUNTER(heartbeat_scopes);
    GC_SUBTRACT_COUNTER(equal_boundary_substitutions);
    GC_SUBTRACT_COUNTER(committed_boundaries);
    GC_SUBTRACT_COUNTER(closed_frontier_catchups);
    GC_SUBTRACT_COUNTER(batches);
    result.pending_work = value.pending_work;
    GC_SUBTRACT_COUNTER(recognition_calls);
    GC_SUBTRACT_COUNTER(score_calls);
#undef GC_SUBTRACT_COUNTER
    result.queries = SubtractQueries(value.queries, baseline.queries);
    result.score_deltas =
        SubtractScoreDeltas(value.score_deltas, baseline.score_deltas);
    return result;
}

const char* ScopeKindName(AbsoluteJudgementScopeKind kind) noexcept {
    switch (kind) {
    case AbsoluteJudgementScopeKind::Event: return "event";
    case AbsoluteJudgementScopeKind::Heartbeat: return "heartbeat";
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
    case AbsoluteJudgementFatalReason::PlaybackMappingConflict:
        return "playback_mapping_conflict";
    case AbsoluteJudgementFatalReason::BackwardTime: return "backward_time";
    case AbsoluteJudgementFatalReason::GameTimeOffsetChanged:
        return "game_time_offset_changed";
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
    case AbsoluteJudgementFatalReason::StorageAllocationFailure:
        return "storage_allocation_failure";
    case AbsoluteJudgementFatalReason::UnexpectedInternalException:
        return "unexpected_internal_exception";
    }
    return "unknown";
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

void AppendHistories(
    std::string& message,
    std::span<const AbsoluteJudgementPlaybackHistoryDiagnostic> histories) {
    std::format_to(
        std::back_inserter(message),
        " history_count={}",
        histories.size());
    for (std::size_t index = 0; index < histories.size(); ++index) {
        const auto& history = histories[index];
        std::format_to(
            std::back_inserter(message),
            " history[{}]={{buffer_id:{},endpoint_generation:{},"
            "last_playback_generation:{},play_epochs:{},seek_epochs:{},"
            "output_origin:{},source_origin:{},output_rate:{},"
            "source_rate:{}}}",
            index,
            history.buffer_instance_id,
            history.endpoint_generation,
            history.last_playback_generation,
            history.play_epoch_count,
            history.seek_epoch_count,
            history.output_origin,
            history.source_origin,
            history.output_rate,
            history.source_rate);
    }
}

void AppendCounters(
    std::string& message,
    std::string_view prefix,
    const AbsoluteJudgementCounterSnapshot& counters) {
    std::format_to(
        std::back_inserter(message),
        " {}native_stage_opens={} {}absolute_stage_activations={}"
        " {}native_stage_ends={} {}transport_records={}"
        " {}transport_rise_masks={} {}transport_fall_masks={}"
        " {}transport_pending={} {}transport_max_depth={}"
        " {}late_records={} {}outside_playback_baseline_records={}"
        " {}transport_evictions={} {}sequence_errors={}",
        prefix,
        counters.native_stage_opens,
        prefix,
        counters.absolute_stage_activations,
        prefix,
        counters.native_stage_ends,
        prefix,
        counters.transport_records,
        prefix,
        counters.transport_rise_masks,
        prefix,
        counters.transport_fall_masks,
        prefix,
        counters.transport_pending_depth,
        prefix,
        counters.transport_max_depth,
        prefix,
        counters.late_records,
        prefix,
        counters.outside_playback_baseline_records,
        prefix,
        counters.transport_evictions,
        prefix,
        counters.sequence_errors);
    std::format_to(
        std::back_inserter(message),
        " {}clock_reads={} {}clock_resolved={} {}clock_unavailable={}"
        " {}endpoint_anchors={} {}playback_epochs={}"
        " {}playback_play_epochs={} {}playback_seek_epochs={}"
        " {}history_errors={} {}discontinuity_errors={}"
        " {}rounded_fallback=0",
        prefix,
        counters.exact_clock_reads,
        prefix,
        counters.resolved_clock_reads,
        prefix,
        counters.unavailable_clock_reads,
        prefix,
        counters.endpoint_anchors,
        prefix,
        counters.playback_epochs,
        prefix,
        counters.playback_play_epochs,
        prefix,
        counters.playback_seek_epochs,
        prefix,
        counters.history_errors,
        prefix,
        counters.discontinuity_errors,
        prefix);
    std::format_to(
        std::back_inserter(message),
        " {}outer_calls={} {}event_scopes={} {}heartbeat_scopes={}"
        " {}equal_boundary_substitutions={} {}committed_boundaries={}"
        " {}closed_frontier_catchups={} {}batches={}"
        " {}maximum_batch={} {}maximum_backlog={}"
        " {}maximum_delivery_delay_qpc={} {}pending_work={}"
        " {}recognition_calls={} {}score_calls={}",
        prefix,
        counters.outer_calls,
        prefix,
        counters.event_scopes,
        prefix,
        counters.heartbeat_scopes,
        prefix,
        counters.equal_boundary_substitutions,
        prefix,
        counters.committed_boundaries,
        prefix,
        counters.closed_frontier_catchups,
        prefix,
        counters.batches,
        prefix,
        counters.maximum_batch,
        prefix,
        counters.maximum_backlog,
        prefix,
        counters.maximum_delivery_delay_qpc,
        prefix,
        counters.pending_work,
        prefix,
        counters.recognition_calls,
        prefix,
        counters.score_calls);
    AppendQueries(message, prefix, counters.queries);
    AppendScoreDeltas(message, prefix, counters.score_deltas);
}

void AppendRuntime(
    std::string& message,
    const AbsoluteJudgementRuntimeSnapshot& runtime) {
    std::format_to(
        std::back_inserter(message),
        " last_endpoint_position={}",
        runtime.last_endpoint_position);
    AppendRational(message, "last_output_frame", runtime.last_output_frame);
    AppendRational(message, "last_source_frame", runtime.last_source_frame);
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

AbsoluteJudgementDiagnostics& JudgementDiagnostics() noexcept {
    static AbsoluteJudgementDiagnostics diagnostics;
    return diagnostics;
}

AbsoluteJudgementSharedCounters&
AbsoluteJudgementDiagnostics::shared_counters() noexcept {
    return shared_;
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

void AbsoluteJudgementDiagnostics::RecordBatch(std::uint64_t size) noexcept {
    if (stage_.batches == (std::numeric_limits<std::uint64_t>::max)()) {
        std::abort();
    }
    ++stage_.batches;
    stage_.maximum_batch = (std::max)(stage_.maximum_batch, size);
    interval_maxima_.batch = (std::max)(interval_maxima_.batch, size);
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
    const auto shared = LoadSharedAbsolute(shared_);
    AbsoluteJudgementCounterSnapshot result{};
    result.native_stage_opens = stage_.native_stage_opens;
    result.absolute_stage_activations = stage_.absolute_stage_activations;
    result.native_stage_ends = stage_.native_stage_ends;
    result.transport_records = SubtractMonotonic(
        shared.transport_records, shared_stage_baseline_.transport_records);
    result.transport_rise_masks = SubtractMonotonic(
        shared.transport_rise_masks,
        shared_stage_baseline_.transport_rise_masks);
    result.transport_fall_masks = SubtractMonotonic(
        shared.transport_fall_masks,
        shared_stage_baseline_.transport_fall_masks);
    result.transport_pending_depth = stage_.transport_pending_depth;
    result.transport_max_depth = stage_.transport_max_depth;
    result.late_records = stage_.late_records;
    result.outside_playback_baseline_records =
        stage_.outside_playback_baseline_records;
    result.transport_evictions = SubtractMonotonic(
        shared.transport_evictions,
        shared_stage_baseline_.transport_evictions);
    result.sequence_errors = stage_.sequence_errors;
    result.exact_clock_reads = stage_.exact_clock_reads;
    result.resolved_clock_reads = stage_.resolved_clock_reads;
    result.unavailable_clock_reads = stage_.unavailable_clock_reads;
    result.endpoint_anchors = SubtractMonotonic(
        shared.endpoint_anchors, shared_stage_baseline_.endpoint_anchors);
    result.playback_epochs = SubtractMonotonic(
        shared.playback_epochs, shared_stage_baseline_.playback_epochs);
    result.playback_play_epochs = SubtractMonotonic(
        shared.playback_play_epochs,
        shared_stage_baseline_.playback_play_epochs);
    result.playback_seek_epochs = SubtractMonotonic(
        shared.playback_seek_epochs,
        shared_stage_baseline_.playback_seek_epochs);
    result.history_errors = SubtractMonotonic(
        shared.history_errors, shared_stage_baseline_.history_errors);
    result.discontinuity_errors = SubtractMonotonic(
        shared.discontinuity_errors,
        shared_stage_baseline_.discontinuity_errors);
    result.outer_calls = stage_.outer_calls;
    result.event_scopes = stage_.event_scopes;
    result.heartbeat_scopes = stage_.heartbeat_scopes;
    result.equal_boundary_substitutions =
        stage_.equal_boundary_substitutions;
    result.committed_boundaries = stage_.committed_boundaries;
    result.closed_frontier_catchups = stage_.closed_frontier_catchups;
    result.batches = stage_.batches;
    result.maximum_batch = stage_.maximum_batch;
    result.maximum_backlog = stage_.maximum_backlog;
    result.maximum_delivery_delay_qpc = stage_.maximum_delivery_delay_qpc;
    result.pending_work = stage_.pending_work;
    result.recognition_calls = stage_.recognition_calls;
    result.score_calls = stage_.score_calls;
    result.queries = stage_.queries;
    result.score_deltas = stage_.score_deltas;
    return result;
}

AbsoluteJudgementCounterSnapshot
AbsoluteJudgementDiagnostics::SnapshotIntervalCounters(
    const AbsoluteJudgementCounterSnapshot& cumulative) const noexcept {
    auto interval = SubtractCounters(cumulative, last_summary_);
    interval.transport_max_depth = interval_maxima_.transport_depth;
    interval.maximum_batch = interval_maxima_.batch;
    interval.maximum_backlog = interval_maxima_.backlog;
    interval.maximum_delivery_delay_qpc =
        interval_maxima_.delivery_delay_qpc;
    return interval;
}

void AbsoluteJudgementDiagnostics::ResetIntervalMaxima() noexcept {
    interval_maxima_ = {};
}

void AbsoluteJudgementDiagnostics::ResetStageState() noexcept {
    stage_ = {};
    ResetIntervalMaxima();
    shared_stage_baseline_ = LoadSharedAbsolute(shared_);
    last_summary_ = {};
    last_committed_time_.reset();
    last_committed_sequence_ = 0;
    has_committed_coordinate_ = false;
    last_heartbeat_index_ = 0;
    has_heartbeat_index_ = false;
    last_native_score_ = {};
    has_native_score_ = false;
    recognition_stopped_.store(false, std::memory_order_release);
    first_fatal_reason_.store(
        AbsoluteJudgementFatalReason::None,
        std::memory_order_release);
    next_summary_tick_ms_ = GetTickCount64() + kSummaryCadenceMilliseconds;
}

void AbsoluteJudgementDiagnostics::LogStartup(
    const AbsoluteJudgementStartupRecord& record) noexcept {
    PLOG_INFO << std::format(
        "AbsoluteJudgement: startup mode={} target_fps={} input_rate_hz={}"
        " backend={} exact_provider_capable={} rounded_fallback=0 sites={}",
        record.enabled ? "absolute" : "stock",
        record.target_fps,
        record.input_rate_hz,
        record.backend,
        record.exact_provider_capable ? 1 : 0,
        record.installed_site_count);
}

void AbsoluteJudgementDiagnostics::LogNativeStageOpen(
    const AbsoluteJudgementNativeStageOpenRecord& record) noexcept {
    ResetStageState();
    stage_.native_stage_opens = 1;
    PLOG_INFO << std::format(
        "AbsoluteJudgement: native-stage-open stage_generation={}"
        " native_manager={} input_generation={} cutoff_sequence={}"
        " first_eligible_sequence={} held_baseline={}"
        " transport_fault_baseline={}",
        record.loader_stage_generation,
        record.native_manager,
        record.input_generation,
        record.cutoff_sequence,
        record.first_eligible_sequence,
        record.held_baseline,
        record.transport_fault_baseline);
}

void AbsoluteJudgementDiagnostics::LogAbsoluteStageActivation(
    const AbsoluteJudgementActivationRecord& record) noexcept {
    ++stage_.absolute_stage_activations;
    auto message = std::format(
        "AbsoluteJudgement: absolute-stage-activation stage_generation={}"
        " native_manager={} tune={} judgement_state={} score_state={}"
        " booster={} player={} input_generation={} endpoint_generation={}",
        record.native.stage_generation,
        record.native.native_manager,
        record.native.tune,
        record.native.judgement_state,
        record.native.score_state,
        record.native.booster,
        record.native.player,
        record.input_generation,
        record.endpoint_generation);
    AppendHistories(message, record.histories);
    AppendRational(message, "initial_j", record.initial_j);
    std::format_to(
        std::back_inserter(message),
        " committed_boundary_seed={} game_time_offset_ms={}"
        " hold_safe_frame={} slide_hold_safe_frame={}"
        " accumulated_clock_waits={}",
        record.committed_boundary_seed,
        record.game_time_offset_ms,
        record.hold_safe_frame,
        record.slide_hold_safe_frame,
        record.accumulated_clock_waits);
    PLOG_INFO << message;
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

void AbsoluteJudgementDiagnostics::MaybeLogFiveSecondSummary(
    const AbsoluteJudgementRuntimeSnapshot& runtime) noexcept {
    const auto now = GetTickCount64();
    if (now < next_summary_tick_ms_) {
        return;
    }
    next_summary_tick_ms_ = now + kSummaryCadenceMilliseconds;
    const auto cumulative = SnapshotCounters();
    LogSummary("five-second-summary", runtime, cumulative);
}

void AbsoluteJudgementDiagnostics::LogNativeStageEnd(
    const AbsoluteJudgementNativeStageEndRecord& record) noexcept {
    ++stage_.native_stage_ends;
    const auto cumulative = SnapshotCounters();
    const auto interval = SnapshotIntervalCounters(cumulative);
    auto message = std::format(
        "AbsoluteJudgement: native-stage-end stage_generation={}"
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

void AbsoluteJudgementDiagnostics::LogScopeVerbose(
    const AbsoluteJudgementScopeRecord& record) noexcept {
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
        " held_before={} held_after={} rise_mask={} fall_mask={}",
        record.held_before,
        record.held_after,
        record.rising,
        record.falling);
    AppendQueries(message, "", record.queries);
    std::format_to(
        std::back_inserter(message),
        " recognition_completed={} score_completed={}",
        record.recognition_completed ? 1 : 0,
        record.score_completed ? 1 : 0);
    AppendScoreDeltas(message, "", record.score_deltas);
    std::format_to(
        std::back_inserter(message),
        " boundary_committed={} committed_boundary={} remaining_backlog={}",
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
        FatalActiveStage(
            AbsoluteJudgementFatalReason::NativeCallCountMismatch,
            snapshot);
    }
    const auto scopes = stage_.event_scopes + stage_.heartbeat_scopes;
    if (stage_.recognition_calls != stage_.score_calls ||
        stage_.recognition_calls != scopes) {
        FatalActiveStage(
            AbsoluteJudgementFatalReason::NativeCallCountMismatch,
            snapshot);
    }
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
                AbsoluteJudgementFatalReason::CommittedOrderViolation,
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
            AbsoluteJudgementFatalReason::HeartbeatFrontierViolation,
            snapshot);
    }
    last_heartbeat_index_ = index;
    has_heartbeat_index_ = true;
}

AbsoluteJudgementScoreDeltas AbsoluteJudgementDiagnostics::
CheckAndRecordNativeScoreCountersOrFatal(
    const AbsoluteJudgementNativeScoreCounters& counters,
    const AbsoluteJudgementFatalSnapshot& snapshot) noexcept {
    AbsoluteJudgementScoreDeltas deltas{};
    if (has_native_score_) {
        if (counters.miss < last_native_score_.miss ||
            counters.good < last_native_score_.good ||
            counters.cool < last_native_score_.cool ||
            counters.great < last_native_score_.great) {
            FatalActiveStage(
                AbsoluteJudgementFatalReason::ScoreCounterRegression,
                snapshot);
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
            FatalActiveStage(
                AbsoluteJudgementFatalReason::CheckedArithmeticFailure,
                snapshot);
        }
        stage_.score_deltas.miss += deltas.miss;
        stage_.score_deltas.good += deltas.good;
        stage_.score_deltas.cool += deltas.cool;
        stage_.score_deltas.great += deltas.great;
    }
    last_native_score_ = counters;
    has_native_score_ = true;
    return deltas;
}

bool AbsoluteJudgementDiagnostics::recognition_stopped() const noexcept {
    return recognition_stopped_.load(std::memory_order_acquire);
}

[[noreturn]] void FatalActiveStage(
    AbsoluteJudgementFatalReason reason,
    const AbsoluteJudgementFatalSnapshot& snapshot) noexcept {
    auto& diagnostics = JudgementDiagnostics();
    diagnostics.recognition_stopped_.store(true, std::memory_order_release);
    if (reason == AbsoluteJudgementFatalReason::None) {
        reason = AbsoluteJudgementFatalReason::UnexpectedInternalException;
    }

    auto expected = AbsoluteJudgementFatalReason::None;
    const bool first = diagnostics.first_fatal_reason_.compare_exchange_strong(
        expected,
        reason,
        std::memory_order_acq_rel,
        std::memory_order_acquire);
    if (!first) {
        static_cast<void>(
            WaitForSingleObject(GetCurrentProcess(), INFINITE));
        RaiseFailFastException(nullptr, nullptr, 0);
        std::abort();
    }

    const auto counters = diagnostics.SnapshotCounters();
    auto message = std::format(
        "AbsoluteJudgement: active-stage-fatal reason={} mode={}"
        " target_fps={} stage_generation={} native_manager={} tune={}"
        " judgement_state={} score_state={} booster={} player={}"
        " input_generation={} endpoint_generation={}"
        " last_anchor_sequence={} recognition_stopped=1",
        FatalReasonName(reason),
        snapshot.enabled ? "absolute" : "stock",
        snapshot.target_fps,
        snapshot.native.stage_generation,
        snapshot.native.native_manager,
        snapshot.native.tune,
        snapshot.native.judgement_state,
        snapshot.native.score_state,
        snapshot.native.booster,
        snapshot.native.player,
        snapshot.input_generation,
        snapshot.endpoint_generation,
        snapshot.last_anchor_sequence);
    AppendHistories(message, snapshot.histories);
    AppendRuntime(message, snapshot.runtime);
    AppendCounters(message, "", counters);
    PLOG_FATAL << message;

    gc::session_log::FlushActiveProcessLog();
    TerminateProcess(GetCurrentProcess(), 0xA7);
    RaiseFailFastException(nullptr, nullptr, 0);
    std::abort();
}

} // namespace gc::absolute_judgement
