#include "Patches/AbsoluteJudgement/JudgementScheduler.h"

#include <algorithm>
#include <bit>
#include <cstdlib>
#include <limits>
#include <span>
#include <utility>

// Semantic lifecycle authority (completed audit, read-only): state 16 commits
// state 17 and reaches RVA 0x2641CC before frame-zero input/audio work. The
// taken state-18 exit predicate reaches RVA 0x264D9A immediately before state
// 19. These transitions, never object construction/cleanup or elapsed time,
// own the loader stage boundary.

namespace gc::absolute_judgement {
namespace {

using gc::timing::CheckedRational;

AbsoluteJudgementFatalReason StageErrorReason(
    const JudgementStageError error) noexcept {
    switch (error) {
    case JudgementStageError::InputCapabilityUnavailable:
        return AbsoluteJudgementFatalReason::InputCapabilityUnavailable;
    case JudgementStageError::EndpointGenerationChanged:
        return AbsoluteJudgementFatalReason::EndpointGenerationChanged;
    case JudgementStageError::InputGenerationChanged:
        return AbsoluteJudgementFatalReason::InputGenerationChanged;
    case JudgementStageError::SafeFrameChanged:
        return AbsoluteJudgementFatalReason::SafeFrameChanged;
    case JudgementStageError::GenerationExhausted:
        return AbsoluteJudgementFatalReason::CheckedArithmeticFailure;
    case JudgementStageError::AlreadyOpen:
    case JudgementStageError::NativeIdentityInvalid:
    case JudgementStageError::NativeIdentityChanged:
    case JudgementStageError::QpcFrequencyChanged:
    case JudgementStageError::CleanupIdentityChanged:
        return AbsoluteJudgementFatalReason::NativeIdentityChanged;
    }
    return AbsoluteJudgementFatalReason::NativeStateMismatch;
}

AbsoluteJudgementFatalReason HistoryErrorReason(
    const JudgementHistoryError error) noexcept {
    switch (error) {
    case JudgementHistoryError::TransportEpochMismatch:
        return AbsoluteJudgementFatalReason::TransportEpochLost;
    case JudgementHistoryError::SequenceDiscontinuity:
    case JudgementHistoryError::TransportStateMismatch:
        return AbsoluteJudgementFatalReason::TransportSequenceError;
    case JudgementHistoryError::BackwardTime:
        return AbsoluteJudgementFatalReason::BackwardTime;
    case JudgementHistoryError::HistoryLost:
        return AbsoluteJudgementFatalReason::RetainedHistoryLost;
    case JudgementHistoryError::CheckedArithmeticFailure:
        return AbsoluteJudgementFatalReason::CheckedArithmeticFailure;
    case JudgementHistoryError::NotInitialized:
    case JudgementHistoryError::InvalidControl:
        return AbsoluteJudgementFatalReason::NativeStateMismatch;
    }
    return AbsoluteJudgementFatalReason::NativeStateMismatch;
}

bool SameScope(const ScheduledJudgementScope& left,
               const ScheduledJudgementScope& right) noexcept {
    return left.kind == right.kind &&
        left.coordinate.judgement_seconds.Compare(
            right.coordinate.judgement_seconds) == 0 &&
        left.coordinate.sequence == right.coordinate.sequence &&
        left.native_ms == right.native_ms &&
        left.native_frame == right.native_frame &&
        left.event == right.event &&
        left.history_prefix_end_sequence ==
            right.history_prefix_end_sequence &&
        left.commits_boundary == right.commits_boundary;
}

} // namespace

void JudgementScheduler::BeginSemanticStage(
    const std::uintptr_t tune_manager,
    const std::int64_t stage_entry_qpc,
    const std::int32_t game_time_offset_ms,
    const std::int32_t hold_safe_frame,
    const std::int32_t slide_hold_safe_frame) noexcept {
    if (!stage_.open()) {
        ClearStageOwnedState();
    }
    const auto result = stage_.Begin(
        tune_manager,
        stage_entry_qpc,
        game_time_offset_ms,
        hold_safe_frame,
        slide_hold_safe_frame);
    if (!result) {
        Fatal(StageErrorReason(result.error()));
    }

    const auto& cutoff = stage_.cutoff();
    history_.Reset(
        cutoff.transport_epoch,
        cutoff.first_stage_sequence,
        cutoff.held_baseline);
    next_drain_sequence_ = cutoff.first_stage_sequence;
    next_delivery_sequence_ = cutoff.first_stage_sequence;
    clock_resolver_.Reset(
        stage_.generation(), stage_entry_qpc, game_time_offset_ms);
    JudgementDiagnostics().LogSemanticStageOpen({
        .loader_stage_generation = stage_.generation(),
        .native_manager = tune_manager,
        .input_generation = cutoff.transport_epoch,
        .cutoff_sequence = cutoff.first_stage_sequence,
        .first_eligible_sequence = cutoff.first_stage_sequence,
        .held_baseline = cutoff.held_baseline,
        .transport_fault_baseline = cutoff.eviction_count,
        .stage_entry_qpc = cutoff.stage_entry_qpc,
        .stage_entry_handoff_drops = cutoff.stage_entry_handoff_drops,
    });
}

void JudgementScheduler::EndSemanticStage(
    const std::uintptr_t tune_manager) noexcept {
    if (!stage_.open()) {
        Fatal(AbsoluteJudgementFatalReason::NativeStateMismatch);
    }
    if (tune_manager == 0 || tune_manager != stage_.tune_manager()) {
        Fatal(AbsoluteJudgementFatalReason::NativeIdentityChanged);
    }
    if (outstanding_scope_) {
        Fatal(AbsoluteJudgementFatalReason::ScopeLifetimeViolation);
    }

    AccountCleanupDropsOrFatal();

    JudgementDiagnostics().LogSemanticStageEnd({
        .loader_stage_generation = stage_.generation(),
        .native_manager = tune_manager,
        .activated = stage_.active(),
        .runtime = RuntimeSnapshot(),
    });
    ClearStageOwnedState();
    stage_.Reset();
}

bool JudgementScheduler::SemanticStageOpen() const noexcept {
    return stage_.open();
}

std::uint64_t JudgementScheduler::stage_generation() const noexcept {
    return stage_.generation();
}

const NativeJudgementIdentity& JudgementScheduler::native_identity()
    const noexcept {
    return stage_.native();
}

const JudgementHistory& JudgementScheduler::history() const noexcept {
    return history_;
}

std::optional<std::int64_t>
JudgementScheduler::committed_boundary_index() const noexcept {
    if (!has_committed_boundary_index_) {
        return std::nullopt;
    }
    return committed_boundary_index_;
}

void JudgementScheduler::ClearStageOwnedState() noexcept {
    clock_resolver_.Reset(0, 0, 0);
    history_.Reset(0, 0, 0);
    unresolved_read_slot_ = 0;
    unresolved_size_ = 0;
    next_drain_sequence_ = 0;
    next_delivery_sequence_ = 0;
    pending_event_count_ = 0;
    marked_overload_count_ = 0;
    accumulated_clock_waits_ = 0;
    last_resolved_coordinate_.reset();
    committed_frontier_.reset();
    committed_frontier_is_boundary_ = false;
    committed_boundary_index_ = 0;
    has_committed_boundary_index_ = false;
    outer_horizon_.reset();
    outstanding_scope_.reset();
    outer_scope_count_ = 0;
    outer_event_scope_count_ = 0;
    outer_heartbeat_scope_count_ = 0;
    outer_prepared_ = false;
    outer_event_barrier_recorded_ = false;
    last_output_frame_.reset();
    last_j_.reset();
    last_anchor_sequence_ = 0;
    last_endpoint_position_.reset();
    outer_now_qpc_ = 0;
    last_qpc_ = 0;
}

void JudgementScheduler::ValidateStageBindingOrFatal(
    const AbsoluteJudgementOuterProbe& probe) noexcept {
    const auto native = stage_.BindOrValidateNative(probe.native);
    if (!native) {
        Fatal(StageErrorReason(native.error()));
    }
    if (!probe.endpoint) {
        return;
    }
    const auto endpoint = stage_.BindEndpointOrValidate(
        probe.endpoint->endpoint_generation(),
        probe.endpoint->qpc_frequency());
    if (!endpoint) {
        Fatal(StageErrorReason(endpoint.error()));
    }
    JudgementDiagnostics().stage_counters().endpoint_publication_count =
        probe.endpoint->publication_count();
}

void JudgementScheduler::PrepareOuterCall(
    const AbsoluteJudgementOuterProbe& probe) {
    if (!stage_.open()) {
        return;
    }
    if (outer_prepared_ || outstanding_scope_) {
        Fatal(AbsoluteJudgementFatalReason::ScopeLifetimeViolation);
    }
    outer_prepared_ = true;
    outer_horizon_.reset();
    outer_scope_count_ = 0;
    outer_event_scope_count_ = 0;
    outer_heartbeat_scope_count_ = 0;
    outer_event_barrier_recorded_ = false;
    outer_now_qpc_ = probe.now_qpc;
    last_qpc_ = probe.now_qpc;
    IncrementOrFatal(JudgementDiagnostics().stage_counters().outer_calls);

    ValidateStageBindingOrFatal(probe);
    DrainTransportOrFatal();

    JudgementClockResult entry_clock{
        .status = JudgementClockStatus::Pending,
    };
    if (!clock_resolver_.bound()) {
        if (probe.group2_cursor_selected && !probe.group2_observation) {
            Fatal(AbsoluteJudgementFatalReason::NativeStateMismatch);
        }
        if (probe.group2_cursor_selected && probe.group2_observation &&
            probe.endpoint) {
            entry_clock = clock_resolver_.TryBind(
                *probe.group2_observation,
                probe.endpoint,
                left_epoch_scratch_);
            if (entry_clock.status ==
                    JudgementClockStatus::CheckedArithmeticFailure ||
                entry_clock.status ==
                    JudgementClockStatus::HistoryLostBeforeBinding ||
                entry_clock.status ==
                    JudgementClockStatus::UnsupportedContinuity) {
                FailForClockResult(entry_clock);
            }
        }
    } else {
        if (probe.endpoint &&
            (probe.endpoint->endpoint_generation() !=
                 clock_resolver_.anchor().endpoint_generation ||
             probe.endpoint.get() !=
                 clock_resolver_.anchor().endpoint.get())) {
            Fatal(AbsoluteJudgementFatalReason::EndpointGenerationChanged);
        }
        entry_clock = clock_resolver_.ResolveQpc(
            stage_.cutoff().stage_entry_qpc);
    }

    TryActivateOrWait(entry_clock);
    if (!clock_resolver_.bound() || !stage_.active()) {
        auto& diagnostics = JudgementDiagnostics();
        diagnostics.ObserveBacklog(PendingWorkCount());
        diagnostics.SetPendingWork(PendingWorkCount());
        return;
    }

    const auto unresolved_status = ResolveUnresolvedPrefixOrFatal();
    if (unresolved_status != JudgementClockStatus::Resolved) {
        auto& diagnostics = JudgementDiagnostics();
        diagnostics.ObserveBacklog(PendingWorkCount());
        diagnostics.SetPendingWork(PendingWorkCount());
        return;
    }

    SelectOuterHorizonOrFatal(probe);
    auto& diagnostics = JudgementDiagnostics();
    diagnostics.ObserveBacklog(PendingWorkCount());
    diagnostics.SetPendingWork(PendingWorkCount());
}

void JudgementScheduler::DrainTransportOrFatal() noexcept {
    auto& diagnostics = JudgementDiagnostics();
    for (;;) {
        if (unresolved_size_ > unresolved_.size()) {
            Fatal(AbsoluteJudgementFatalReason::RetainedHistoryLost);
        }
        const auto free_capacity = unresolved_.size() - unresolved_size_;
        const auto requested = (std::min)(
            free_capacity, drain_batch_.size());
        gc::input::GameplayTransitionStatus status{};
        const auto count = gc::input::DrainGameplayTransitions(
            std::span<gc::input::GameplayTransitionRecord>(
                drain_batch_.data(), requested),
            &status);
        if (count > requested ||
            count > (std::numeric_limits<std::uint64_t>::max)() -
                static_cast<std::uint64_t>(status.depth)) {
            Fatal(AbsoluteJudgementFatalReason::TransportSequenceError);
        }
        diagnostics.ObserveTransportPendingDepth(
            static_cast<std::uint64_t>(count) + status.depth);
        if (!status.enabled || !status.active ||
            status.transport_epoch != stage_.cutoff().transport_epoch ||
            status.qpc_frequency != stage_.cutoff().qpc_frequency) {
            Fatal(AbsoluteJudgementFatalReason::TransportEpochLost);
        }
        if (status.eviction_count != stage_.cutoff().eviction_count) {
            Fatal(AbsoluteJudgementFatalReason::TransportEviction);
        }
        if (status.next_sequence < status.depth) {
            Fatal(AbsoluteJudgementFatalReason::TransportSequenceError);
        }

        for (std::size_t index = 0; index < count; ++index) {
            const auto& record = drain_batch_[index];
            if (record.transport_epoch != stage_.cutoff().transport_epoch ||
                record.sequence != next_drain_sequence_ ||
                record.sequence ==
                    (std::numeric_limits<std::uint64_t>::max)()) {
                IncrementOrFatal(
                    diagnostics.stage_counters().sequence_errors);
                Fatal(AbsoluteJudgementFatalReason::TransportSequenceError);
            }
            IncrementOrFatal(
                diagnostics.stage_counters().transport_records_drained);
            const auto rising = static_cast<std::uint64_t>(
                std::popcount(record.rising));
            const auto falling = static_cast<std::uint64_t>(
                std::popcount(record.falling));
            auto& counters = diagnostics.stage_counters();
            if (rising > (std::numeric_limits<std::uint64_t>::max)() -
                    counters.transport_rising_controls ||
                falling > (std::numeric_limits<std::uint64_t>::max)() -
                    counters.transport_falling_controls) {
                Fatal(
                    AbsoluteJudgementFatalReason::CheckedArithmeticFailure);
            }
            counters.transport_rising_controls += rising;
            counters.transport_falling_controls += falling;
            AppendUnresolvedOrFatal(record);
            ++next_drain_sequence_;
        }
        const auto first_remaining_sequence =
            status.next_sequence - status.depth;
        if (first_remaining_sequence != next_drain_sequence_) {
            IncrementOrFatal(
                diagnostics.stage_counters().sequence_errors);
            Fatal(AbsoluteJudgementFatalReason::TransportSequenceError);
        }
        diagnostics.ObserveTransportPendingDepth(status.depth);
        if (status.depth != 0 && unresolved_size_ == unresolved_.size()) {
            Fatal(AbsoluteJudgementFatalReason::RetainedHistoryLost);
        }
        if (status.depth == 0) {
            break;
        }
        if (count == 0) {
            Fatal(AbsoluteJudgementFatalReason::TransportSequenceError);
        }
    }
}

void JudgementScheduler::AccountCleanupDropsOrFatal() noexcept {
    gc::input::GameplayTransitionCutoff cutoff{};
    if (!gc::input::CaptureGameplayTransitionCutoff(
            stage_.cutoff().stage_entry_qpc, &cutoff)) {
        Fatal(AbsoluteJudgementFatalReason::TransportEpochLost);
    }
    const auto& stage_cutoff = stage_.cutoff();
    if (cutoff.transport_epoch != stage_cutoff.transport_epoch ||
        cutoff.qpc_frequency != stage_cutoff.qpc_frequency) {
        Fatal(AbsoluteJudgementFatalReason::TransportEpochLost);
    }
    if (cutoff.eviction_count != stage_cutoff.eviction_count) {
        Fatal(AbsoluteJudgementFatalReason::TransportEviction);
    }
    if (cutoff.first_stage_sequence <
        stage_cutoff.first_stage_sequence) {
        Fatal(AbsoluteJudgementFatalReason::TransportSequenceError);
    }

    auto& diagnostics = JudgementDiagnostics();
    auto& counters = diagnostics.stage_counters();
    counters.post_cutoff_records = cutoff.first_stage_sequence -
        stage_cutoff.first_stage_sequence;

    std::uint64_t already_classified{};
    const auto add_classified = [this, &already_classified](
                                    const std::uint64_t value) noexcept {
        if (value > (std::numeric_limits<std::uint64_t>::max)() -
                already_classified) {
            Fatal(AbsoluteJudgementFatalReason::CheckedArithmeticFailure);
        }
        already_classified += value;
    };
    add_classified(counters.event_scopes);
    add_classified(counters.late_records);
    add_classified(counters.overload_drops);
    if (counters.post_cutoff_records < already_classified) {
        Fatal(AbsoluteJudgementFatalReason::TransportSequenceError);
    }
    counters.cleanup_drops =
        counters.post_cutoff_records - already_classified;

    unresolved_read_slot_ = 0;
    unresolved_size_ = 0;
    pending_event_count_ = 0;
    marked_overload_count_ = 0;
    next_drain_sequence_ = cutoff.first_stage_sequence;
    next_delivery_sequence_ = cutoff.first_stage_sequence;
    history_.Reset(
        cutoff.transport_epoch,
        cutoff.first_stage_sequence,
        cutoff.held_baseline);
    diagnostics.ObserveTransportPendingDepth(0);
    diagnostics.SetPendingWork(0);
    diagnostics.CheckFinalTransportIdentityOrFatal(FatalSnapshot());
}

JudgementClockStatus
JudgementScheduler::ResolveUnresolvedPrefixOrFatal() noexcept {
    if (!clock_resolver_.bound()) {
        return JudgementClockStatus::Pending;
    }

    auto& counters = JudgementDiagnostics().stage_counters();
    while (unresolved_size_ != 0) {
        const auto& record = UnresolvedFront();
        IncrementOrFatal(counters.exact_clock_reads);
        const auto resolved = clock_resolver_.ResolveQpc(record.qpc_ticks);
        last_qpc_ = record.qpc_ticks;
        last_output_frame_ = resolved.output_frame;
        last_j_ = resolved.judgement_seconds;
        if (resolved.endpoint_anchor_sequence != 0) {
            last_anchor_sequence_ = resolved.endpoint_anchor_sequence;
        }
        if (resolved.endpoint_position) {
            last_endpoint_position_ = resolved.endpoint_position;
        }
        if (resolved.status == JudgementClockStatus::Pending) {
            return JudgementClockStatus::Pending;
        }
        if (resolved.status ==
            JudgementClockStatus::TemporarilyUnavailable) {
            IncrementOrFatal(counters.unavailable_clock_reads);
            return JudgementClockStatus::TemporarilyUnavailable;
        }
        if (resolved.status != JudgementClockStatus::Resolved ||
            !resolved.judgement_seconds) {
            FailForClockResult(resolved);
        }

        IncrementOrFatal(counters.resolved_clock_reads);
        const JudgementScopeCoordinate coordinate{
            .judgement_seconds = *resolved.judgement_seconds,
            .sequence = record.sequence,
        };
        if (last_resolved_coordinate_) {
            const auto order = coordinate.judgement_seconds.Compare(
                last_resolved_coordinate_->judgement_seconds);
            if (order < 0 ||
                (order == 0 &&
                 coordinate.sequence <= last_resolved_coordinate_->sequence)) {
                Fatal(AbsoluteJudgementFatalReason::BackwardTime);
            }
        }
        last_resolved_coordinate_ = coordinate;

        if (IsBehindCommittedFrontier(coordinate)) {
            if (next_delivery_sequence_ != record.sequence) {
                Fatal(AbsoluteJudgementFatalReason::CommittedOrderViolation);
            }
            ApplyHistoryResultOrFatal(history_.AppendStateOnly(
                {
                    .transport = record,
                    .judgement_seconds = *resolved.judgement_seconds,
                },
                StateOnlyReason::AcceptedLate));
            next_delivery_sequence_ = record.sequence + 1;
            IncrementOrFatal(counters.late_records);
        } else {
            ApplyHistoryResultOrFatal(history_.Append({
                .transport = record,
                .judgement_seconds = *resolved.judgement_seconds,
            }));
            IncrementOrFatal(pending_event_count_);
            JudgementDiagnostics().ObserveEventBacklog(
                pending_event_count_);
        }
        PopUnresolved();
    }
    return JudgementClockStatus::Resolved;
}

void JudgementScheduler::TryActivateOrWait(
    const JudgementClockResult& entry_clock) noexcept {
    if (stage_.active()) {
        return;
    }
    if (entry_clock.status == JudgementClockStatus::Pending ||
        entry_clock.status ==
            JudgementClockStatus::TemporarilyUnavailable) {
        IncrementOrFatal(accumulated_clock_waits_);
        return;
    }
    if (entry_clock.status != JudgementClockStatus::Resolved ||
        !entry_clock.judgement_seconds || !clock_resolver_.bound()) {
        FailForClockResult(entry_clock);
    }

    const auto scaled = entry_clock.judgement_seconds->Multiply(60, 1);
    const auto ceiling = scaled ? scaled->Ceil() :
        std::expected<std::int64_t, gc::timing::RationalError>(
            std::unexpected(gc::timing::RationalError::Overflow));
    if (!scaled || !ceiling ||
        *ceiling == (std::numeric_limits<std::int64_t>::min)()) {
        Fatal(AbsoluteJudgementFatalReason::CheckedArithmeticFailure);
    }
    committed_boundary_index_ = *ceiling - 1;
    has_committed_boundary_index_ = true;
    stage_.Activate();
    if (!stage_.active()) {
        Fatal(AbsoluteJudgementFatalReason::NativeStateMismatch);
    }
    last_j_ = entry_clock.judgement_seconds;
    JudgementDiagnostics().SeedHeartbeatIndex(committed_boundary_index_);
    const auto& anchor = clock_resolver_.anchor();
    JudgementDiagnostics().LogAbsoluteStageActivation({
        .native = {
            .stage_generation = stage_.native().stage_generation,
            .native_manager = stage_.native().tune_manager,
            .tune = stage_.native().tune,
            .judgement_state = stage_.native().judgement_state,
            .score_state = stage_.native().score_state,
            .booster = stage_.native().booster,
            .player = stage_.native().player,
        },
        .input_generation = stage_.cutoff().transport_epoch,
        .endpoint_generation = stage_.endpoint_generation(),
        .buffer_instance_id = anchor.buffer_instance_id,
        .playback_generation = anchor.playback_generation,
        .output_origin = anchor.output_origin,
        .source_origin = anchor.source_origin,
        .output_rate = anchor.output_rate,
        .source_rate = anchor.source_rate,
        .initial_j = *entry_clock.judgement_seconds,
        .committed_boundary_seed = committed_boundary_index_,
        .game_time_offset_ms = anchor.game_time_offset_ms,
        .hold_safe_frame = stage_.native().hold_safe_frame,
        .slide_hold_safe_frame = stage_.native().slide_hold_safe_frame,
        .accumulated_clock_waits = accumulated_clock_waits_,
    });
}

void JudgementScheduler::SelectOuterHorizonOrFatal(
    const AbsoluteJudgementOuterProbe& probe) noexcept {
    if (!stage_.active()) {
        return;
    }

    auto& counters = JudgementDiagnostics().stage_counters();
    IncrementOrFatal(counters.exact_clock_reads);
    last_qpc_ = probe.now_qpc;
    const auto current = clock_resolver_.ResolveQpc(probe.now_qpc);
    last_output_frame_ = current.output_frame;
    last_j_ = current.judgement_seconds;
    if (current.endpoint_anchor_sequence != 0) {
        last_anchor_sequence_ = current.endpoint_anchor_sequence;
    }
    if (current.endpoint_position) {
        last_endpoint_position_ = current.endpoint_position;
    }
    if (current.status ==
        JudgementClockStatus::TemporarilyUnavailable) {
        IncrementOrFatal(counters.unavailable_clock_reads);
        return;
    }
    if (current.status == JudgementClockStatus::Pending) {
        return;
    }
    if (current.status != JudgementClockStatus::Resolved ||
        !current.judgement_seconds) {
        FailForClockResult(current);
    }

    IncrementOrFatal(counters.resolved_clock_reads);
    SetReadyHorizonOrFatal(*current.judgement_seconds);
}

void JudgementScheduler::SetReadyHorizonOrFatal(
    const CheckedRational& ready) noexcept {
    if (committed_frontier_ &&
        ready.Compare(committed_frontier_->judgement_seconds) < 0) {
        Fatal(AbsoluteJudgementFatalReason::ClockDiscontinuous);
    }
    if (!has_committed_boundary_index_) {
        Fatal(AbsoluteJudgementFatalReason::HeartbeatFrontierViolation);
    }
    MarkReadyOverloadOrFatal(ready);
    const auto scaled = ready.Multiply(60, 1);
    const auto target = scaled ? scaled->Floor() :
        std::expected<std::int64_t, gc::timing::RationalError>(
            std::unexpected(gc::timing::RationalError::Overflow));
    if (!scaled || !target) {
        Fatal(AbsoluteJudgementFatalReason::CheckedArithmeticFailure);
    }

    bool capped{};
    if (*target > committed_boundary_index_) {
        if (committed_boundary_index_ >
            (std::numeric_limits<std::int64_t>::max)() - 3) {
            Fatal(AbsoluteJudgementFatalReason::CheckedArithmeticFailure);
        }
        capped = *target > committed_boundary_index_ + 3;
    }
    if (capped) {
        outer_horizon_ = BoundaryAt(committed_boundary_index_ + 3);
        if (!outer_horizon_) {
            Fatal(AbsoluteJudgementFatalReason::CheckedArithmeticFailure);
        }
    } else {
        outer_horizon_ = ready;
    }
}

void JudgementScheduler::MarkReadyOverloadOrFatal(
    const CheckedRational& ready) noexcept {
    const auto ready_count = history_.CountResolvedAtOrBefore(
        next_delivery_sequence_, ready);
    if (!ready_count) {
        Fatal(HistoryErrorReason(ready_count.error()));
    }
    const auto required_marked = *ready_count > kProtectedReadyEventCount
        ? *ready_count - kProtectedReadyEventCount
        : 0;
    marked_overload_count_ = required_marked;
}

void JudgementScheduler::ConsumeMarkedOverloadOrFatal(
    const ResolvedGameplayTransition& event) noexcept {
    if (marked_overload_count_ == 0 || pending_event_count_ == 0 ||
        event.transport.sequence ==
            (std::numeric_limits<std::uint64_t>::max)()) {
        Fatal(AbsoluteJudgementFatalReason::CommittedOrderViolation);
    }
    ApplyHistoryResultOrFatal(history_.ConvertResolvedToStateOnly(
        event.transport.sequence, StateOnlyReason::Overload));
    next_delivery_sequence_ = event.transport.sequence + 1;
    --pending_event_count_;
    --marked_overload_count_;

    auto& counters = JudgementDiagnostics().stage_counters();
    const bool first_drop = counters.overload_drops == 0;
    IncrementOrFatal(counters.overload_drops);
    if (first_drop) {
        counters.first_overload_drop_sequence = event.transport.sequence;
    }
    counters.last_overload_drop_sequence = event.transport.sequence;
    JudgementDiagnostics().SetPendingWork(PendingWorkCount());
}


std::optional<ScheduledJudgementScope>
JudgementScheduler::NextScope() noexcept {
    if (!outer_prepared_ || !stage_.active() || !outer_horizon_) {
        return std::nullopt;
    }
    if (outstanding_scope_) {
        return outstanding_scope_;
    }
    if (outer_event_scope_count_ != 0 ||
        outer_heartbeat_scope_count_ >= 3) {
        return std::nullopt;
    }
    if (!has_committed_boundary_index_ ||
        committed_boundary_index_ ==
            (std::numeric_limits<std::int64_t>::max)()) {
        Fatal(AbsoluteJudgementFatalReason::HeartbeatFrontierViolation);
    }

    for (;;) {
        const auto boundary_index = committed_boundary_index_ + 1;
        const auto boundary = BoundaryAt(boundary_index);
        if (!boundary) {
            Fatal(AbsoluteJudgementFatalReason::CheckedArithmeticFailure);
        }
        const auto* event = history_.FirstResolvedAtOrAfter(
            next_delivery_sequence_);
        const bool event_due = event != nullptr &&
            event->judgement_seconds.Compare(*outer_horizon_) <= 0;
        const bool boundary_due = boundary->Compare(*outer_horizon_) <= 0;
        if (!event_due && !boundary_due) {
            return std::nullopt;
        }

        const bool event_is_next = event_due &&
            (!boundary_due ||
             event->judgement_seconds.Compare(*boundary) <= 0);
        if (event_is_next && marked_overload_count_ != 0) {
            ConsumeMarkedOverloadOrFatal(*event);
            continue;
        }
        if (event_is_next && outer_heartbeat_scope_count_ != 0) {
            if (!outer_event_barrier_recorded_) {
                IncrementOrFatal(JudgementDiagnostics().stage_counters()
                                     .event_barrier_deferrals);
                outer_event_barrier_recorded_ = true;
            }
            return std::nullopt;
        }

        std::optional<ScheduledJudgementScope> result = event_is_next
            ? MakeEventScope(*event, *boundary)
            : MakeHeartbeatScope(*boundary);
        if (!result) {
            Fatal(AbsoluteJudgementFatalReason::CheckedArithmeticFailure);
        }
        outstanding_scope_ = *result;
        return result;
    }
}

std::optional<ScheduledJudgementScope>
JudgementScheduler::MakeEventScope(
    const ResolvedGameplayTransition& event,
    const CheckedRational& boundary) noexcept {
    if (event.transport.sequence ==
        (std::numeric_limits<std::uint64_t>::max)()) {
        return std::nullopt;
    }
    const auto native = NativeArguments(event.judgement_seconds);
    if (!native) {
        return std::nullopt;
    }
    bool commits_boundary{};
    if (event.judgement_seconds.Compare(boundary) == 0) {
        const auto* next = history_.FirstResolvedAtOrAfter(
            event.transport.sequence + 1);
        commits_boundary = next == nullptr ||
            next->judgement_seconds.Compare(boundary) != 0;
    }
    return ScheduledJudgementScope{
        .kind = JudgementScopeKind::Event,
        .coordinate = {
            .judgement_seconds = event.judgement_seconds,
            .sequence = event.transport.sequence,
        },
        .native_ms = native->first,
        .native_frame = native->second,
        .event = &event,
        .history_prefix_end_sequence = event.transport.sequence + 1,
        .commits_boundary = commits_boundary,
    };
}

std::optional<ScheduledJudgementScope>
JudgementScheduler::MakeHeartbeatScope(
    const CheckedRational& boundary) noexcept {
    const auto native = NativeArguments(boundary);
    if (!native) {
        return std::nullopt;
    }
    return ScheduledJudgementScope{
        .kind = JudgementScopeKind::Heartbeat,
        .coordinate = {
            .judgement_seconds = boundary,
            .sequence = (std::numeric_limits<std::uint64_t>::max)(),
        },
        .native_ms = native->first,
        .native_frame = native->second,
        .history_prefix_end_sequence = CurrentHistoryPrefixEnd(),
        .commits_boundary = true,
    };
}

void JudgementScheduler::CommitScope(
    const ScheduledJudgementScope& scope) noexcept {
    if (!outer_prepared_ || !outstanding_scope_ ||
        !SameScope(scope, *outstanding_scope_)) {
        Fatal(AbsoluteJudgementFatalReason::CommittedOrderViolation);
    }

    auto& diagnostics = JudgementDiagnostics();
    auto& counters = diagnostics.stage_counters();
    if (scope.kind == JudgementScopeKind::Event) {
        if (scope.event == nullptr || pending_event_count_ == 0 ||
            scope.event->transport.sequence != scope.coordinate.sequence ||
            scope.event->transport.sequence ==
                (std::numeric_limits<std::uint64_t>::max)()) {
            Fatal(AbsoluteJudgementFatalReason::CommittedOrderViolation);
        }
        next_delivery_sequence_ = scope.event->transport.sequence + 1;
        --pending_event_count_;
        IncrementOrFatal(counters.event_scopes);
        IncrementOrFatal(outer_event_scope_count_);
        if (outer_now_qpc_ >= scope.event->transport.qpc_ticks) {
            diagnostics.ObserveDeliveryDelayQpc(
                static_cast<std::uint64_t>(
                    outer_now_qpc_ - scope.event->transport.qpc_ticks));
        }
    } else {
        IncrementOrFatal(counters.heartbeat_scopes);
        IncrementOrFatal(outer_heartbeat_scope_count_);
    }

    diagnostics.CheckAndRecordCommittedOrderOrFatal(
        scope.coordinate.judgement_seconds,
        scope.coordinate.sequence,
        FatalSnapshot());
    committed_frontier_ = scope.coordinate;
    committed_frontier_is_boundary_ = scope.commits_boundary;

    if (scope.commits_boundary) {
        if (committed_boundary_index_ ==
            (std::numeric_limits<std::int64_t>::max)()) {
            Fatal(AbsoluteJudgementFatalReason::HeartbeatFrontierViolation);
        }
        ++committed_boundary_index_;
        diagnostics.CheckAndRecordHeartbeatIndexOrFatal(
            committed_boundary_index_, true, FatalSnapshot());
        IncrementOrFatal(counters.committed_boundaries);
        if (scope.kind == JudgementScopeKind::Event) {
            IncrementOrFatal(counters.equal_boundary_substitutions);
        }
    }

    ApplyHistoryResultOrFatal(history_.PruneBefore(
        scope.coordinate.judgement_seconds,
        CurrentHistoryPrefixEnd()));
    outstanding_scope_.reset();
    IncrementOrFatal(outer_scope_count_);
    diagnostics.SetPendingWork(PendingWorkCount());
}

void JudgementScheduler::FinishOuterCall() noexcept {
    if (!stage_.open() || !outer_prepared_) {
        return;
    }
    if (outstanding_scope_) {
        Fatal(AbsoluteJudgementFatalReason::ScopeLifetimeViolation);
    }
    auto& diagnostics = JudgementDiagnostics();
    if (outer_scope_count_ != 0) {
        auto& counters = diagnostics.stage_counters();
        if (outer_event_scope_count_ == 1 &&
            outer_heartbeat_scope_count_ == 0) {
            IncrementOrFatal(counters.event_only_batches);
        } else if (outer_event_scope_count_ == 0 &&
                   outer_heartbeat_scope_count_ >= 1 &&
                   outer_heartbeat_scope_count_ <= 3) {
            IncrementOrFatal(counters.heartbeat_only_batches);
        } else {
            IncrementOrFatal(counters.mixed_event_batches);
            Fatal(AbsoluteJudgementFatalReason::NativeCallCountMismatch);
        }
        diagnostics.RecordBatch(outer_scope_count_, FatalSnapshot());
        diagnostics.CheckCompletedBatchInvariantOrFatal(FatalSnapshot());
    }
    diagnostics.ObserveBacklog(PendingWorkCount());
    diagnostics.SetPendingWork(PendingWorkCount());
    diagnostics.MaybeLogFiveSecondSummary(RuntimeSnapshot());
    outer_horizon_.reset();
    outer_scope_count_ = 0;
    outer_event_scope_count_ = 0;
    outer_heartbeat_scope_count_ = 0;
    outer_prepared_ = false;
    outer_event_barrier_recorded_ = false;
}

void JudgementScheduler::CheckNativeCallInvariantOrFatal() const noexcept {
    JudgementDiagnostics().CheckNativeCallInvariantOrFatal(FatalSnapshot());
}

AbsoluteJudgementScoreDeltas
JudgementScheduler::CheckAndRecordNativeScoreCountersOrFatal(
    const AbsoluteJudgementNativeScoreCounters& counters) const noexcept {
    return JudgementDiagnostics().CheckAndRecordNativeScoreCountersOrFatal(
        counters, FatalSnapshot());
}

void JudgementScheduler::AccumulateQueryCountersOrFatal(
    const AbsoluteJudgementQueryCounters& counters) const noexcept {
    JudgementDiagnostics().AccumulateQueryCountersOrFatal(
        counters, FatalSnapshot());
}

void JudgementScheduler::RecordTransientPublicationsOrFatal(
    const AbsoluteJudgementTransientPublications& publications)
    const noexcept {
    JudgementDiagnostics().RecordTransientPublicationsOrFatal(
        publications, FatalSnapshot());
}

[[noreturn]] void JudgementScheduler::FailActiveStage(
    const AbsoluteJudgementFatalReason reason) const noexcept {
    Fatal(reason);
}

std::uint64_t JudgementScheduler::CurrentHistoryPrefixEnd() const noexcept {
    const auto* next = history_.FirstResolvedAtOrAfter(
        next_delivery_sequence_);
    return next != nullptr ? next->transport.sequence
                           : history_.next_sequence();
}

bool JudgementScheduler::IsBehindCommittedFrontier(
    const JudgementScopeCoordinate& coordinate) const noexcept {
    if (!committed_frontier_) {
        return false;
    }
    const auto order = coordinate.judgement_seconds.Compare(
        committed_frontier_->judgement_seconds);
    if (order != 0) {
        return order < 0;
    }
    return committed_frontier_is_boundary_ ||
        coordinate.sequence <= committed_frontier_->sequence;
}

std::optional<CheckedRational> JudgementScheduler::BoundaryAt(
    const std::int64_t index) const noexcept {
    const auto result = CheckedRational::Create(index, 60);
    if (!result) {
        return std::nullopt;
    }
    return *result;
}

std::optional<std::pair<std::int32_t, std::int32_t>>
JudgementScheduler::NativeArguments(
    const CheckedRational& judgement_seconds) const noexcept {
    const auto milliseconds = judgement_seconds.Multiply(1000, 1);
    const auto native_ms = milliseconds ? milliseconds->Truncate() :
        std::expected<std::int64_t, gc::timing::RationalError>(
            std::unexpected(gc::timing::RationalError::Overflow));
    const auto frames = judgement_seconds.Multiply(60, 1);
    const auto native_frame = frames ? frames->Floor() :
        std::expected<std::int64_t, gc::timing::RationalError>(
            std::unexpected(gc::timing::RationalError::Overflow));
    if (!milliseconds || !native_ms || !frames || !native_frame ||
        *native_ms < (std::numeric_limits<std::int32_t>::min)() ||
        *native_ms > (std::numeric_limits<std::int32_t>::max)() ||
        *native_frame < (std::numeric_limits<std::int32_t>::min)() ||
        *native_frame > (std::numeric_limits<std::int32_t>::max)()) {
        return std::nullopt;
    }
    return std::pair{
        static_cast<std::int32_t>(*native_ms),
        static_cast<std::int32_t>(*native_frame),
    };
}

void JudgementScheduler::AppendUnresolvedOrFatal(
    const gc::input::GameplayTransitionRecord& record) noexcept {
    if (unresolved_size_ == unresolved_.size()) {
        Fatal(AbsoluteJudgementFatalReason::RetainedHistoryLost);
    }
    const auto write_slot =
        (unresolved_read_slot_ + unresolved_size_) % unresolved_.size();
    unresolved_[write_slot] = record;
    ++unresolved_size_;
}

gc::input::GameplayTransitionRecord&
JudgementScheduler::UnresolvedFront() noexcept {
    if (unresolved_size_ == 0) {
        std::abort();
    }
    return unresolved_[unresolved_read_slot_];
}

void JudgementScheduler::PopUnresolved() noexcept {
    if (unresolved_size_ == 0) {
        Fatal(AbsoluteJudgementFatalReason::RetainedHistoryLost);
    }
    unresolved_read_slot_ =
        (unresolved_read_slot_ + 1) % unresolved_.size();
    --unresolved_size_;
}

void JudgementScheduler::ApplyHistoryResultOrFatal(
    const std::expected<void, JudgementHistoryError>& result) noexcept {
    if (!result) {
        Fatal(HistoryErrorReason(result.error()));
    }
}

void JudgementScheduler::IncrementOrFatal(std::uint64_t& value) noexcept {
    if (value == (std::numeric_limits<std::uint64_t>::max)()) {
        Fatal(AbsoluteJudgementFatalReason::CheckedArithmeticFailure);
    }
    ++value;
}

void JudgementScheduler::FailForClockResult(
    const JudgementClockResult& result) noexcept {
    if (result.status ==
        JudgementClockStatus::CheckedArithmeticFailure) {
        Fatal(AbsoluteJudgementFatalReason::CheckedArithmeticFailure);
    }
    if (result.status ==
        JudgementClockStatus::HistoryLostBeforeBinding) {
        Fatal(AbsoluteJudgementFatalReason::ClockHistoryLost);
    }
    if (result.status ==
        JudgementClockStatus::UnsupportedContinuity) {
        Fatal(AbsoluteJudgementFatalReason::ClockDiscontinuous);
    }
    Fatal(AbsoluteJudgementFatalReason::NativeStateMismatch);
}

AbsoluteJudgementRuntimeSnapshot
JudgementScheduler::RuntimeSnapshot() const noexcept {
    return {
        .last_endpoint_anchor_sequence = last_anchor_sequence_,
        .last_endpoint_position = last_endpoint_position_,
        .last_output_frame = last_output_frame_,
        .last_qpc = last_qpc_,
        .last_j = last_j_,
        .committed_boundary = has_committed_boundary_index_
            ? committed_boundary_index_
            : 0,
        .pending_work = PendingWorkCount(),
        .last_sequence = next_drain_sequence_,
        .held_mask = history_.current_held(),
        .game_time_offset_ms = stage_.bound()
            ? stage_.native().game_time_offset_ms
            : 0,
        .hold_safe_frame = stage_.bound()
            ? stage_.native().hold_safe_frame
            : 0,
        .slide_hold_safe_frame = stage_.bound()
            ? stage_.native().slide_hold_safe_frame
            : 0,
    };
}

std::uint64_t JudgementScheduler::PendingWorkCount() const noexcept {
    const auto unresolved = static_cast<std::uint64_t>(unresolved_size_);
    if (pending_event_count_ >
        (std::numeric_limits<std::uint64_t>::max)() - unresolved) {
        return (std::numeric_limits<std::uint64_t>::max)();
    }
    return pending_event_count_ + unresolved;
}

AbsoluteJudgementFatalSnapshot JudgementScheduler::FatalSnapshot()
    const noexcept {
    AbsoluteJudgementNativeIdentityDiagnostic native{};
    if (stage_.bound()) {
        native = {
            .stage_generation = stage_.native().stage_generation,
            .native_manager = stage_.native().tune_manager,
            .tune = stage_.native().tune,
            .judgement_state = stage_.native().judgement_state,
            .score_state = stage_.native().score_state,
            .booster = stage_.native().booster,
            .player = stage_.native().player,
        };
    } else {
        native.stage_generation = stage_.generation();
        native.native_manager = stage_.tune_manager();
    }
    return {
        .enabled = true,
        .native = native,
        .input_generation = stage_.open()
            ? stage_.cutoff().transport_epoch
            : 0,
        .endpoint_generation = stage_.endpoint_generation(),
        .last_anchor_sequence = last_anchor_sequence_,
        .runtime = RuntimeSnapshot(),
    };
}

[[noreturn]] void JudgementScheduler::Fatal(
    const AbsoluteJudgementFatalReason reason) const noexcept {
    FatalActiveStage(reason, FatalSnapshot());
}

} // namespace gc::absolute_judgement
