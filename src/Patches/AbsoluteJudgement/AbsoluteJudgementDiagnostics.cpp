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
    GC_SUBTRACT_COUNTER(native_stage_opens);
    GC_SUBTRACT_COUNTER(absolute_stage_activations);
    GC_SUBTRACT_COUNTER(native_stage_ends);
    GC_SUBTRACT_COUNTER(transport_records_drained);
    GC_SUBTRACT_COUNTER(transport_rising_controls);
    GC_SUBTRACT_COUNTER(transport_falling_controls);
    result.transport_pending_depth = value.transport_pending_depth;
    GC_SUBTRACT_COUNTER(late_records);
    GC_SUBTRACT_COUNTER(outside_playback_baseline_records);
    GC_SUBTRACT_COUNTER(sequence_errors);
    GC_SUBTRACT_COUNTER(post_cutoff_records);
    GC_SUBTRACT_COUNTER(overload_drops);
    GC_SUBTRACT_COUNTER(cleanup_drops);
    result.first_overload_drop_sequence =
        value.first_overload_drop_sequence;
    result.last_overload_drop_sequence = value.last_overload_drop_sequence;
    GC_SUBTRACT_COUNTER(exact_clock_reads);
    GC_SUBTRACT_COUNTER(resolved_clock_reads);
    GC_SUBTRACT_COUNTER(unavailable_clock_reads);
    result.endpoint_publication_count = value.endpoint_publication_count;
    GC_SUBTRACT_COUNTER(endpoint_stage_publications);
    GC_SUBTRACT_COUNTER(playback_epochs);
    GC_SUBTRACT_COUNTER(playback_play_epochs);
    GC_SUBTRACT_COUNTER(playback_seek_epochs);
    GC_SUBTRACT_COUNTER(closed_frontier_selections);
    GC_SUBTRACT_COUNTER(outer_calls);
    GC_SUBTRACT_COUNTER(event_scopes);
    GC_SUBTRACT_COUNTER(heartbeat_scopes);
    GC_SUBTRACT_COUNTER(event_only_batches);
    GC_SUBTRACT_COUNTER(heartbeat_only_batches);
    GC_SUBTRACT_COUNTER(mixed_event_batches);
    GC_SUBTRACT_COUNTER(event_barrier_deferrals);
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
    result.transient_publications = SubtractTransientPublications(
        value.transient_publications,
        baseline.transient_publications);
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
        " {}native_stage_ends={} {}transport_records_drained={}"
        " {}transport_rising_controls={} {}transport_falling_controls={}"
        " {}transport_pending={} {}transport_max_depth={}"
        " {}late_records={} {}outside_playback_baseline_records={}"
        " {}sequence_errors={} {}post_cutoff_records={}"
        " {}overload_drops={} {}cleanup_drops={}"
        " {}first_overload_drop_sequence={}"
        " {}last_overload_drop_sequence={}",
        prefix,
        counters.native_stage_opens,
        prefix,
        counters.absolute_stage_activations,
        prefix,
        counters.native_stage_ends,
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
        counters.outside_playback_baseline_records,
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
        " {}clock_reads={} {}clock_resolved={} {}clock_unavailable={}"
        " {}endpoint_publication_count={}"
        " {}endpoint_stage_publications={} {}playback_epochs={}"
        " {}playback_play_epochs={} {}playback_seek_epochs={}"
        " {}closed_frontier_selections={}"
        " {}rounded_fallback=0",
        prefix,
        counters.exact_clock_reads,
        prefix,
        counters.resolved_clock_reads,
        prefix,
        counters.unavailable_clock_reads,
        prefix,
        counters.endpoint_publication_count,
        prefix,
        counters.endpoint_stage_publications,
        prefix,
        counters.playback_epochs,
        prefix,
        counters.playback_play_epochs,
        prefix,
        counters.playback_seek_epochs,
        prefix,
        counters.closed_frontier_selections,
        prefix);
    std::format_to(
        std::back_inserter(message),
        " {}outer_calls={} {}event_scopes={} {}heartbeat_scopes={}"
        " {}event_only_batches={} {}heartbeat_only_batches={}"
        " {}mixed_event_batches={} {}event_barrier_deferrals={}"
        " {}equal_boundary_substitutions={} {}committed_boundaries={}"
        " {}closed_frontier_catchups={} {}batches={}"
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
        counters.closed_frontier_catchups,
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
    AppendScoreDeltas(message, prefix, counters.score_deltas);
    AppendTransientPublicationCounts(
        message, prefix, counters.transient_publications);
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
    AppendRational(message, "last_source_frame", runtime.last_source_frame);
    std::format_to(
        std::back_inserter(message), " last_qpc={}", runtime.last_qpc);
    AppendRational(message, "last_j", runtime.last_j);
    AppendRational(
        message, "last_closed_frontier", runtime.last_closed_frontier);
    AppendRational(message, "frozen_j", runtime.frozen_j);
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
    const std::uint64_t size,
    const AbsoluteJudgementFatalSnapshot& snapshot) noexcept {
    if (stage_.batches == (std::numeric_limits<std::uint64_t>::max)()) {
        FatalActiveStage(
            AbsoluteJudgementFatalReason::CheckedArithmeticFailure,
            snapshot);
    }
    ++stage_.batches;
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
        .native_stage_opens = stage_.native_stage_opens,
        .absolute_stage_activations = stage_.absolute_stage_activations,
        .native_stage_ends = stage_.native_stage_ends,
        .transport_records_drained = stage_.transport_records_drained,
        .transport_rising_controls = stage_.transport_rising_controls,
        .transport_falling_controls = stage_.transport_falling_controls,
        .transport_pending_depth = stage_.transport_pending_depth,
        .transport_max_depth = stage_.transport_max_depth,
        .late_records = stage_.late_records,
        .outside_playback_baseline_records =
            stage_.outside_playback_baseline_records,
        .sequence_errors = stage_.sequence_errors,
        .post_cutoff_records = stage_.post_cutoff_records,
        .overload_drops = stage_.overload_drops,
        .cleanup_drops = stage_.cleanup_drops,
        .first_overload_drop_sequence =
            stage_.first_overload_drop_sequence,
        .last_overload_drop_sequence =
            stage_.last_overload_drop_sequence,
        .exact_clock_reads = stage_.exact_clock_reads,
        .resolved_clock_reads = stage_.resolved_clock_reads,
        .unavailable_clock_reads = stage_.unavailable_clock_reads,
        .endpoint_publication_count = stage_.endpoint_publication_count,
        .endpoint_stage_publications = stage_.endpoint_stage_publications,
        .playback_epochs = stage_.playback_epochs,
        .playback_play_epochs = stage_.playback_play_epochs,
        .playback_seek_epochs = stage_.playback_seek_epochs,
        .closed_frontier_selections = stage_.closed_frontier_selections,
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
        .closed_frontier_catchups = stage_.closed_frontier_catchups,
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
        .score_deltas = stage_.score_deltas,
        .transient_publications = stage_.transient_publications,
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
    recognition_stopped_.store(false, std::memory_order_release);
    first_fatal_reason_.store(
        AbsoluteJudgementFatalReason::None,
        std::memory_order_release);
    next_summary_tick_ms_ = GetTickCount64() + kSummaryCadenceMilliseconds;
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

void AbsoluteJudgementDiagnostics::CheckCompletedBatchInvariantOrFatal(
    const AbsoluteJudgementFatalSnapshot& snapshot) noexcept {
    if (stage_.event_scopes != stage_.event_only_batches ||
        stage_.mixed_event_batches != 0) {
        FatalActiveStage(
            AbsoluteJudgementFatalReason::NativeCallCountMismatch,
            snapshot);
    }
}

void AbsoluteJudgementDiagnostics::CheckFinalTransportIdentityOrFatal(
    const AbsoluteJudgementFatalSnapshot& snapshot) noexcept {
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
        !add(stage_.outside_playback_baseline_records) ||
        !add(stage_.late_records) ||
        !add(stage_.overload_drops) ||
        !add(stage_.cleanup_drops)) {
        FatalActiveStage(
            AbsoluteJudgementFatalReason::CheckedArithmeticFailure,
            snapshot);
    }
    if (stage_.post_cutoff_records != classified) {
        FatalActiveStage(
            AbsoluteJudgementFatalReason::TransportSequenceError,
            snapshot);
    }
}

void AbsoluteJudgementDiagnostics::AccumulateQueryCountersOrFatal(
    const AbsoluteJudgementQueryCounters& counters,
    const AbsoluteJudgementFatalSnapshot& snapshot) noexcept {
    const auto add = [&snapshot](std::uint64_t& total,
                                 const std::uint64_t value) noexcept {
        if (value > (std::numeric_limits<std::uint64_t>::max)() - total) {
            FatalActiveStage(
                AbsoluteJudgementFatalReason::CheckedArithmeticFailure,
                snapshot);
        }
        total += value;
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

void AbsoluteJudgementDiagnostics::RecordTransientPublicationsOrFatal(
    const AbsoluteJudgementTransientPublications& publications,
    const AbsoluteJudgementFatalSnapshot& snapshot) noexcept {
    const auto increment = [&snapshot](std::uint64_t& total,
                                       const bool published) noexcept {
        if (!published) {
            return;
        }
        if (total == (std::numeric_limits<std::uint64_t>::max)()) {
            FatalActiveStage(
                AbsoluteJudgementFatalReason::CheckedArithmeticFailure,
                snapshot);
        }
        ++total;
    };
    increment(
        stage_.transient_publications.arrange, publications.arrange);
    increment(stage_.transient_publications.left_free_tap,
              publications.left_free_tap);
    increment(stage_.transient_publications.right_free_tap,
              publications.right_free_tap);
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
