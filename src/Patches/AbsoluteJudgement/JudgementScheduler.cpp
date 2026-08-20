#include "Patches/AbsoluteJudgement/JudgementScheduler.h"

#include <algorithm>
#include <bit>
#include <cstdlib>
#include <limits>
#include <span>
#include <utility>

// Native lifecycle authority (completed audit, read-only):
// CTuneGameManager_RunGameplayFrameStateMachine invokes RVA 0x2629A0 only in
// state 5. A false result stays in state 5; a true result advances to state 6.
// The matching cleanup entry is RVA 0x262080. These explicit calls, never an
// elapsed-time or audio heuristic, own the loader stage boundary.

namespace gc::absolute_judgement {
namespace {

using gc::audio::ExactClockStatus;
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
    case JudgementStageError::GameTimeOffsetChanged:
        return AbsoluteJudgementFatalReason::GameTimeOffsetChanged;
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

void JudgementScheduler::BeginNativeStage(
    const std::uintptr_t tune_manager) noexcept {
    if (!stage_.open()) {
        ClearStageOwnedState();
    }
    const auto result = stage_.Begin(tune_manager);
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
    JudgementDiagnostics().LogNativeStageOpen({
        .loader_stage_generation = stage_.generation(),
        .native_manager = tune_manager,
        .input_generation = cutoff.transport_epoch,
        .cutoff_sequence = cutoff.first_stage_sequence,
        .first_eligible_sequence = cutoff.first_stage_sequence,
        .held_baseline = cutoff.held_baseline,
        .transport_fault_baseline = cutoff.eviction_count,
    });
}

void JudgementScheduler::EndNativeStage(
    const std::uintptr_t tune_manager) noexcept {
    if (!stage_.open()) {
        return;
    }
    const auto validation = stage_.ValidateCleanup(tune_manager);
    if (!validation) {
        Fatal(StageErrorReason(validation.error()));
    }
    if (outstanding_scope_) {
        Fatal(AbsoluteJudgementFatalReason::ScopeLifetimeViolation);
    }

    AccountCleanupDropsOrFatal();

    JudgementDiagnostics().LogNativeStageEnd({
        .loader_stage_generation = stage_.generation(),
        .native_manager = tune_manager,
        .activated = stage_.active(),
        .runtime = RuntimeSnapshot(),
    });
    ClearStageOwnedState();
    stage_.Reset();
}

bool JudgementScheduler::NativeStageOpen() const noexcept {
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
    clock_binding_.endpoint_generation = 0;
    clock_binding_.endpoint.reset();
    clock_binding_.observed_stage_bgm_histories.clear();
    history_diagnostics_.clear();
    history_.Reset(0, 0, 0);
    unresolved_read_slot_ = 0;
    unresolved_size_ = 0;
    next_drain_sequence_ = 0;
    next_delivery_sequence_ = 0;
    pending_event_count_ = 0;
    marked_overload_count_ = 0;
    last_selected_buffer_instance_id_ = 0;
    accumulated_clock_waits_ = 0;
    endpoint_publication_baseline_ = 0;
    last_endpoint_publication_count_ = 0;
    has_endpoint_publication_baseline_ = false;
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
    outer_uses_closed_frontier_ = false;
    outer_event_barrier_recorded_ = false;
    outer_closed_frontier_.reset();
    last_output_frame_.reset();
    last_source_frame_.reset();
    last_j_.reset();
    last_closed_frontier_.reset();
    frozen_j_.reset();
    last_anchor_sequence_ = 0;
    last_endpoint_position_.reset();
    outer_now_qpc_ = 0;
    last_qpc_ = 0;
}

void JudgementScheduler::ValidateStageBindingOrFatal(
    const AbsoluteJudgementOuterProbe& probe) noexcept {
    if (!probe.endpoint) {
        Fatal(AbsoluteJudgementFatalReason::EndpointCapabilityUnavailable);
    }
    const auto endpoint_generation = probe.endpoint->endpoint_generation();
    const auto binding = stage_.BindOrValidate(
        probe.native,
        endpoint_generation,
        probe.endpoint->qpc_frequency());
    if (!binding) {
        Fatal(StageErrorReason(binding.error()));
    }
    if (!clock_binding_.endpoint) {
        clock_binding_.endpoint_generation = endpoint_generation;
        clock_binding_.endpoint = probe.endpoint;
        endpoint_publication_baseline_ =
            probe.endpoint->publication_count();
        last_endpoint_publication_count_ =
            endpoint_publication_baseline_;
        has_endpoint_publication_baseline_ = true;
    } else if (clock_binding_.endpoint_generation != endpoint_generation ||
               clock_binding_.endpoint.get() != probe.endpoint.get()) {
        Fatal(AbsoluteJudgementFatalReason::EndpointGenerationChanged);
    }
    if (!has_endpoint_publication_baseline_) {
        Fatal(AbsoluteJudgementFatalReason::NativeStateMismatch);
    }
    const auto publication_count = probe.endpoint->publication_count();
    if (publication_count < last_endpoint_publication_count_ ||
        publication_count < endpoint_publication_baseline_) {
        Fatal(AbsoluteJudgementFatalReason::ClockDiscontinuous);
    }
    last_endpoint_publication_count_ = publication_count;
    auto& counters = JudgementDiagnostics().stage_counters();
    counters.endpoint_publication_count = publication_count;
    counters.endpoint_stage_publications =
        publication_count - endpoint_publication_baseline_;
}

ObservedPlaybackHistory*
JudgementScheduler::RegisterOrValidateObservation(
    const gc::audio::GameplayAudioCursorObservation& observation) {
    if (!observation.exact_history || observation.buffer_instance_id == 0 ||
        observation.endpoint_generation !=
            clock_binding_.endpoint_generation ||
        !observation.exact_history->HasExactPlaybackHistory() ||
        observation.exact_history->exact_buffer_instance_id() !=
            observation.buffer_instance_id ||
        observation.exact_history->exact_endpoint_generation() !=
            observation.endpoint_generation) {
        Fatal(AbsoluteJudgementFatalReason::NativeStateMismatch);
    }

    for (auto& observed :
         clock_binding_.observed_stage_bgm_histories) {
        if (observed.buffer_instance_id != observation.buffer_instance_id) {
            continue;
        }
        if (observed.endpoint_generation !=
                observation.endpoint_generation ||
            observed.history.get() != observation.exact_history.get()) {
            Fatal(AbsoluteJudgementFatalReason::PlaybackMappingConflict);
        }
        return &observed;
    }

    clock_binding_.observed_stage_bgm_histories.push_back({
        .buffer_instance_id = observation.buffer_instance_id,
        .endpoint_generation = observation.endpoint_generation,
        .history = observation.exact_history,
    });
    history_diagnostics_.push_back({
        .buffer_instance_id = observation.buffer_instance_id,
        .endpoint_generation = observation.endpoint_generation,
    });
    return &clock_binding_.observed_stage_bgm_histories.back();
}

ExactClockStatus JudgementScheduler::UpdatePlaybackDiagnostics() noexcept {
    if (history_diagnostics_.size() !=
        clock_binding_.observed_stage_bgm_histories.size()) {
        Fatal(AbsoluteJudgementFatalReason::NativeStateMismatch);
    }
    std::uint64_t play_epochs{};
    std::uint64_t seek_epochs{};
    for (std::size_t index = 0;
         index < clock_binding_.observed_stage_bgm_histories.size();
         ++index) {
        const auto& observed =
            clock_binding_.observed_stage_bgm_histories[index];
        if (!observed.history) {
            return ExactClockStatus::Discontinuous;
        }
        auto& diagnostic = history_diagnostics_[index];
        gc::audio::ExactPlaybackHistoryStatus status{};
        const auto count = observed.history->CopyExactPlaybackEpochs(
            left_epoch_scratch_, &status);
        if (status.prefix_evicted) {
            return ExactClockStatus::HistoryLost;
        }
        if (observed.last_validated_publication != 0 &&
            (status.status == ExactClockStatus::Pending ||
             status.status == ExactClockStatus::NoPlayback)) {
            return ExactClockStatus::Discontinuous;
        }
        if (status.status != ExactClockStatus::Resolved) {
            return status.status;
        }
        if (count == 0) {
            return ExactClockStatus::Pending;
        }
        if (status.publication_sequence <
            observed.last_validated_publication) {
            return ExactClockStatus::Discontinuous;
        }
        if (status.publication_sequence !=
            observed.last_validated_publication) {
            return ExactClockStatus::TemporarilyUnavailable;
        }
        diagnostic.play_epoch_count = 0;
        diagnostic.seek_epoch_count = 0;
        for (std::size_t epoch = 0; epoch < count; ++epoch) {
            if (left_epoch_scratch_[epoch].origin ==
                gc::audio::ExactPlaybackOrigin::Play) {
                if (play_epochs ==
                    (std::numeric_limits<std::uint64_t>::max)()) {
                    Fatal(
                        AbsoluteJudgementFatalReason::CheckedArithmeticFailure);
                }
                ++diagnostic.play_epoch_count;
                ++play_epochs;
            } else {
                if (seek_epochs ==
                    (std::numeric_limits<std::uint64_t>::max)()) {
                    Fatal(
                        AbsoluteJudgementFatalReason::CheckedArithmeticFailure);
                }
                ++diagnostic.seek_epoch_count;
                ++seek_epochs;
            }
        }
        const auto& first = left_epoch_scratch_[0];
        const auto& last = left_epoch_scratch_[count - 1];
        diagnostic.last_playback_generation = last.playback_generation;
        diagnostic.output_origin = first.output_origin;
        diagnostic.source_origin = first.source_origin;
        diagnostic.output_rate = first.output_rate;
        diagnostic.source_rate = first.source_rate;
    }
    if (seek_epochs > (std::numeric_limits<std::uint64_t>::max)() -
            play_epochs) {
        Fatal(AbsoluteJudgementFatalReason::CheckedArithmeticFailure);
    }
    auto& counters = JudgementDiagnostics().stage_counters();
    counters.playback_play_epochs = play_epochs;
    counters.playback_seek_epochs = seek_epochs;
    counters.playback_epochs = play_epochs + seek_epochs;
    return ExactClockStatus::Resolved;
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
    outer_uses_closed_frontier_ = false;
    outer_event_barrier_recorded_ = false;
    outer_closed_frontier_.reset();
    outer_now_qpc_ = probe.now_qpc;
    last_qpc_ = probe.now_qpc;
    IncrementOrFatal(JudgementDiagnostics().stage_counters().outer_calls);

    ValidateStageBindingOrFatal(probe);

    ObservedPlaybackHistory* selected{};
    if (probe.group2_cursor_selected) {
        if (!probe.group2_observation) {
            Fatal(AbsoluteJudgementFatalReason::NativeStateMismatch);
        }
        selected = RegisterOrValidateObservation(*probe.group2_observation);
        last_selected_buffer_instance_id_ = selected->buffer_instance_id;
    }

    const auto validation = clock_resolver_.ValidateRetainedHistories(
        clock_binding_, left_epoch_scratch_, right_epoch_scratch_);
    if (validation.checked_arithmetic_failure) {
        Fatal(AbsoluteJudgementFatalReason::CheckedArithmeticFailure);
    }
    if (validation.status == ExactClockStatus::HistoryLost) {
        Fatal(AbsoluteJudgementFatalReason::ClockHistoryLost);
    }
    if (validation.status == ExactClockStatus::Discontinuous) {
        Fatal(AbsoluteJudgementFatalReason::PlaybackMappingConflict);
    }
    auto validation_status = validation.status;
    if (validation_status != ExactClockStatus::Resolved &&
        validation_status != ExactClockStatus::Pending &&
        validation_status != ExactClockStatus::TemporarilyUnavailable) {
        Fatal(AbsoluteJudgementFatalReason::PlaybackMappingConflict);
    }
    if (validation_status == ExactClockStatus::Resolved) {
        validation_status = UpdatePlaybackDiagnostics();
        if (validation_status == ExactClockStatus::HistoryLost) {
            Fatal(AbsoluteJudgementFatalReason::ClockHistoryLost);
        }
        if (validation_status == ExactClockStatus::Discontinuous) {
            Fatal(AbsoluteJudgementFatalReason::PlaybackMappingConflict);
        }
        if (validation_status != ExactClockStatus::Resolved &&
            validation_status != ExactClockStatus::Pending &&
            validation_status !=
                ExactClockStatus::TemporarilyUnavailable) {
            Fatal(AbsoluteJudgementFatalReason::PlaybackMappingConflict);
        }
    }

    DrainTransportOrFatal();
    const auto unresolved_status =
        ResolveUnresolvedPrefixOrFatal(validation_status);
    TryActivateOrWait(validation_status);
    if (validation_status != ExactClockStatus::Resolved ||
        unresolved_status != ExactClockStatus::Resolved) {
        auto& diagnostics = JudgementDiagnostics();
        diagnostics.ObserveBacklog(PendingWorkCount());
        diagnostics.SetPendingWork(PendingWorkCount());
        return;
    }
    SelectOuterHorizonOrFatal(probe, selected);

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
    if (!gc::input::CaptureGameplayTransitionCutoff(&cutoff)) {
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
    add_classified(counters.outside_playback_baseline_records);
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

ExactClockStatus JudgementScheduler::ResolveUnresolvedPrefixOrFatal(
    const ExactClockStatus validation_status) noexcept {
    if (validation_status == ExactClockStatus::TemporarilyUnavailable ||
        validation_status == ExactClockStatus::Pending) {
        return validation_status;
    }
    if (validation_status != ExactClockStatus::Resolved) {
        Fatal(AbsoluteJudgementFatalReason::NativeStateMismatch);
    }

    auto& counters = JudgementDiagnostics().stage_counters();
    while (unresolved_size_ != 0) {
        const auto& record = UnresolvedFront();
        IncrementOrFatal(counters.exact_clock_reads);
        const auto resolved = clock_resolver_.ResolveHistoricalQpc(
            clock_binding_,
            record.qpc_ticks,
            stage_.native().game_time_offset_ms,
            left_epoch_scratch_);
        last_qpc_ = record.qpc_ticks;
        last_output_frame_ = resolved.output_frame;
        last_source_frame_ = resolved.source_frame;
        last_j_ = resolved.judgement_seconds
            ? resolved.judgement_seconds
            : resolved.closed_frontier_seconds;
        if (resolved.endpoint_anchor_sequence != 0) {
            last_anchor_sequence_ = resolved.endpoint_anchor_sequence;
        }
        if (resolved.endpoint_anchor_position) {
            last_endpoint_position_ = resolved.endpoint_anchor_position;
        }
        if (resolved.checked_arithmetic_failure) {
            Fatal(AbsoluteJudgementFatalReason::CheckedArithmeticFailure);
        }
        if (resolved.status == ExactClockStatus::Pending) {
            return ExactClockStatus::Pending;
        }
        if (resolved.status == ExactClockStatus::TemporarilyUnavailable) {
            IncrementOrFatal(counters.unavailable_clock_reads);
            return ExactClockStatus::TemporarilyUnavailable;
        }
        if (resolved.status == ExactClockStatus::OutsidePlayback) {
            ApplyHistoryResultOrFatal(history_.ApplyBaselineOnly(
                record, BaselineOnlyReason::OutsidePlayback));
            IncrementOrFatal(counters.outside_playback_baseline_records);
            PopUnresolved();
            continue;
        }
        if (resolved.status != ExactClockStatus::Resolved ||
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
            ApplyHistoryResultOrFatal(history_.ApplyBaselineOnly(
                record, BaselineOnlyReason::AcceptedLate));
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
    return ExactClockStatus::Resolved;
}

void JudgementScheduler::TryActivateOrWait(
    const ExactClockStatus validation_status) noexcept {
    if (stage_.active()) {
        return;
    }
    if (validation_status != ExactClockStatus::Resolved) {
        IncrementOrFatal(accumulated_clock_waits_);
        return;
    }
    const auto origin = clock_resolver_.FindFirstPlaybackOrigin(
        clock_binding_,
        stage_.native().game_time_offset_ms,
        left_epoch_scratch_);
    if (origin.checked_arithmetic_failure) {
        Fatal(AbsoluteJudgementFatalReason::CheckedArithmeticFailure);
    }
    if (origin.status == ExactClockStatus::Pending ||
        origin.status == ExactClockStatus::TemporarilyUnavailable) {
        IncrementOrFatal(accumulated_clock_waits_);
        return;
    }
    if (origin.status != ExactClockStatus::Resolved ||
        !origin.judgement_seconds) {
        Fatal(origin.status == ExactClockStatus::HistoryLost
                  ? AbsoluteJudgementFatalReason::ClockHistoryLost
                  : AbsoluteJudgementFatalReason::ClockDiscontinuous);
    }

    const auto scaled = origin.judgement_seconds->Multiply(60, 1);
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
    last_j_ = origin.judgement_seconds;
    JudgementDiagnostics().SeedHeartbeatIndex(committed_boundary_index_);
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
        .histories = history_diagnostics_,
        .initial_j = *origin.judgement_seconds,
        .committed_boundary_seed = committed_boundary_index_,
        .game_time_offset_ms = stage_.native().game_time_offset_ms,
        .hold_safe_frame = stage_.native().hold_safe_frame,
        .slide_hold_safe_frame = stage_.native().slide_hold_safe_frame,
        .accumulated_clock_waits = accumulated_clock_waits_,
    });
}

void JudgementScheduler::SelectOuterHorizonOrFatal(
    const AbsoluteJudgementOuterProbe& probe,
    const ObservedPlaybackHistory* selected) noexcept {
    if (!stage_.active()) {
        return;
    }

    auto& counters = JudgementDiagnostics().stage_counters();
    JudgementClockResult current{};
    bool can_use_result{};
    bool resolved_ready_allowed{};
    if (probe.group2_cursor_selected) {
        if (selected == nullptr || !probe.group2_observation) {
            Fatal(AbsoluteJudgementFatalReason::NativeStateMismatch);
        }
        switch (probe.group2_observation->state) {
        case gc::audio::GameplayAudioCursorState::Exact:
            resolved_ready_allowed = true;
            [[fallthrough]];
        case gc::audio::GameplayAudioCursorState::Pending:
        case gc::audio::GameplayAudioCursorState::Inactive:
            IncrementOrFatal(counters.exact_clock_reads);
            last_qpc_ = probe.now_qpc;
            current = clock_resolver_.ResolveCurrentQpc(
                clock_binding_,
                *selected,
                probe.now_qpc,
                stage_.native().game_time_offset_ms,
                left_epoch_scratch_);
            can_use_result = true;
            break;
        }
    } else if (last_selected_buffer_instance_id_ != 0) {
        for (const auto& observed :
             clock_binding_.observed_stage_bgm_histories) {
            if (observed.buffer_instance_id ==
                last_selected_buffer_instance_id_) {
                IncrementOrFatal(counters.exact_clock_reads);
                last_qpc_ = probe.now_qpc;
                current = clock_resolver_.ResolveCurrentQpc(
                    clock_binding_,
                    observed,
                    probe.now_qpc,
                    stage_.native().game_time_offset_ms,
                    left_epoch_scratch_);
                can_use_result = true;
                break;
            }
        }
        if (!can_use_result) {
            Fatal(AbsoluteJudgementFatalReason::NativeStateMismatch);
        }
    }
    if (!can_use_result) {
        return;
    }

    last_output_frame_ = current.output_frame;
    last_source_frame_ = current.source_frame;
    last_j_ = current.judgement_seconds
        ? current.judgement_seconds
        : current.closed_frontier_seconds;
    if (current.endpoint_anchor_sequence != 0) {
        last_anchor_sequence_ = current.endpoint_anchor_sequence;
    }
    if (current.endpoint_anchor_position) {
        last_endpoint_position_ = current.endpoint_anchor_position;
    }
    if (current.checked_arithmetic_failure) {
        Fatal(AbsoluteJudgementFatalReason::CheckedArithmeticFailure);
    }
    if (current.status == ExactClockStatus::TemporarilyUnavailable) {
        IncrementOrFatal(counters.unavailable_clock_reads);
        return;
    }
    if (current.status == ExactClockStatus::Pending) {
        return;
    }
    if (current.status == ExactClockStatus::Resolved &&
        resolved_ready_allowed && current.judgement_seconds) {
        IncrementOrFatal(counters.resolved_clock_reads);
        last_j_ = current.judgement_seconds;
        SetReadyHorizonOrFatal(*current.judgement_seconds, false);
        return;
    }
    if (current.status == ExactClockStatus::OutsidePlayback) {
        if (current.closed_frontier_seconds) {
            last_j_ = current.closed_frontier_seconds;
            SetReadyHorizonOrFatal(
                *current.closed_frontier_seconds, true);
        }
        return;
    }
    if (current.status == ExactClockStatus::Resolved &&
        !resolved_ready_allowed) {
        return;
    }
    FailForClockResult(current);
}

void JudgementScheduler::SetReadyHorizonOrFatal(
    const CheckedRational& ready,
    const bool closed_frontier) noexcept {
    if (committed_frontier_ &&
        ready.Compare(committed_frontier_->judgement_seconds) < 0) {
        Fatal(AbsoluteJudgementFatalReason::ClockDiscontinuous);
    }
    if (!has_committed_boundary_index_) {
        Fatal(AbsoluteJudgementFatalReason::HeartbeatFrontierViolation);
    }
    if (closed_frontier) {
        if (!last_closed_frontier_ ||
            last_closed_frontier_->Compare(ready) != 0) {
            IncrementOrFatal(JudgementDiagnostics().stage_counters()
                                 .closed_frontier_selections);
            last_closed_frontier_ = ready;
        }
        outer_closed_frontier_ = ready;
    } else if (frozen_j_ && ready.Compare(*frozen_j_) > 0) {
        frozen_j_.reset();
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
    outer_uses_closed_frontier_ = closed_frontier;
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
    if (marked_overload_count_ > required_marked) {
        Fatal(AbsoluteJudgementFatalReason::CommittedOrderViolation);
    }
    const auto additional = required_marked - marked_overload_count_;
    if (additional > (std::numeric_limits<std::uint64_t>::max)() -
            marked_overload_count_) {
        Fatal(AbsoluteJudgementFatalReason::CheckedArithmeticFailure);
    }
    marked_overload_count_ += additional;
}

void JudgementScheduler::ConsumeMarkedOverloadOrFatal(
    const ResolvedGameplayTransition& event) noexcept {
    if (marked_overload_count_ == 0 || pending_event_count_ == 0 ||
        event.transport.sequence ==
            (std::numeric_limits<std::uint64_t>::max)()) {
        Fatal(AbsoluteJudgementFatalReason::CommittedOrderViolation);
    }
    ApplyHistoryResultOrFatal(history_.ConvertResolvedToBaselineOnly(
        event.transport.sequence, BaselineOnlyReason::Overload));
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

void JudgementScheduler::UpdateFrozenCoordinateOrFatal() noexcept {
    if (!outer_closed_frontier_) {
        return;
    }
    if (!has_committed_boundary_index_ ||
        committed_boundary_index_ ==
            (std::numeric_limits<std::int64_t>::max)()) {
        Fatal(AbsoluteJudgementFatalReason::HeartbeatFrontierViolation);
    }
    const auto* event = history_.FirstResolvedAtOrAfter(
        next_delivery_sequence_);
    const bool event_due = event != nullptr &&
        event->judgement_seconds.Compare(*outer_closed_frontier_) <= 0;
    const auto boundary = BoundaryAt(committed_boundary_index_ + 1);
    if (!boundary) {
        Fatal(AbsoluteJudgementFatalReason::CheckedArithmeticFailure);
    }
    const bool boundary_due =
        boundary->Compare(*outer_closed_frontier_) <= 0;
    if (!event_due && !boundary_due) {
        frozen_j_ = *outer_closed_frontier_;
    }
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
        if (outer_uses_closed_frontier_) {
            IncrementOrFatal(
                diagnostics.stage_counters().closed_frontier_catchups);
        }
        diagnostics.CheckCompletedBatchInvariantOrFatal(FatalSnapshot());
    }
    UpdateFrozenCoordinateOrFatal();
    diagnostics.ObserveBacklog(PendingWorkCount());
    diagnostics.SetPendingWork(PendingWorkCount());
    diagnostics.MaybeLogFiveSecondSummary(RuntimeSnapshot());
    outer_horizon_.reset();
    outer_scope_count_ = 0;
    outer_event_scope_count_ = 0;
    outer_heartbeat_scope_count_ = 0;
    outer_prepared_ = false;
    outer_uses_closed_frontier_ = false;
    outer_event_barrier_recorded_ = false;
    outer_closed_frontier_.reset();
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
    if (result.checked_arithmetic_failure) {
        Fatal(AbsoluteJudgementFatalReason::CheckedArithmeticFailure);
    }
    if (result.status == ExactClockStatus::HistoryLost) {
        Fatal(AbsoluteJudgementFatalReason::ClockHistoryLost);
    }
    if (result.status == ExactClockStatus::Discontinuous) {
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
        .last_source_frame = last_source_frame_,
        .last_qpc = last_qpc_,
        .last_j = last_j_,
        .last_closed_frontier = last_closed_frontier_,
        .frozen_j = frozen_j_,
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
        .target_fps = JudgementDiagnostics().startup_target_fps(),
        .native = native,
        .input_generation = stage_.open()
            ? stage_.cutoff().transport_epoch
            : 0,
        .endpoint_generation = stage_.endpoint_generation(),
        .last_anchor_sequence = last_anchor_sequence_,
        .histories = history_diagnostics_,
        .runtime = RuntimeSnapshot(),
    };
}

[[noreturn]] void JudgementScheduler::Fatal(
    const AbsoluteJudgementFatalReason reason) const noexcept {
    FatalActiveStage(reason, FatalSnapshot());
}

} // namespace gc::absolute_judgement
