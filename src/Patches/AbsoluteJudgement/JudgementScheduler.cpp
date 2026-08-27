#include "Patches/AbsoluteJudgement/JudgementScheduler.h"

#include <algorithm>
#include <bit>
#include <limits>
#include <span>
#include <utility>

// Semantic lifecycle authority (completed audit, read-only): state 16 commits
// state 17 and reaches RVA 0x2641CC before frame-zero input/audio work. Normal
// state-18 completion reaches RVA 0x264D9A immediately before state 19. A
// canceled task may instead reach CTuneGameManager_InitGameplayState at RVA
// 0x26251C before another entry without traversing the normal exit site; that
// native reinitialization terminates any still-open loader generation.

namespace gc::absolute_judgement
{
    namespace
    {
        using gc::timing::CheckedRational;

        bool SameScope(const ScheduledJudgementScope& left, const ScheduledJudgementScope& right) noexcept
        {
            return left.kind == right.kind &&
                left.coordinate.judgement_seconds.Compare(right.coordinate.judgement_seconds) == 0 &&
                left.coordinate.sequence == right.coordinate.sequence && left.native_ms == right.native_ms &&
                left.native_frame == right.native_frame && left.event == right.event &&
                left.history_prefix_end_sequence == right.history_prefix_end_sequence &&
                left.commits_boundary == right.commits_boundary;
        }
    } // namespace

    void JudgementScheduler::BeginSemanticStage(const std::uintptr_t tune_manager,
                                                const gc::timing::AbsoluteHostTime& stage_entry_time,
                                                const std::int32_t game_time_offset_ms,
                                                const std::int32_t hold_safe_frame,
                                                const std::int32_t slide_hold_safe_frame) noexcept
    {
        if (!stage_.open())
        {
            ClearStageOwnedState();
        }
        if (hold_safe_frame != 0)
        {
            Fatal(AbsoluteJudgementFatalPredicate::HoldSafeFrameNonZero, AbsoluteJudgementFatalReason::SafeFrameChanged,
                  {static_cast<std::uint64_t>(static_cast<std::uint32_t>(hold_safe_frame))});
        }
        if (slide_hold_safe_frame != 0)
        {
            Fatal(AbsoluteJudgementFatalPredicate::SlideHoldSafeFrameNonZero,
                  AbsoluteJudgementFatalReason::SafeFrameChanged,
                  {static_cast<std::uint64_t>(static_cast<std::uint32_t>(slide_hold_safe_frame))});
        }
        const auto result =
            stage_.Begin(tune_manager, stage_entry_time, game_time_offset_ms, hold_safe_frame, slide_hold_safe_frame);
        if (!result)
        {
            FatalStageError(result.error());
        }

        const auto& cutoff = stage_.cutoff();
        ApplyHistoryResultOrFatal(
            history_.Reset(cutoff.transport_epoch, cutoff.first_stage_sequence, cutoff.held_baseline));
        next_drain_sequence_ = cutoff.first_stage_sequence;
        next_delivery_sequence_ = cutoff.first_stage_sequence;
        clock_resolver_.Reset(stage_.generation(), stage_entry_time, game_time_offset_ms);
        JudgementDiagnostics().LogSemanticStageOpen({
            .loader_stage_generation = stage_.generation(),
            .native_manager = tune_manager,
            .input_generation = cutoff.transport_epoch,
            .cutoff_sequence = cutoff.first_stage_sequence,
            .first_eligible_sequence = cutoff.first_stage_sequence,
            .held_baseline = cutoff.held_baseline,
            .transport_fault_baseline = cutoff.eviction_count,
            .stage_entry_qpc = cutoff.stage_entry_time.qpc_ticks,
            .stage_entry_multimedia_time_ms = cutoff.stage_entry_time.multimedia_time_ms,
            .stage_entry_handoff_drops = cutoff.stage_entry_handoff_drops,
        });
    }

    bool JudgementScheduler::TerminateSemanticStageForGameplayInitialization(const std::uintptr_t tune_manager) noexcept
    {
        if (!stage_.open())
        {
            return false;
        }
        if (tune_manager == 0 || tune_manager != stage_.tune_manager())
        {
            Fatal(AbsoluteJudgementFatalPredicate::SemanticStageReceiverMismatch,
                  AbsoluteJudgementFatalReason::NativeIdentityChanged, {stage_.tune_manager(), tune_manager});
        }
        if (outstanding_scope_)
        {
            Fatal(AbsoluteJudgementFatalPredicate::ScopeLifetimeMismatch,
                  AbsoluteJudgementFatalReason::ScopeLifetimeViolation);
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
        return true;
    }

    void JudgementScheduler::EndSemanticStage(const std::uintptr_t tune_manager) noexcept
    {
        if (!stage_.open())
        {
            Fatal(AbsoluteJudgementFatalPredicate::SemanticStageExitWithoutOpen,
                  AbsoluteJudgementFatalReason::NativeStateMismatch, {tune_manager});
        }
        if (tune_manager == 0 || tune_manager != stage_.tune_manager())
        {
            Fatal(AbsoluteJudgementFatalPredicate::SemanticStageReceiverMismatch,
                  AbsoluteJudgementFatalReason::NativeIdentityChanged, {stage_.tune_manager(), tune_manager});
        }
        if (outstanding_scope_)
        {
            Fatal(AbsoluteJudgementFatalPredicate::ScopeLifetimeMismatch,
                  AbsoluteJudgementFatalReason::ScopeLifetimeViolation);
        }
        if (stage_.endpoint_generation() == 0)
        {
            Fatal(AbsoluteJudgementFatalPredicate::EndpointProviderMissingAtStageExit,
                  AbsoluteJudgementFatalReason::EndpointCapabilityUnavailable);
        }
        if (!clock_resolver_.bound() || !stage_.active())
        {
            Fatal(AbsoluteJudgementFatalPredicate::StageOriginUnboundAtStageExit,
                  AbsoluteJudgementFatalReason::ClockHistoryLost);
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

    bool JudgementScheduler::SemanticStageOpen() const noexcept
    {
        return stage_.open();
    }

    std::uint64_t JudgementScheduler::stage_generation() const noexcept
    {
        return stage_.generation();
    }

    const NativeJudgementIdentity& JudgementScheduler::native_identity() const noexcept
    {
        return stage_.native();
    }

    const JudgementHistory& JudgementScheduler::history() const noexcept
    {
        return history_;
    }

    std::optional<std::int64_t> JudgementScheduler::committed_boundary_index() const noexcept
    {
        if (!has_committed_boundary_index_)
        {
            return std::nullopt;
        }
        return committed_boundary_index_;
    }

    void JudgementScheduler::ClearStageOwnedState() noexcept
    {
        clock_resolver_.Reset(0, {}, 0);
        const auto reset = history_.Reset(0, 0, 0);
        if (!reset)
        {
            FatalHistoryError(reset.error());
        }
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

    void JudgementScheduler::ValidateStageBindingOrFatal(const AbsoluteJudgementOuterProbe& probe) noexcept
    {
        const auto native = stage_.BindOrValidateNative(probe.native);
        if (!native)
        {
            FatalStageError(native.error(), &probe.native);
        }
        if (!probe.endpoint)
        {
            Fatal(AbsoluteJudgementFatalPredicate::ExactOutputProviderMissing,
                  AbsoluteJudgementFatalReason::EndpointCapabilityUnavailable);
        }
        const auto endpoint_info = probe.endpoint->info();
        const auto endpoint =
            stage_.BindEndpointOrValidate(endpoint_info.endpoint_generation, endpoint_info.qpc_frequency);
        if (!endpoint)
        {
            if (endpoint.error() == JudgementStageError::EndpointGenerationChanged)
            {
                Fatal(AbsoluteJudgementFatalPredicate::EndpointGenerationChanged,
                      AbsoluteJudgementFatalReason::EndpointGenerationChanged,
                      {stage_.endpoint_generation(), endpoint_info.endpoint_generation});
            }
            if (endpoint.error() == JudgementStageError::QpcFrequencyChanged)
            {
                Fatal(AbsoluteJudgementFatalPredicate::EndpointQpcFrequencyMismatch,
                      AbsoluteJudgementFatalReason::ClockDiscontinuous,
                      {
                          static_cast<std::uint64_t>(stage_.cutoff().qpc_frequency),
                          static_cast<std::uint64_t>(endpoint_info.qpc_frequency)
                      });
            }
            FatalStageError(endpoint.error(), &probe.native);
        }
        JudgementDiagnostics().stage_counters().endpoint_publication_count =
            probe.endpoint->counters().publication_count;
    }

    void JudgementScheduler::PrepareOuterCall(const AbsoluteJudgementOuterProbe& probe)
    {
        if (!stage_.open())
        {
            return;
        }
        if (outer_prepared_ || outstanding_scope_)
        {
            Fatal(AbsoluteJudgementFatalPredicate::ScopeAlreadyActive,
                  AbsoluteJudgementFatalReason::ScopeLifetimeViolation,
                  {outer_prepared_ ? 1u : 0u, outstanding_scope_ ? 1u : 0u});
        }
        outer_prepared_ = true;
        outer_horizon_.reset();
        outer_scope_count_ = 0;
        outer_event_scope_count_ = 0;
        outer_heartbeat_scope_count_ = 0;
        outer_event_barrier_recorded_ = false;
        outer_now_qpc_ = probe.now.qpc_ticks;
        last_qpc_ = probe.now.qpc_ticks;
        IncrementDiagnostic(JudgementDiagnostics().stage_counters().outer_calls);

        ValidateStageBindingOrFatal(probe);
        DrainTransportOrFatal();

        JudgementClockResult entry_clock{
            .status = JudgementClockStatus::Pending,
        };
        if (!clock_resolver_.bound())
        {
            if (probe.group2_cursor_selected && !probe.group2_observation)
            {
                Fatal(AbsoluteJudgementFatalPredicate::EndpointProjectionDiscontinuous,
                      AbsoluteJudgementFatalReason::NativeStateMismatch,
                      {static_cast<std::uint64_t>(probe.now.qpc_ticks)});
            }
            if (probe.group2_cursor_selected && probe.group2_observation)
            {
                entry_clock = clock_resolver_.TryBind(*probe.group2_observation, probe.endpoint, left_epoch_scratch_);
                if (entry_clock.status == JudgementClockStatus::CheckedArithmeticFailure ||
                    entry_clock.status == JudgementClockStatus::HistoryLostBeforeBinding ||
                    entry_clock.status == JudgementClockStatus::UnsupportedContinuity)
                {
                    FailForClockResult(entry_clock);
                }
            }
        }
        else
        {
            const auto endpoint_info = probe.endpoint->info();
            if (endpoint_info.endpoint_generation != clock_resolver_.anchor().endpoint_generation ||
                probe.endpoint.get() != clock_resolver_.anchor().endpoint.get())
            {
                const auto expected_provider =
                    reinterpret_cast<std::uintptr_t>(clock_resolver_.anchor().endpoint.get());
                const auto actual_provider = reinterpret_cast<std::uintptr_t>(probe.endpoint.get());
                Fatal(endpoint_info.endpoint_generation != clock_resolver_.anchor().endpoint_generation
                          ? AbsoluteJudgementFatalPredicate::EndpointGenerationChanged
                          : AbsoluteJudgementFatalPredicate::EndpointProviderIdentityChanged,
                      AbsoluteJudgementFatalReason::EndpointGenerationChanged,
                      {
                          clock_resolver_.anchor().endpoint_generation, endpoint_info.endpoint_generation,
                          expected_provider, actual_provider
                      });
            }
            if (!stage_.active())
            {
                entry_clock = clock_resolver_.Resolve(
                    stage_.cutoff().stage_entry_time,
                    gc::audio::ExactClockResolveIntent::FinalizedTimestamp);
            }
        }

        TryActivateOrWait(entry_clock);
        if (!clock_resolver_.bound() || !stage_.active())
        {
            auto& diagnostics = JudgementDiagnostics();
            diagnostics.ObserveBacklog(PendingWorkCount());
            diagnostics.SetPendingWork(PendingWorkCount());
            return;
        }

        const auto unresolved_status = ResolveUnresolvedPrefixOrFatal();
        if (unresolved_status != JudgementClockStatus::Resolved)
        {
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

    void JudgementScheduler::DrainTransportOrFatal() noexcept
    {
        auto& diagnostics = JudgementDiagnostics();
        for (;;)
        {
            if (unresolved_size_ > unresolved_.size())
            {
                Fatal(AbsoluteJudgementFatalPredicate::UnresolvedCapacityExhausted,
                      AbsoluteJudgementFatalReason::RetainedHistoryLost, {unresolved_.size(), unresolved_size_});
            }
            const auto free_capacity = unresolved_.size() - unresolved_size_;
            const auto requested = (std::min)(free_capacity, drain_batch_.size());
            gc::input::GameplayTransitionStatus status{};
            const auto count = gc::input::DrainGameplayTransitions(
                std::span<gc::input::GameplayTransitionRecord>(drain_batch_.data(), requested), &status);
            if (count > requested ||
                count > (std::numeric_limits<std::uint64_t>::max)() - static_cast<std::uint64_t>(status.depth))
            {
                Fatal(AbsoluteJudgementFatalPredicate::TransportDrainContradiction,
                      AbsoluteJudgementFatalReason::TransportSequenceError,
                      {count, requested, status.depth, status.next_sequence});
            }
            diagnostics.ObserveTransportPendingDepth(static_cast<std::uint64_t>(count) + status.depth);
            if (!status.enabled)
            {
                Fatal(AbsoluteJudgementFatalPredicate::InputTransportWorkerBecameInactive,
                      AbsoluteJudgementFatalReason::InputCapabilityUnavailable,
                      {status.enabled ? 1u : 0u, status.active ? 1u : 0u});
            }
            if (!status.active)
            {
                if (status.next_sequence == (std::numeric_limits<std::uint64_t>::max)())
                {
                    Fatal(AbsoluteJudgementFatalPredicate::SequenceExhausted,
                          AbsoluteJudgementFatalReason::CheckedArithmeticFailure, {status.next_sequence});
                }
                if (status.eviction_count != stage_.cutoff().eviction_count)
                {
                    Fatal(AbsoluteJudgementFatalPredicate::TransportEvicted,
                          AbsoluteJudgementFatalReason::TransportEviction,
                          {stage_.cutoff().eviction_count, status.eviction_count});
                }
                Fatal(AbsoluteJudgementFatalPredicate::InputTransportWorkerBecameInactive,
                      AbsoluteJudgementFatalReason::InputCapabilityUnavailable, {1, 0});
            }
            if (status.transport_epoch != stage_.cutoff().transport_epoch)
            {
                Fatal(AbsoluteJudgementFatalPredicate::InputTransportEpochChanged,
                      AbsoluteJudgementFatalReason::TransportEpochLost,
                      {stage_.cutoff().transport_epoch, status.transport_epoch});
            }
            if (status.qpc_frequency != stage_.cutoff().qpc_frequency)
            {
                Fatal(AbsoluteJudgementFatalPredicate::InputQpcFrequencyChanged,
                      AbsoluteJudgementFatalReason::ClockDiscontinuous,
                      {
                          static_cast<std::uint64_t>(stage_.cutoff().qpc_frequency),
                          static_cast<std::uint64_t>(status.qpc_frequency)
                      });
            }
            if (status.eviction_count != stage_.cutoff().eviction_count)
            {
                Fatal(AbsoluteJudgementFatalPredicate::TransportEvicted,
                      AbsoluteJudgementFatalReason::TransportEviction,
                      {stage_.cutoff().eviction_count, status.eviction_count});
            }
            if (status.next_sequence < status.depth)
            {
                Fatal(AbsoluteJudgementFatalPredicate::TransportDrainContradiction,
                      AbsoluteJudgementFatalReason::TransportSequenceError,
                      {count, requested, status.depth, status.next_sequence});
            }

            for (std::size_t index = 0; index < count; ++index)
            {
                const auto& record = drain_batch_[index];
                if (record.transport_epoch != stage_.cutoff().transport_epoch)
                {
                    IncrementDiagnostic(diagnostics.stage_counters().sequence_errors);
                    Fatal(AbsoluteJudgementFatalPredicate::InputTransportEpochChanged,
                          AbsoluteJudgementFatalReason::TransportEpochLost,
                          {stage_.cutoff().transport_epoch, record.transport_epoch});
                }
                if (record.sequence != next_drain_sequence_)
                {
                    IncrementDiagnostic(diagnostics.stage_counters().sequence_errors);
                    Fatal(AbsoluteJudgementFatalPredicate::TransportSequenceDiscontinuous,
                          AbsoluteJudgementFatalReason::TransportSequenceError,
                          {next_drain_sequence_, record.sequence});
                }
                if (record.sequence == (std::numeric_limits<std::uint64_t>::max)())
                {
                    Fatal(AbsoluteJudgementFatalPredicate::SequenceExhausted,
                          AbsoluteJudgementFatalReason::CheckedArithmeticFailure, {record.sequence});
                }
                IncrementDiagnostic(diagnostics.stage_counters().transport_records_drained);
                const auto rising = static_cast<std::uint64_t>(std::popcount(record.rising));
                const auto falling = static_cast<std::uint64_t>(std::popcount(record.falling));
                auto& counters = diagnostics.stage_counters();
                counters.transport_rising_controls =
                    rising <= (std::numeric_limits<std::uint64_t>::max)() - counters.transport_rising_controls
                        ? counters.transport_rising_controls + rising
                        : (std::numeric_limits<std::uint64_t>::max)();
                counters.transport_falling_controls =
                    falling <= (std::numeric_limits<std::uint64_t>::max)() - counters.transport_falling_controls
                        ? counters.transport_falling_controls + falling
                        : (std::numeric_limits<std::uint64_t>::max)();
                AppendUnresolvedOrFatal(record);
                ++next_drain_sequence_;
            }
            const auto first_remaining_sequence = status.next_sequence - status.depth;
            if (first_remaining_sequence != next_drain_sequence_)
            {
                IncrementDiagnostic(diagnostics.stage_counters().sequence_errors);
                Fatal(AbsoluteJudgementFatalPredicate::TransportSequenceDiscontinuous,
                      AbsoluteJudgementFatalReason::TransportSequenceError,
                      {next_drain_sequence_, first_remaining_sequence});
            }
            diagnostics.ObserveTransportPendingDepth(status.depth);
            if (status.depth != 0 && unresolved_size_ == unresolved_.size())
            {
                Fatal(AbsoluteJudgementFatalPredicate::UnresolvedCapacityExhausted,
                      AbsoluteJudgementFatalReason::RetainedHistoryLost, {unresolved_.size(), unresolved_size_});
            }
            if (status.depth == 0)
            {
                break;
            }
            if (count == 0)
            {
                Fatal(AbsoluteJudgementFatalPredicate::TransportDrainContradiction,
                      AbsoluteJudgementFatalReason::TransportSequenceError,
                      {count, requested, status.depth, status.next_sequence});
            }
        }
    }

    void JudgementScheduler::AccountCleanupDropsOrFatal() noexcept
    {
        gc::input::GameplayTransitionCutoff cutoff{};
        if (!gc::input::CaptureGameplayTransitionCutoff(stage_.cutoff().stage_entry_time, &cutoff))
        {
            const auto status = gc::input::ReadGameplayTransitionStatus();
            if (status.next_sequence == (std::numeric_limits<std::uint64_t>::max)())
            {
                Fatal(AbsoluteJudgementFatalPredicate::SequenceExhausted,
                      AbsoluteJudgementFatalReason::CheckedArithmeticFailure, {status.next_sequence});
            }
            if (status.eviction_count != stage_.cutoff().eviction_count)
            {
                Fatal(AbsoluteJudgementFatalPredicate::TransportEvicted,
                      AbsoluteJudgementFatalReason::TransportEviction,
                      {stage_.cutoff().eviction_count, status.eviction_count});
            }
            Fatal(AbsoluteJudgementFatalPredicate::InputTransportWorkerBecameInactive,
                  AbsoluteJudgementFatalReason::InputCapabilityUnavailable,
                  {status.enabled ? 1u : 0u, status.active ? 1u : 0u});
        }
        const auto& stage_cutoff = stage_.cutoff();
        if (cutoff.transport_epoch != stage_cutoff.transport_epoch)
        {
            Fatal(AbsoluteJudgementFatalPredicate::InputTransportEpochChanged,
                  AbsoluteJudgementFatalReason::TransportEpochLost,
                  {stage_cutoff.transport_epoch, cutoff.transport_epoch});
        }
        if (cutoff.qpc_frequency != stage_cutoff.qpc_frequency)
        {
            Fatal(AbsoluteJudgementFatalPredicate::InputQpcFrequencyChanged,
                  AbsoluteJudgementFatalReason::ClockDiscontinuous,
                  {
                      static_cast<std::uint64_t>(stage_cutoff.qpc_frequency),
                      static_cast<std::uint64_t>(cutoff.qpc_frequency)
                  });
        }
        if (cutoff.eviction_count != stage_cutoff.eviction_count)
        {
            Fatal(AbsoluteJudgementFatalPredicate::TransportEvicted, AbsoluteJudgementFatalReason::TransportEviction,
                  {stage_cutoff.eviction_count, cutoff.eviction_count});
        }
        if (cutoff.first_stage_sequence < stage_cutoff.first_stage_sequence)
        {
            Fatal(AbsoluteJudgementFatalPredicate::TransportSequenceDiscontinuous,
                  AbsoluteJudgementFatalReason::TransportSequenceError,
                  {stage_cutoff.first_stage_sequence, cutoff.first_stage_sequence});
        }

        auto& diagnostics = JudgementDiagnostics();
        auto& counters = diagnostics.stage_counters();
        counters.post_cutoff_records = cutoff.first_stage_sequence - stage_cutoff.first_stage_sequence;

        std::uint64_t already_classified{};
        const auto add_classified = [&already_classified](const std::uint64_t value) noexcept
        {
            already_classified = value <= (std::numeric_limits<std::uint64_t>::max)() - already_classified
                                     ? already_classified + value
                                     : (std::numeric_limits<std::uint64_t>::max)();
        };
        add_classified(counters.event_scopes);
        add_classified(counters.late_records);
        add_classified(counters.overload_drops);
        counters.cleanup_drops =
            counters.post_cutoff_records >= already_classified ? counters.post_cutoff_records - already_classified : 0;

        unresolved_read_slot_ = 0;
        unresolved_size_ = 0;
        pending_event_count_ = 0;
        marked_overload_count_ = 0;
        next_drain_sequence_ = cutoff.first_stage_sequence;
        next_delivery_sequence_ = cutoff.first_stage_sequence;
        ApplyHistoryResultOrFatal(
            history_.Reset(cutoff.transport_epoch, cutoff.first_stage_sequence, cutoff.held_baseline));
        diagnostics.ObserveTransportPendingDepth(0);
        diagnostics.SetPendingWork(0);
        diagnostics.CheckFinalTransportIdentity();
    }

    JudgementClockStatus JudgementScheduler::ResolveUnresolvedPrefixOrFatal() noexcept
    {
        if (!clock_resolver_.bound())
        {
            return JudgementClockStatus::Pending;
        }

        auto& counters = JudgementDiagnostics().stage_counters();
        while (unresolved_size_ != 0)
        {
            const auto& record = UnresolvedFront();
            IncrementDiagnostic(counters.exact_clock_reads);
            const auto resolved =
                clock_resolver_.Resolve(record.observed_time, gc::audio::ExactClockResolveIntent::FinalizedTimestamp);
            last_qpc_ = record.observed_time.qpc_ticks;
            last_output_frame_ = resolved.output_frame;
            last_j_ = resolved.judgement_seconds;
            if (resolved.endpoint_anchor_sequence != 0)
            {
                last_anchor_sequence_ = resolved.endpoint_anchor_sequence;
            }
            if (resolved.endpoint_position)
            {
                last_endpoint_position_ = resolved.endpoint_position;
            }
            if (resolved.status == JudgementClockStatus::Pending)
            {
                IncrementDiagnostic(counters.pending_clock_reads);
                return JudgementClockStatus::Pending;
            }
            if (resolved.status == JudgementClockStatus::TemporarilyUnavailable)
            {
                IncrementDiagnostic(counters.unavailable_clock_reads);
                return JudgementClockStatus::TemporarilyUnavailable;
            }
            if (resolved.status != JudgementClockStatus::Resolved || !resolved.judgement_seconds)
            {
                FailForClockResult(resolved);
            }

            IncrementDiagnostic(counters.resolved_clock_reads);
            const JudgementScopeCoordinate coordinate{
                .judgement_seconds = *resolved.judgement_seconds,
                .sequence = record.sequence,
            };
            if (last_resolved_coordinate_)
            {
                const auto order = coordinate.judgement_seconds.Compare(last_resolved_coordinate_->judgement_seconds);
                if (order < 0 || (order == 0 && coordinate.sequence <= last_resolved_coordinate_->sequence))
                {
                    Fatal(AbsoluteJudgementFatalPredicate::ResolvedCoordinateRegressed,
                          AbsoluteJudgementFatalReason::BackwardTime,
                          {last_resolved_coordinate_->sequence, coordinate.sequence});
                }
            }
            last_resolved_coordinate_ = coordinate;

            if (IsBehindCommittedFrontier(coordinate))
            {
                if (next_delivery_sequence_ != record.sequence)
                {
                    Fatal(AbsoluteJudgementFatalPredicate::DeliveryOrderViolated,
                          AbsoluteJudgementFatalReason::CommittedOrderViolation,
                          {next_delivery_sequence_, record.sequence});
                }
                ApplyHistoryResultOrFatal(history_.AppendStateOnly(
                    {
                        .transport = record,
                        .judgement_seconds = *resolved.judgement_seconds,
                    },
                    StateOnlyReason::AcceptedLate));
                next_delivery_sequence_ = record.sequence + 1;
                IncrementDiagnostic(counters.late_records);
            }
            else
            {
                ApplyHistoryResultOrFatal(history_.Append({
                    .transport = record,
                    .judgement_seconds = *resolved.judgement_seconds,
                }));
                IncrementDiagnostic(pending_event_count_);
                JudgementDiagnostics().ObserveEventBacklog(pending_event_count_);
            }
            PopUnresolved();
        }
        return JudgementClockStatus::Resolved;
    }

    void JudgementScheduler::TryActivateOrWait(const JudgementClockResult& entry_clock) noexcept
    {
        if (stage_.active())
        {
            return;
        }
        if (entry_clock.status == JudgementClockStatus::Pending ||
            entry_clock.status == JudgementClockStatus::TemporarilyUnavailable)
        {
            IncrementDiagnostic(accumulated_clock_waits_);
            return;
        }
        if (entry_clock.status != JudgementClockStatus::Resolved || !entry_clock.judgement_seconds ||
            !clock_resolver_.bound())
        {
            FailForClockResult(entry_clock);
        }

        const auto scaled = entry_clock.judgement_seconds->Multiply(60, 1);
        const auto ceiling = scaled
                                 ? scaled->Ceil()
                                 : std::expected<std::int64_t, gc::timing::RationalError>(
                                     std::unexpected(gc::timing::RationalError::Overflow));
        if (!scaled || !ceiling || *ceiling == (std::numeric_limits<std::int64_t>::min)())
        {
            Fatal(AbsoluteJudgementFatalPredicate::RationalOperationUnrepresentable,
                  AbsoluteJudgementFatalReason::CheckedArithmeticFailure, {1, 60, 1});
        }
        const auto first_pending_boundary = BoundaryAt(*ceiling);
        const auto first_pending_native =
            first_pending_boundary ? NativeArguments(*first_pending_boundary) : std::nullopt;
        committed_boundary_index_ = *ceiling - 1;
        has_committed_boundary_index_ = true;
        stage_.Activate();
        if (!stage_.active())
        {
            Fatal(AbsoluteJudgementFatalPredicate::CommitTopologyMismatch,
                  AbsoluteJudgementFatalReason::NativeStateMismatch,
                  {
                      stage_.open() ? 1u : 0u, stage_.bound() ? 1u : 0u, stage_.endpoint_generation(),
                      stage_.active() ? 1u : 0u
                  });
        }
        last_j_ = entry_clock.judgement_seconds;
        JudgementDiagnostics().SeedHeartbeatIndex(committed_boundary_index_);
        const auto& anchor = clock_resolver_.anchor();
        const auto provider_info = anchor.endpoint->info();
        const auto provider_counters = anchor.endpoint->counters();
        JudgementDiagnostics().LogAbsoluteStageActivation({
            .native =
            {
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
            .provider_domain = gc::audio::ExactOutputClockDomainName(provider_info.domain),
            .endpoint_qpc_frequency = provider_info.qpc_frequency,
            .provider_output_rate = provider_info.output_sample_rate,
            .provider_period_frames = provider_info.period_frames,
            .provider_output_latency_frames = provider_info.output_latency_frames,
            .provider_timestamp_quantum_ns = provider_info.timestamp_quantum_ns,
            .provider_publication_count = provider_counters.publication_count,
            .buffer_instance_id = anchor.buffer_instance_id,
            .playback_generation = anchor.playback_generation,
            .output_origin = anchor.output_origin,
            .source_origin = anchor.source_origin,
            .output_rate = anchor.output_rate,
            .source_rate = anchor.source_rate,
            .initial_j = *entry_clock.judgement_seconds,
            .committed_boundary_seed = committed_boundary_index_,
            .first_pending_boundary_index = *ceiling,
            .first_pending_boundary_j = first_pending_boundary,
            .first_pending_boundary_native_ms =
            first_pending_native ? std::optional<std::int32_t>(first_pending_native->first) : std::nullopt,
            .first_pending_boundary_native_frame =
            first_pending_native ? std::optional<std::int32_t>(first_pending_native->second) : std::nullopt,
            .pending_negative_boundary_count = *ceiling < 0 ? static_cast<std::uint64_t>(-*ceiling) : 0,
            .game_time_offset_ms = anchor.game_time_offset_ms,
            .hold_safe_frame = stage_.native().hold_safe_frame,
            .slide_hold_safe_frame = stage_.native().slide_hold_safe_frame,
            .accumulated_clock_waits = accumulated_clock_waits_,
        });
    }

    void JudgementScheduler::SelectOuterHorizonOrFatal(const AbsoluteJudgementOuterProbe& probe) noexcept
    {
        if (!stage_.active())
        {
            return;
        }

        auto& counters = JudgementDiagnostics().stage_counters();
        IncrementDiagnostic(counters.exact_clock_reads);
        last_qpc_ = probe.now.qpc_ticks;
        const auto current = clock_resolver_.Resolve(probe.now, gc::audio::ExactClockResolveIntent::ProvisionalHorizon);
        last_output_frame_ = current.output_frame;
        last_j_ = current.judgement_seconds;
        if (current.endpoint_anchor_sequence != 0)
        {
            last_anchor_sequence_ = current.endpoint_anchor_sequence;
        }
        if (current.endpoint_position)
        {
            last_endpoint_position_ = current.endpoint_position;
        }
        if (current.status == JudgementClockStatus::TemporarilyUnavailable)
        {
            IncrementDiagnostic(counters.unavailable_clock_reads);
            return;
        }
        if (current.status == JudgementClockStatus::Pending)
        {
            IncrementDiagnostic(counters.pending_clock_reads);
            return;
        }
        if (current.status != JudgementClockStatus::Resolved || !current.judgement_seconds)
        {
            FailForClockResult(current);
        }

        IncrementDiagnostic(counters.resolved_clock_reads);
        SetReadyHorizonOrFatal(*current.judgement_seconds);
    }

    void JudgementScheduler::SetReadyHorizonOrFatal(const CheckedRational& ready) noexcept
    {
        if (committed_frontier_ && ready.Compare(committed_frontier_->judgement_seconds) < 0)
        {
            Fatal(AbsoluteJudgementFatalPredicate::EndpointProjectionDiscontinuous,
                  AbsoluteJudgementFatalReason::ClockDiscontinuous, {static_cast<std::uint64_t>(last_qpc_)});
        }
        if (!has_committed_boundary_index_)
        {
            Fatal(AbsoluteJudgementFatalPredicate::CommitTopologyMismatch,
                  AbsoluteJudgementFatalReason::HeartbeatFrontierViolation, {0, 0, 0, 0});
        }
        MarkReadyOverloadOrFatal(ready);
        const auto scaled = ready.Multiply(60, 1);
        const auto target = scaled
                                ? scaled->Floor()
                                : std::expected<std::int64_t, gc::timing::RationalError>(
                                    std::unexpected(gc::timing::RationalError::Overflow));
        if (!scaled || !target)
        {
            Fatal(AbsoluteJudgementFatalPredicate::RationalOperationUnrepresentable,
                  AbsoluteJudgementFatalReason::CheckedArithmeticFailure, {2, 60, 1});
        }

        bool capped{};
        if (*target > committed_boundary_index_)
        {
            if (committed_boundary_index_ > (std::numeric_limits<std::int64_t>::max)() - 3)
            {
                Fatal(AbsoluteJudgementFatalPredicate::SequenceExhausted,
                      AbsoluteJudgementFatalReason::CheckedArithmeticFailure,
                      {static_cast<std::uint64_t>(committed_boundary_index_)});
            }
            capped = *target > committed_boundary_index_ + 3;
        }
        if (capped)
        {
            outer_horizon_ = BoundaryAt(committed_boundary_index_ + 3);
            if (!outer_horizon_)
            {
                Fatal(AbsoluteJudgementFatalPredicate::RationalOperationUnrepresentable,
                      AbsoluteJudgementFatalReason::CheckedArithmeticFailure,
                      {3, static_cast<std::uint64_t>(committed_boundary_index_ + 3), 60});
            }
        }
        else
        {
            outer_horizon_ = ready;
        }
    }

    void JudgementScheduler::MarkReadyOverloadOrFatal(const CheckedRational& ready) noexcept
    {
        const auto ready_count = history_.CountResolvedAtOrBefore(next_delivery_sequence_, ready);
        if (!ready_count)
        {
            FatalHistoryError(ready_count.error());
        }
        const auto required_marked =
            *ready_count > kProtectedReadyEventCount ? *ready_count - kProtectedReadyEventCount : 0;
        marked_overload_count_ = required_marked;
    }

    void JudgementScheduler::ConsumeMarkedOverloadOrFatal(const ResolvedGameplayTransition& event) noexcept
    {
        if (marked_overload_count_ == 0 || pending_event_count_ == 0 ||
            event.transport.sequence == (std::numeric_limits<std::uint64_t>::max)())
        {
            Fatal(AbsoluteJudgementFatalPredicate::DeliveryOrderViolated,
                  AbsoluteJudgementFatalReason::CommittedOrderViolation,
                  {next_delivery_sequence_, event.transport.sequence});
        }
        ApplyHistoryResultOrFatal(
            history_.ConvertResolvedToStateOnly(event.transport.sequence, StateOnlyReason::Overload));
        next_delivery_sequence_ = event.transport.sequence + 1;
        --pending_event_count_;
        --marked_overload_count_;

        auto& counters = JudgementDiagnostics().stage_counters();
        const bool first_drop = counters.overload_drops == 0;
        IncrementDiagnostic(counters.overload_drops);
        if (first_drop)
        {
            counters.first_overload_drop_sequence = event.transport.sequence;
        }
        counters.last_overload_drop_sequence = event.transport.sequence;
        JudgementDiagnostics().SetPendingWork(PendingWorkCount());
    }

    std::optional<ScheduledJudgementScope> JudgementScheduler::NextScope() noexcept
    {
        if (!outer_prepared_ || !stage_.active() || !outer_horizon_)
        {
            return std::nullopt;
        }
        if (outstanding_scope_)
        {
            return outstanding_scope_;
        }
        if (outer_event_scope_count_ != 0 || outer_heartbeat_scope_count_ >= 3)
        {
            return std::nullopt;
        }
        if (!has_committed_boundary_index_ || committed_boundary_index_ == (std::numeric_limits<std::int64_t>::max)())
        {
            Fatal(AbsoluteJudgementFatalPredicate::CommitTopologyMismatch,
                  AbsoluteJudgementFatalReason::HeartbeatFrontierViolation,
                  {
                      has_committed_boundary_index_ ? 1u : 0u, static_cast<std::uint64_t>(committed_boundary_index_),
                      outer_event_scope_count_, outer_heartbeat_scope_count_
                  });
        }

        for (;;)
        {
            const auto boundary_index = committed_boundary_index_ + 1;
            const auto boundary = BoundaryAt(boundary_index);
            if (!boundary)
            {
                Fatal(AbsoluteJudgementFatalPredicate::RationalOperationUnrepresentable,
                      AbsoluteJudgementFatalReason::CheckedArithmeticFailure,
                      {4, static_cast<std::uint64_t>(boundary_index), 60});
            }
            const auto* event = history_.FirstResolvedAtOrAfter(next_delivery_sequence_);
            const bool event_due = event != nullptr && event->judgement_seconds.Compare(*outer_horizon_) <= 0;
            const bool boundary_due = boundary->Compare(*outer_horizon_) <= 0;
            if (!event_due && !boundary_due)
            {
                return std::nullopt;
            }

            const bool event_is_next = event_due && (!boundary_due || event->judgement_seconds.Compare(*boundary) <= 0);
            if (event_is_next && marked_overload_count_ != 0)
            {
                ConsumeMarkedOverloadOrFatal(*event);
                continue;
            }
            if (event_is_next && outer_heartbeat_scope_count_ != 0)
            {
                if (!outer_event_barrier_recorded_)
                {
                    IncrementDiagnostic(JudgementDiagnostics().stage_counters().event_barrier_deferrals);
                    outer_event_barrier_recorded_ = true;
                }
                return std::nullopt;
            }

            std::optional<ScheduledJudgementScope> result =
                event_is_next ? MakeEventScope(*event, *boundary) : MakeHeartbeatScope(*boundary);
            if (!result)
            {
                Fatal(AbsoluteJudgementFatalPredicate::RationalOperationUnrepresentable,
                      AbsoluteJudgementFatalReason::CheckedArithmeticFailure,
                      {5, event_is_next ? 1u : 0u, static_cast<std::uint64_t>(boundary_index)});
            }
            outstanding_scope_ = *result;
            return result;
        }
    }

    std::optional<ScheduledJudgementScope> JudgementScheduler::MakeEventScope(
        const ResolvedGameplayTransition& event, const CheckedRational& boundary) const noexcept
    {
        if (event.transport.sequence == (std::numeric_limits<std::uint64_t>::max)())
        {
            return std::nullopt;
        }
        const auto native = NativeArguments(event.judgement_seconds);
        if (!native)
        {
            return std::nullopt;
        }
        bool commits_boundary{};
        if (event.judgement_seconds.Compare(boundary) == 0)
        {
            const auto* next = history_.FirstResolvedAtOrAfter(event.transport.sequence + 1);
            commits_boundary = next == nullptr || next->judgement_seconds.Compare(boundary) != 0;
        }
        return ScheduledJudgementScope{
            .kind = JudgementScopeKind::Event,
            .coordinate =
            {
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

    std::optional<ScheduledJudgementScope> JudgementScheduler::MakeHeartbeatScope(
        const CheckedRational& boundary) const noexcept
    {
        const auto native = NativeArguments(boundary);
        if (!native)
        {
            return std::nullopt;
        }
        return ScheduledJudgementScope{
            .kind = JudgementScopeKind::Heartbeat,
            .coordinate =
            {
                .judgement_seconds = boundary,
                .sequence = (std::numeric_limits<std::uint64_t>::max)(),
            },
            .native_ms = native->first,
            .native_frame = native->second,
            .history_prefix_end_sequence = CurrentHistoryPrefixEnd(),
            .commits_boundary = true,
        };
    }

    void JudgementScheduler::CommitScope(const ScheduledJudgementScope& scope) noexcept
    {
        if (!outer_prepared_ || !outstanding_scope_ || !SameScope(scope, *outstanding_scope_))
        {
            Fatal(AbsoluteJudgementFatalPredicate::ScopeLifetimeMismatch,
                  AbsoluteJudgementFatalReason::CommittedOrderViolation,
                  {outer_prepared_ ? 1u : 0u, outstanding_scope_ ? 1u : 0u});
        }

        auto& diagnostics = JudgementDiagnostics();
        auto& counters = diagnostics.stage_counters();
        if (scope.kind == JudgementScopeKind::Event)
        {
            if (scope.event == nullptr || pending_event_count_ == 0 ||
                scope.event->transport.sequence != scope.coordinate.sequence ||
                scope.event->transport.sequence == (std::numeric_limits<std::uint64_t>::max)())
            {
                Fatal(AbsoluteJudgementFatalPredicate::DeliveryOrderViolated,
                      AbsoluteJudgementFatalReason::CommittedOrderViolation,
                      {next_delivery_sequence_, scope.coordinate.sequence});
            }
            next_delivery_sequence_ = scope.event->transport.sequence + 1;
            --pending_event_count_;
            IncrementDiagnostic(counters.event_scopes);
            IncrementDiagnostic(outer_event_scope_count_);
            if (outer_now_qpc_ >= scope.event->transport.observed_time.qpc_ticks)
            {
                diagnostics.ObserveDeliveryDelayQpc(
                    static_cast<std::uint64_t>(outer_now_qpc_ - scope.event->transport.observed_time.qpc_ticks));
            }
        }
        else
        {
            IncrementDiagnostic(counters.heartbeat_scopes);
            IncrementDiagnostic(outer_heartbeat_scope_count_);
        }

        diagnostics.CheckAndRecordCommittedOrderOrFatal(scope.coordinate.judgement_seconds, scope.coordinate.sequence,
                                                        FatalSnapshot());
        committed_frontier_ = scope.coordinate;
        committed_frontier_is_boundary_ = scope.commits_boundary;

        if (scope.commits_boundary)
        {
            if (committed_boundary_index_ == (std::numeric_limits<std::int64_t>::max)())
            {
                Fatal(AbsoluteJudgementFatalPredicate::SequenceExhausted,
                      AbsoluteJudgementFatalReason::HeartbeatFrontierViolation,
                      {static_cast<std::uint64_t>(committed_boundary_index_)});
            }
            ++committed_boundary_index_;
            diagnostics.CheckAndRecordHeartbeatIndexOrFatal(committed_boundary_index_, true, FatalSnapshot());
            IncrementDiagnostic(counters.committed_boundaries);
            if (scope.kind == JudgementScopeKind::Event)
            {
                IncrementDiagnostic(counters.equal_boundary_substitutions);
            }
        }

        ApplyHistoryResultOrFatal(history_.PruneBefore(scope.coordinate.judgement_seconds, CurrentHistoryPrefixEnd()));
        outstanding_scope_.reset();
        IncrementDiagnostic(outer_scope_count_);
        diagnostics.SetPendingWork(PendingWorkCount());
    }

    void JudgementScheduler::FinishOuterCall() noexcept
    {
        if (!stage_.open() || !outer_prepared_)
        {
            return;
        }
        if (outstanding_scope_)
        {
            Fatal(AbsoluteJudgementFatalPredicate::ScopeLifetimeMismatch,
                  AbsoluteJudgementFatalReason::ScopeLifetimeViolation, {outstanding_scope_->coordinate.sequence});
        }
        auto& diagnostics = JudgementDiagnostics();
        if (outer_scope_count_ != 0)
        {
            auto& counters = diagnostics.stage_counters();
            if (outer_event_scope_count_ == 1 && outer_heartbeat_scope_count_ == 0)
            {
                IncrementDiagnostic(counters.event_only_batches);
            }
            else if (outer_event_scope_count_ == 0 && outer_heartbeat_scope_count_ >= 1 &&
                outer_heartbeat_scope_count_ <= 3)
            {
                IncrementDiagnostic(counters.heartbeat_only_batches);
            }
            else
            {
                IncrementDiagnostic(counters.mixed_event_batches);
                Fatal(AbsoluteJudgementFatalPredicate::CommitTopologyMismatch,
                      AbsoluteJudgementFatalReason::NativeCallCountMismatch,
                      {
                          outer_scope_count_, outer_event_scope_count_, outer_heartbeat_scope_count_,
                          counters.mixed_event_batches
                      });
            }
            diagnostics.RecordBatch(outer_scope_count_);
            diagnostics.CheckCompletedBatchInvariantOrFatal(FatalSnapshot());
        }
        diagnostics.ObserveBacklog(PendingWorkCount());
        diagnostics.SetPendingWork(PendingWorkCount());
        diagnostics.MaybeLogPeriodicDiagnostics(RuntimeSnapshot());
        outer_horizon_.reset();
        outer_scope_count_ = 0;
        outer_event_scope_count_ = 0;
        outer_heartbeat_scope_count_ = 0;
        outer_prepared_ = false;
        outer_event_barrier_recorded_ = false;
    }

    void JudgementScheduler::CheckNativeCallInvariantOrFatal() const noexcept
    {
        JudgementDiagnostics().CheckNativeCallInvariantOrFatal(FatalSnapshot());
    }

    AbsoluteJudgementScoreDeltas
    // These forwarding methods remain instance-level scheduler operations.
    // ReSharper disable once CppMemberFunctionMayBeStatic
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    JudgementScheduler::ObserveNativeScoreCounters(const AbsoluteJudgementNativeScoreCounters& counters) const noexcept
    {
        return JudgementDiagnostics().ObserveNativeScoreCounters(counters);
    }

    // These forwarding methods remain instance-level scheduler operations.
    // ReSharper disable once CppMemberFunctionMayBeStatic
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    void JudgementScheduler::AccumulateQueryCounters(const AbsoluteJudgementQueryCounters& counters) const noexcept
    {
        JudgementDiagnostics().AccumulateQueryCounters(counters);
    }

    // These forwarding methods remain instance-level scheduler operations.
    // ReSharper disable once CppMemberFunctionMayBeStatic
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    void JudgementScheduler::RecordTransientPublications(
        const AbsoluteJudgementTransientPublications& publications) const noexcept
    {
        JudgementDiagnostics().RecordTransientPublications(publications);
    }

    [[noreturn]] void JudgementScheduler::FailActiveStage(
        const AbsoluteJudgementFatalPredicate predicate, const AbsoluteJudgementFatalReason category,
        const std::initializer_list<std::uint64_t> operands) const noexcept
    {
        Fatal(predicate, category, operands);
    }

    [[noreturn]] void JudgementScheduler::FailHistoryInvariant(const JudgementHistoryError error) const noexcept
    {
        FatalHistoryError(error);
    }

    std::uint64_t JudgementScheduler::CurrentHistoryPrefixEnd() const noexcept
    {
        const auto* next = history_.FirstResolvedAtOrAfter(next_delivery_sequence_);
        return next != nullptr ? next->transport.sequence : history_.next_sequence();
    }

    bool JudgementScheduler::IsBehindCommittedFrontier(const JudgementScopeCoordinate& coordinate) const noexcept
    {
        if (!committed_frontier_)
        {
            return false;
        }
        const auto order = coordinate.judgement_seconds.Compare(committed_frontier_->judgement_seconds);
        if (order != 0)
        {
            return order < 0;
        }
        return committed_frontier_is_boundary_ || coordinate.sequence <= committed_frontier_->sequence;
    }

    std::optional<CheckedRational> JudgementScheduler::BoundaryAt(const std::int64_t index) noexcept
    {
        const auto result = CheckedRational::Create(index, 60);
        if (!result)
        {
            return std::nullopt;
        }
        return *result;
    }

    std::optional<std::pair<std::int32_t, std::int32_t>> JudgementScheduler::NativeArguments(
        const CheckedRational& judgement_seconds) noexcept
    {
        const auto milliseconds = judgement_seconds.Multiply(1000, 1);
        const auto native_ms = milliseconds
                                   ? milliseconds->Truncate()
                                   : std::expected<std::int64_t, gc::timing::RationalError>(
                                       std::unexpected(gc::timing::RationalError::Overflow));
        const auto frames = judgement_seconds.Multiply(60, 1);
        const auto native_frame = frames
                                      ? frames->Floor()
                                      : std::expected<std::int64_t, gc::timing::RationalError>(
                                          std::unexpected(gc::timing::RationalError::Overflow));
        if (!milliseconds || !native_ms || !frames || !native_frame ||
            *native_ms < (std::numeric_limits<std::int32_t>::min)() ||
            *native_ms > (std::numeric_limits<std::int32_t>::max)() ||
            *native_frame < (std::numeric_limits<std::int32_t>::min)() ||
            *native_frame > (std::numeric_limits<std::int32_t>::max)())
        {
            return std::nullopt;
        }
        return std::pair{
            static_cast<std::int32_t>(*native_ms),
            static_cast<std::int32_t>(*native_frame),
        };
    }

    void JudgementScheduler::AppendUnresolvedOrFatal(const gc::input::GameplayTransitionRecord& record) noexcept
    {
        if (unresolved_size_ == unresolved_.size())
        {
            Fatal(AbsoluteJudgementFatalPredicate::UnresolvedCapacityExhausted,
                  AbsoluteJudgementFatalReason::RetainedHistoryLost, {unresolved_.size(), unresolved_size_});
        }
        const auto write_slot = (unresolved_read_slot_ + unresolved_size_) % unresolved_.size();
        unresolved_[write_slot] = record;
        ++unresolved_size_;
    }

    gc::input::GameplayTransitionRecord& JudgementScheduler::UnresolvedFront() noexcept
    {
        if (unresolved_size_ == 0)
        {
            Fatal(AbsoluteJudgementFatalPredicate::UnresolvedFrontEmpty,
                  AbsoluteJudgementFatalReason::RetainedHistoryLost);
        }
        return unresolved_[unresolved_read_slot_];
    }

    void JudgementScheduler::PopUnresolved() noexcept
    {
        if (unresolved_size_ == 0)
        {
            Fatal(AbsoluteJudgementFatalPredicate::UnresolvedFrontEmpty,
                  AbsoluteJudgementFatalReason::RetainedHistoryLost);
        }
        unresolved_read_slot_ = (unresolved_read_slot_ + 1) % unresolved_.size();
        --unresolved_size_;
    }

    void JudgementScheduler::ApplyHistoryResultOrFatal(
        const std::expected<void, JudgementHistoryError>& result) const noexcept
    {
        if (!result)
        {
            FatalHistoryError(result.error());
        }
    }

    void JudgementScheduler::IncrementDiagnostic(std::uint64_t& value) noexcept
    {
        if (value != (std::numeric_limits<std::uint64_t>::max)())
        {
            ++value;
        }
    }

    void JudgementScheduler::FailForClockResult(const JudgementClockResult& result) const noexcept
    {
        switch (result.failure)
        {
        case JudgementClockFailure::InvalidStageBinding:
            Fatal(AbsoluteJudgementFatalPredicate::CommitTopologyMismatch,
                  AbsoluteJudgementFatalReason::NativeStateMismatch,
                  {
                      stage_.generation(), static_cast<std::uint64_t>(stage_.cutoff().stage_entry_time.qpc_ticks),
                      clock_resolver_.bound() ? 1u : 0u, stage_.active() ? 1u : 0u
                  });
        case JudgementClockFailure::EndpointProviderChanged:
            Fatal(AbsoluteJudgementFatalPredicate::EndpointProviderIdentityChanged,
                  AbsoluteJudgementFatalReason::EndpointGenerationChanged);
        case JudgementClockFailure::EndpointGenerationChanged:
            Fatal(AbsoluteJudgementFatalPredicate::EndpointGenerationChanged,
                  AbsoluteJudgementFatalReason::EndpointGenerationChanged, {stage_.endpoint_generation(), 0});
        case JudgementClockFailure::PlaybackHistoryObjectChangedBeforeAnchor:
            Fatal(AbsoluteJudgementFatalPredicate::PlaybackHistoryObjectChangedBeforeAnchor,
                  AbsoluteJudgementFatalReason::ClockDiscontinuous);
        case JudgementClockFailure::PlaybackHistoryEndpointChangedBeforeAnchor:
            Fatal(AbsoluteJudgementFatalPredicate::PlaybackHistoryEndpointChangedBeforeAnchor,
                  AbsoluteJudgementFatalReason::EndpointGenerationChanged);
        case JudgementClockFailure::StageOriginHistoryLost:
            Fatal(AbsoluteJudgementFatalPredicate::StageOriginHistoryLost,
                  AbsoluteJudgementFatalReason::ClockHistoryLost);
        case JudgementClockFailure::EndpointProjectionDiscontinuous:
        case JudgementClockFailure::InvalidClockRates:
            Fatal(AbsoluteJudgementFatalPredicate::EndpointProjectionDiscontinuous,
                  AbsoluteJudgementFatalReason::ClockDiscontinuous, {static_cast<std::uint64_t>(last_qpc_)});
        case JudgementClockFailure::RationalOperationUnrepresentable:
            Fatal(AbsoluteJudgementFatalPredicate::RationalOperationUnrepresentable,
                  AbsoluteJudgementFatalReason::CheckedArithmeticFailure,
                  {6, static_cast<std::uint64_t>(last_qpc_), 0});
        case JudgementClockFailure::None:
            break;
        }
        Fatal(AbsoluteJudgementFatalPredicate::FatalRecordInvalid, AbsoluteJudgementFatalReason::NativeStateMismatch,
              {static_cast<std::uint64_t>(result.status), static_cast<std::uint64_t>(result.failure)});
    }

    [[noreturn]] void JudgementScheduler::FatalStageError(const JudgementStageError error,
                                                          const NativeJudgementIdentity* const observed) const noexcept
    {
        const auto actual = observed != nullptr ? *observed : NativeJudgementIdentity{};
        switch (error)
        {
        case JudgementStageError::AlreadyOpen:
            Fatal(AbsoluteJudgementFatalPredicate::SemanticStageAlreadyOpen,
                  AbsoluteJudgementFatalReason::NativeStateMismatch, {stage_.generation(), stage_.tune_manager()});
        case JudgementStageError::GenerationExhausted:
            Fatal(AbsoluteJudgementFatalPredicate::StageGenerationExhausted,
                  AbsoluteJudgementFatalReason::CheckedArithmeticFailure, {stage_.generation()});
        case JudgementStageError::TuneManagerMissing:
        case JudgementStageError::TuneMissing:
            Fatal(AbsoluteJudgementFatalPredicate::TuneMissing, AbsoluteJudgementFatalReason::NativeStateMismatch,
                  {actual.tune_manager, actual.tune});
        case JudgementStageError::InputTransportInactiveAtStageEntry:
            {
                const auto& status = stage_.failure_transport_status();
                Fatal(AbsoluteJudgementFatalPredicate::InputTransportInactiveAtStageEntry,
                      AbsoluteJudgementFatalReason::InputCapabilityUnavailable,
                      {status.enabled ? 1u : 0u, status.active ? 1u : 0u});
            }
        case JudgementStageError::InputSequenceExhausted:
            Fatal(AbsoluteJudgementFatalPredicate::SequenceExhausted,
                  AbsoluteJudgementFatalReason::CheckedArithmeticFailure,
                  {(std::numeric_limits<std::uint64_t>::max)()});
        case JudgementStageError::InputQpcFrequencyInvalidAtStageEntry:
            Fatal(AbsoluteJudgementFatalPredicate::InputQpcFrequencyInvalidAtStageEntry,
                  AbsoluteJudgementFatalReason::InputCapabilityUnavailable,
                  {static_cast<std::uint64_t>(stage_.cutoff().qpc_frequency)});
        case JudgementStageError::StageNotOpen:
            Fatal(AbsoluteJudgementFatalPredicate::SemanticStageMissingAtOwnedLoop,
                  AbsoluteJudgementFatalReason::NativeStateMismatch);
        case JudgementStageError::StageGenerationChanged:
            Fatal(AbsoluteJudgementFatalPredicate::ScopeGenerationMismatch,
                  AbsoluteJudgementFatalReason::NativeIdentityChanged, {stage_.generation(), actual.stage_generation});
        case JudgementStageError::TuneManagerChanged:
            Fatal(AbsoluteJudgementFatalPredicate::SemanticStageReceiverMismatch,
                  AbsoluteJudgementFatalReason::NativeIdentityChanged, {stage_.tune_manager(), actual.tune_manager});
        case JudgementStageError::JudgementStateMissing:
            Fatal(AbsoluteJudgementFatalPredicate::JudgementStateMissing,
                  AbsoluteJudgementFatalReason::NativeStateMismatch, {actual.tune, actual.player});
        case JudgementStageError::ScoreStateMissing:
            Fatal(AbsoluteJudgementFatalPredicate::ScoreStateMissing, AbsoluteJudgementFatalReason::NativeStateMismatch,
                  {actual.tune, actual.player});
        case JudgementStageError::BoosterMissing:
            Fatal(AbsoluteJudgementFatalPredicate::BoosterMissing, AbsoluteJudgementFatalReason::NativeStateMismatch,
                  {0, actual.booster});
        case JudgementStageError::TuneChanged:
            Fatal(AbsoluteJudgementFatalPredicate::TuneIdentityChanged,
                  AbsoluteJudgementFatalReason::NativeIdentityChanged, {stage_.native().tune, actual.tune});
        case JudgementStageError::JudgementStateChanged:
            Fatal(AbsoluteJudgementFatalPredicate::JudgementStateIdentityChanged,
                  AbsoluteJudgementFatalReason::NativeIdentityChanged,
                  {stage_.native().judgement_state, actual.judgement_state});
        case JudgementStageError::ScoreStateChanged:
            Fatal(AbsoluteJudgementFatalPredicate::ScoreStateIdentityChanged,
                  AbsoluteJudgementFatalReason::NativeIdentityChanged,
                  {stage_.native().score_state, actual.score_state});
        case JudgementStageError::BoosterChanged:
            Fatal(AbsoluteJudgementFatalPredicate::BoosterIdentityChanged,
                  AbsoluteJudgementFatalReason::NativeIdentityChanged, {stage_.native().booster, actual.booster});
        case JudgementStageError::PlayerChanged:
            Fatal(AbsoluteJudgementFatalPredicate::PlayerIdentityChanged,
                  AbsoluteJudgementFatalReason::NativeIdentityChanged, {stage_.native().player, actual.player});
        case JudgementStageError::EndpointGenerationChanged:
            Fatal(AbsoluteJudgementFatalPredicate::EndpointGenerationChanged,
                  AbsoluteJudgementFatalReason::EndpointGenerationChanged, {stage_.endpoint_generation(), 0});
        case JudgementStageError::QpcFrequencyChanged:
            Fatal(AbsoluteJudgementFatalPredicate::EndpointQpcFrequencyMismatch,
                  AbsoluteJudgementFatalReason::ClockDiscontinuous,
                  {static_cast<std::uint64_t>(stage_.cutoff().qpc_frequency), 0});
        case JudgementStageError::HoldSafeFrameNonZero:
            Fatal(AbsoluteJudgementFatalPredicate::HoldSafeFrameNonZero, AbsoluteJudgementFatalReason::SafeFrameChanged,
                  {static_cast<std::uint64_t>(static_cast<std::uint32_t>(actual.hold_safe_frame))});
        case JudgementStageError::SlideHoldSafeFrameNonZero:
            Fatal(AbsoluteJudgementFatalPredicate::SlideHoldSafeFrameNonZero,
                  AbsoluteJudgementFatalReason::SafeFrameChanged,
                  {static_cast<std::uint64_t>(static_cast<std::uint32_t>(actual.slide_hold_safe_frame))});
        }
        Fatal(AbsoluteJudgementFatalPredicate::FatalRecordInvalid, AbsoluteJudgementFatalReason::NativeStateMismatch,
              {static_cast<std::uint64_t>(error)});
    }

    [[noreturn]] void JudgementScheduler::FatalHistoryError(const JudgementHistoryError error) const noexcept
    {
        const auto& operands = history_.last_failure_operands();
        switch (error)
        {
        case JudgementHistoryError::NotInitialized:
            Fatal(AbsoluteJudgementFatalPredicate::HistoryNotInitialized,
                  AbsoluteJudgementFatalReason::NativeStateMismatch);
        case JudgementHistoryError::BaselineMaskInvalid:
            Fatal(AbsoluteJudgementFatalPredicate::HistoryBaselineMaskInvalid,
                  AbsoluteJudgementFatalReason::NativeStateMismatch, {operands[0], operands[1]});
        case JudgementHistoryError::TransportEpochMismatch:
            Fatal(AbsoluteJudgementFatalPredicate::InputTransportEpochChanged,
                  AbsoluteJudgementFatalReason::TransportEpochLost, {operands[0], operands[1]});
        case JudgementHistoryError::SequenceDiscontinuity:
            Fatal(AbsoluteJudgementFatalPredicate::TransportSequenceDiscontinuous,
                  AbsoluteJudgementFatalReason::TransportSequenceError, {operands[0], operands[1]});
        case JudgementHistoryError::SequenceExhausted:
            Fatal(AbsoluteJudgementFatalPredicate::SequenceExhausted,
                  AbsoluteJudgementFatalReason::CheckedArithmeticFailure, {operands[0]});
        case JudgementHistoryError::TransportStateMismatch:
            Fatal(AbsoluteJudgementFatalPredicate::TransportMaskMismatch,
                  AbsoluteJudgementFatalReason::TransportSequenceError, {operands[0], operands[1]});
        case JudgementHistoryError::BackwardTime:
            Fatal(AbsoluteJudgementFatalPredicate::ResolvedCoordinateRegressed,
                  AbsoluteJudgementFatalReason::BackwardTime, {operands[0], operands[1]});
        case JudgementHistoryError::CapacityExhausted:
            Fatal(AbsoluteJudgementFatalPredicate::HistoryCapacityExhausted,
                  AbsoluteJudgementFatalReason::RetainedHistoryLost, {operands[0], operands[1]});
        case JudgementHistoryError::PrefixBeyondNext:
            Fatal(AbsoluteJudgementFatalPredicate::HistoryPrefixBeyondNext,
                  AbsoluteJudgementFatalReason::RetainedHistoryLost, {operands[0], operands[1]});
        case JudgementHistoryError::PromisedEntryMissing:
            Fatal(AbsoluteJudgementFatalPredicate::HistoryPromisedEntryMissing,
                  AbsoluteJudgementFatalReason::RetainedHistoryLost, {operands[0], operands[1], operands[2]});
        case JudgementHistoryError::InvalidControl:
            Fatal(AbsoluteJudgementFatalPredicate::HistoryControlInvalid,
                  AbsoluteJudgementFatalReason::NativeStateMismatch, {operands[0], operands[1]});
        case JudgementHistoryError::CheckedArithmeticFailure:
            Fatal(AbsoluteJudgementFatalPredicate::RationalOperationUnrepresentable,
                  AbsoluteJudgementFatalReason::CheckedArithmeticFailure, {7, 0, 0});
        }
        Fatal(AbsoluteJudgementFatalPredicate::FatalRecordInvalid, AbsoluteJudgementFatalReason::NativeStateMismatch,
              {static_cast<std::uint64_t>(error)});
    }

    AbsoluteJudgementRuntimeSnapshot JudgementScheduler::RuntimeSnapshot() const noexcept
    {
        return {
            .last_endpoint_anchor_sequence = last_anchor_sequence_,
            .last_endpoint_position = last_endpoint_position_,
            .last_output_frame = last_output_frame_,
            .last_qpc = last_qpc_,
            .last_j = last_j_,
            .committed_boundary = has_committed_boundary_index_ ? committed_boundary_index_ : 0,
            .pending_work = PendingWorkCount(),
            .last_sequence = next_drain_sequence_,
            .held_mask = history_.current_held(),
            .game_time_offset_ms = stage_.bound() ? stage_.native().game_time_offset_ms : 0,
            .hold_safe_frame = stage_.bound() ? stage_.native().hold_safe_frame : 0,
            .slide_hold_safe_frame = stage_.bound() ? stage_.native().slide_hold_safe_frame : 0,
        };
    }

    std::uint64_t JudgementScheduler::PendingWorkCount() const noexcept
    {
        const auto unresolved = static_cast<std::uint64_t>(unresolved_size_);
        if (pending_event_count_ > (std::numeric_limits<std::uint64_t>::max)() - unresolved)
        {
            return (std::numeric_limits<std::uint64_t>::max)();
        }
        return pending_event_count_ + unresolved;
    }

    AbsoluteJudgementFatalSnapshot JudgementScheduler::FatalSnapshot() const noexcept
    {
        AbsoluteJudgementNativeIdentityDiagnostic native{};
        if (stage_.bound())
        {
            native = {
                .stage_generation = stage_.native().stage_generation,
                .native_manager = stage_.native().tune_manager,
                .tune = stage_.native().tune,
                .judgement_state = stage_.native().judgement_state,
                .score_state = stage_.native().score_state,
                .booster = stage_.native().booster,
                .player = stage_.native().player,
            };
        }
        else
        {
            native.stage_generation = stage_.generation();
            native.native_manager = stage_.tune_manager();
        }
        return {
            .enabled = true,
            .native = native,
            .input_generation = stage_.open() ? stage_.cutoff().transport_epoch : 0,
            .endpoint_generation = stage_.endpoint_generation(),
            .last_anchor_sequence = last_anchor_sequence_,
            .runtime = RuntimeSnapshot(),
        };
    }

    [[noreturn]] void JudgementScheduler::Fatal(const AbsoluteJudgementFatalPredicate predicate,
                                                const AbsoluteJudgementFatalReason category,
                                                const std::initializer_list<std::uint64_t> operands) const noexcept
    {
        FatalActiveStage(MakeAbsoluteJudgementFatalRecord(predicate, stage_.generation(), category, operands),
                         FatalSnapshot());
    }
} // namespace gc::absolute_judgement
