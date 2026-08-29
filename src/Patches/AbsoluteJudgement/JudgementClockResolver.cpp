#include "Patches/AbsoluteJudgement/JudgementClockResolver.h"

#include <limits>

namespace gc::absolute_judgement
{
    namespace
    {
        using gc::audio::ExactClockStatus;
        using gc::audio::ExactPlaybackEpoch;
        using gc::audio::ExactPlaybackOrigin;
        using gc::timing::CheckedRational;

        [[nodiscard]] std::optional<CheckedRational> WholeUnsigned(const std::uint64_t value) noexcept
        {
            if (value > static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()))
            {
                return std::nullopt;
            }
            return CheckedRational::Whole(static_cast<std::int64_t>(value));
        }

        [[nodiscard]] JudgementClockStatus TimelineStatus(const ExactClockStatus status,
                                                           const bool before_binding) noexcept
        {
            switch (status)
            {
            case ExactClockStatus::Pending:
                return JudgementClockStatus::Pending;
            case ExactClockStatus::TemporarilyUnavailable:
                return JudgementClockStatus::TemporarilyUnavailable;
            case ExactClockStatus::HistoryLost:
                return before_binding ? JudgementClockStatus::HistoryLostBeforeBinding
                                      : JudgementClockStatus::UnsupportedContinuity;
            case ExactClockStatus::Resolved:
                return JudgementClockStatus::Resolved;
            default:
                return JudgementClockStatus::UnsupportedContinuity;
            }
        }

        [[nodiscard]] JudgementClockResult TimelineFailure(const gc::audio::ExactJudgementTimelineResult& timeline,
                                                            const bool before_binding) noexcept
        {
            return {
                .status = TimelineStatus(timeline.status, before_binding),
                .failure = timeline.status == ExactClockStatus::HistoryLost
                               ? (before_binding ? JudgementClockFailure::StageOriginHistoryLost
                                                 : JudgementClockFailure::TimelineProjectionDiscontinuous)
                               : JudgementClockFailure::None,
                .output_frame = timeline.logical_output_frame,
                .provider_anchor_sequence = timeline.provider_anchor_sequence,
                .provider_position = timeline.provider_position,
            };
        }
    } // namespace

    void JudgementClockResolver::Reset(const std::uint64_t stage_generation,
                                       const gc::timing::AbsoluteHostTime& stage_entry_time,
                                       const std::int32_t game_time_offset_ms) noexcept
    {
        binding_ = {
            .stage_generation = stage_generation,
            .stage_entry_time = stage_entry_time,
            .game_time_offset_ms = game_time_offset_ms,
        };
    }

    bool JudgementClockResolver::bound() const noexcept
    {
        return binding_.anchor.has_value();
    }

    const JudgementStageClockAnchor& JudgementClockResolver::anchor() const noexcept
    {
        return *binding_.anchor;
    }

    JudgementClockResult JudgementClockResolver::TryBind(
        const gc::audio::GameplayAudioCursorObservation& selected,
        const std::shared_ptr<const gc::audio::ExactJudgementTimeline>& timeline,
        const std::span<ExactPlaybackEpoch> scratch) noexcept
    {
        if (bound())
        {
            return Resolve(binding_.stage_entry_time, gc::audio::ExactClockResolveIntent::FinalizedTimestamp);
        }
        if (binding_.stage_generation == 0 || binding_.stage_entry_time.qpc_ticks <= 0)
        {
            return {
                .status = JudgementClockStatus::UnsupportedContinuity,
                .failure = JudgementClockFailure::InvalidStageBinding,
            };
        }
        if (!timeline)
        {
            return {.status = JudgementClockStatus::Pending};
        }

        const auto timeline_generation = timeline->info().timeline_generation;
        if (timeline_generation == 0)
        {
            return {
                .status = JudgementClockStatus::UnsupportedContinuity,
                .failure = JudgementClockFailure::TimelineGenerationChanged,
            };
        }
        if (binding_.pending_timeline)
        {
            if (binding_.pending_timeline.get() != timeline.get() ||
                binding_.pending_timeline->info().timeline_generation != timeline_generation)
            {
                return {
                    .status = JudgementClockStatus::UnsupportedContinuity,
                    .failure = binding_.pending_timeline.get() != timeline.get()
                                   ? JudgementClockFailure::TimelineProviderChanged
                                   : JudgementClockFailure::TimelineGenerationChanged,
                };
            }
        }
        else
        {
            binding_.pending_timeline = timeline;
        }
        if (selected.state != gc::audio::GameplayAudioCursorState::Exact || !selected.exact_history ||
            selected.buffer_instance_id == 0 || selected.timeline_generation == 0 || selected.playback_generation == 0)
        {
            return {.status = JudgementClockStatus::Pending};
        }
        if (timeline_generation != selected.timeline_generation)
        {
            return {
                .status = JudgementClockStatus::UnsupportedContinuity,
                .failure = JudgementClockFailure::PlaybackHistoryTimelineChangedBeforeAnchor,
            };
        }
        if (binding_.pending_history)
        {
            if (binding_.pending_buffer_instance_id != selected.buffer_instance_id ||
                binding_.pending_timeline_generation != selected.timeline_generation ||
                binding_.pending_history.get() != selected.exact_history.get())
            {
                return {
                    .status = JudgementClockStatus::UnsupportedContinuity,
                    .failure = binding_.pending_timeline_generation != selected.timeline_generation
                                   ? JudgementClockFailure::PlaybackHistoryTimelineChangedBeforeAnchor
                                   : JudgementClockFailure::PlaybackHistoryObjectChangedBeforeAnchor,
                };
            }
        }
        else
        {
            binding_.pending_buffer_instance_id = selected.buffer_instance_id;
            binding_.pending_timeline_generation = selected.timeline_generation;
            binding_.pending_history = selected.exact_history;
        }

        if (!binding_.pending_history->HasExactPlaybackHistory() ||
            binding_.pending_history->exact_buffer_instance_id() != binding_.pending_buffer_instance_id ||
            binding_.pending_history->exact_timeline_generation() != binding_.pending_timeline_generation)
        {
            return {
                .status = JudgementClockStatus::UnsupportedContinuity,
                .failure = binding_.pending_history->exact_timeline_generation() != binding_.pending_timeline_generation
                               ? JudgementClockFailure::PlaybackHistoryTimelineChangedBeforeAnchor
                               : JudgementClockFailure::PlaybackHistoryObjectChangedBeforeAnchor,
            };
        }

        const auto entry_output =
            timeline->Resolve(binding_.stage_entry_time, gc::audio::ExactClockResolveIntent::FinalizedTimestamp);
        if (entry_output.timeline_generation != timeline_generation)
        {
            return {
                .status = JudgementClockStatus::UnsupportedContinuity,
                .failure = JudgementClockFailure::TimelineGenerationChanged,
            };
        }
        if (entry_output.status != ExactClockStatus::Resolved || !entry_output.logical_output_frame)
        {
            return TimelineFailure(entry_output, true);
        }

        gc::audio::ExactPlaybackHistoryStatus history_status{};
        const auto count = binding_.pending_history->CopyExactPlaybackEpochs(scratch, &history_status);
        if (history_status.prefix_evicted || history_status.status == ExactClockStatus::HistoryLost)
        {
            return {
                .status = JudgementClockStatus::HistoryLostBeforeBinding,
                .failure = JudgementClockFailure::StageOriginHistoryLost,
                .output_frame = entry_output.logical_output_frame,
                .provider_anchor_sequence = entry_output.provider_anchor_sequence,
                .provider_position = entry_output.provider_position,
            };
        }
        if (history_status.status == ExactClockStatus::Pending ||
            history_status.status == ExactClockStatus::NoPlayback || count == 0)
        {
            return {
                .status = JudgementClockStatus::Pending,
                .output_frame = entry_output.logical_output_frame,
                .provider_anchor_sequence = entry_output.provider_anchor_sequence,
                .provider_position = entry_output.provider_position,
            };
        }
        if (history_status.status == ExactClockStatus::TemporarilyUnavailable)
        {
            return {
                .status = JudgementClockStatus::TemporarilyUnavailable,
                .output_frame = entry_output.logical_output_frame,
                .provider_anchor_sequence = entry_output.provider_anchor_sequence,
                .provider_position = entry_output.provider_position,
            };
        }
        if (history_status.status != ExactClockStatus::Resolved)
        {
            return {
                .status = JudgementClockStatus::UnsupportedContinuity,
                .failure = JudgementClockFailure::PlaybackHistoryObjectChangedBeforeAnchor,
                .output_frame = entry_output.logical_output_frame,
                .provider_anchor_sequence = entry_output.provider_anchor_sequence,
                .provider_position = entry_output.provider_position,
            };
        }

        const ExactPlaybackEpoch* earliest{};
        for (std::size_t index = 0; index < count; ++index)
        {
            const auto& epoch = scratch[index];
            if (epoch.buffer_instance_id != selected.buffer_instance_id ||
                epoch.timeline_generation != selected.timeline_generation ||
                epoch.origin != ExactPlaybackOrigin::Play || epoch.playback_generation == 0 ||
                epoch.playback_generation > selected.playback_generation)
            {
                continue;
            }
            const auto output_origin = WholeUnsigned(epoch.output_origin);
            if (!output_origin)
            {
                return {
                    .status = JudgementClockStatus::CheckedArithmeticFailure,
                    .failure = JudgementClockFailure::RationalOperationUnrepresentable,
                };
            }
            if (output_origin->Compare(*entry_output.logical_output_frame) < 0)
            {
                continue;
            }
            if (epoch.output_rate == 0 || epoch.source_rate == 0)
            {
                return {
                    .status = JudgementClockStatus::UnsupportedContinuity,
                    .failure = JudgementClockFailure::InvalidClockRates,
                };
            }
            if (earliest == nullptr || epoch.output_origin < earliest->output_origin ||
                (epoch.output_origin == earliest->output_origin &&
                 epoch.playback_generation < earliest->playback_generation))
            {
                earliest = &epoch;
            }
        }
        if (earliest == nullptr)
        {
            return {
                .status = JudgementClockStatus::Pending,
                .output_frame = entry_output.logical_output_frame,
                .provider_anchor_sequence = entry_output.provider_anchor_sequence,
                .provider_position = entry_output.provider_position,
            };
        }

        binding_.anchor = JudgementStageClockAnchor{
            .stage_generation = binding_.stage_generation,
            .timeline_generation = timeline_generation,
            .buffer_instance_id = earliest->buffer_instance_id,
            .playback_generation = earliest->playback_generation,
            .logical_output_origin = earliest->output_origin,
            .source_origin = earliest->source_origin,
            .logical_output_rate = earliest->output_rate,
            .source_rate = earliest->source_rate,
            .game_time_offset_ms = binding_.game_time_offset_ms,
            .timeline = std::move(binding_.pending_timeline),
        };
        binding_.pending_history.reset();
        return Resolve(binding_.stage_entry_time, gc::audio::ExactClockResolveIntent::FinalizedTimestamp);
    }

    JudgementClockResult JudgementClockResolver::Resolve(const gc::timing::AbsoluteHostTime& timestamp,
                                                         const gc::audio::ExactClockResolveIntent intent) const noexcept
    {
        if (!bound())
        {
            return {.status = JudgementClockStatus::Pending};
        }
        const auto& stage_anchor = *binding_.anchor;
        if (!stage_anchor.timeline || stage_anchor.timeline_generation == 0 ||
            stage_anchor.timeline->info().timeline_generation != stage_anchor.timeline_generation)
        {
            return {
                .status = JudgementClockStatus::UnsupportedContinuity,
                .failure = !stage_anchor.timeline ? JudgementClockFailure::TimelineProviderChanged
                                                  : JudgementClockFailure::TimelineGenerationChanged,
            };
        }

        const auto timeline = stage_anchor.timeline->Resolve(timestamp, intent);
        if (timeline.timeline_generation != stage_anchor.timeline_generation)
        {
            return {
                .status = JudgementClockStatus::UnsupportedContinuity,
                .failure = JudgementClockFailure::TimelineGenerationChanged,
            };
        }
        if (timeline.status != ExactClockStatus::Resolved || !timeline.logical_output_frame)
        {
            return TimelineFailure(timeline, false);
        }

        const auto output_origin = WholeUnsigned(stage_anchor.logical_output_origin);
        const auto source_origin = WholeUnsigned(stage_anchor.source_origin);
        if (!output_origin || !source_origin || stage_anchor.logical_output_rate == 0 || stage_anchor.source_rate == 0)
        {
            return {
                .status = JudgementClockStatus::CheckedArithmeticFailure,
                .failure = stage_anchor.logical_output_rate == 0 || stage_anchor.source_rate == 0
                               ? JudgementClockFailure::InvalidClockRates
                               : JudgementClockFailure::RationalOperationUnrepresentable,
            };
        }
        const auto source_origin_seconds = source_origin->Multiply(1, stage_anchor.source_rate);
        const auto game_offset_seconds = CheckedRational::Whole(stage_anchor.game_time_offset_ms).Multiply(1, 1000);
        const auto output_delta = timeline.logical_output_frame->Subtract(*output_origin);
        if (!source_origin_seconds || !game_offset_seconds || !output_delta)
        {
            return {
                .status = JudgementClockStatus::CheckedArithmeticFailure,
                .failure = JudgementClockFailure::RationalOperationUnrepresentable,
            };
        }
        const auto output_delta_seconds = output_delta->Multiply(1, stage_anchor.logical_output_rate);
        const auto with_offset = source_origin_seconds->Add(*game_offset_seconds);
        if (!output_delta_seconds || !with_offset)
        {
            return {
                .status = JudgementClockStatus::CheckedArithmeticFailure,
                .failure = JudgementClockFailure::RationalOperationUnrepresentable,
            };
        }
        const auto judgement = with_offset->Add(*output_delta_seconds);
        if (!judgement)
        {
            return {
                .status = JudgementClockStatus::CheckedArithmeticFailure,
                .failure = JudgementClockFailure::RationalOperationUnrepresentable,
            };
        }

        return {
            .status = JudgementClockStatus::Resolved,
            .output_frame = timeline.logical_output_frame,
            .judgement_seconds = *judgement,
            .provider_anchor_sequence = timeline.provider_anchor_sequence,
            .provider_position = timeline.provider_position,
        };
    }
} // namespace gc::absolute_judgement
