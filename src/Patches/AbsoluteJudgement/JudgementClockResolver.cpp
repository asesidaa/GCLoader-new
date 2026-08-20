#include "Patches/AbsoluteJudgement/JudgementClockResolver.h"

#include <algorithm>
#include <limits>
#include <span>

namespace gc::absolute_judgement {
namespace {

using gc::audio::ExactClockStatus;
using gc::audio::ExactPlaybackClosure;
using gc::audio::ExactPlaybackEpoch;
using gc::timing::CheckedRational;

struct EpochSnapshotResult final {
    ExactClockStatus status{ExactClockStatus::Pending};
    std::size_t count{};
    std::uint64_t publication{};
    bool checked_arithmetic_failure{};
};

std::optional<CheckedRational> WholeUnsigned(
    const std::uint64_t value) noexcept {
    if (value > static_cast<std::uint64_t>(
                    (std::numeric_limits<std::int64_t>::max)())) {
        return std::nullopt;
    }
    return CheckedRational::Whole(static_cast<std::int64_t>(value));
}

std::optional<CheckedRational> SourceFrameAtOutput(
    const ExactPlaybackEpoch& epoch,
    const CheckedRational& output) noexcept {
    const auto output_origin = WholeUnsigned(epoch.output_origin);
    const auto source_origin = WholeUnsigned(epoch.source_origin);
    if (!output_origin || !source_origin || epoch.output_rate == 0 ||
        epoch.source_rate == 0) {
        return std::nullopt;
    }
    const auto output_delta = output.Subtract(*output_origin);
    if (!output_delta) {
        return std::nullopt;
    }
    const auto source_delta = output_delta->Multiply(
        static_cast<std::int64_t>(epoch.source_rate), epoch.output_rate);
    if (!source_delta) {
        return std::nullopt;
    }
    const auto source = source_origin->Add(*source_delta);
    if (!source || source->Compare(CheckedRational::Whole(0)) < 0) {
        return std::nullopt;
    }
    return *source;
}

std::optional<CheckedRational> SourceSecondsAtOutput(
    const ExactPlaybackEpoch& epoch,
    const CheckedRational& output) noexcept {
    const auto source = SourceFrameAtOutput(epoch, output);
    if (!source || epoch.source_rate == 0) {
        return std::nullopt;
    }
    const auto seconds = source->Multiply(1, epoch.source_rate);
    if (!seconds) {
        return std::nullopt;
    }
    return *seconds;
}

std::optional<CheckedRational> AddGameTimeOffset(
    const CheckedRational& source_seconds,
    const std::int32_t game_time_offset_ms) noexcept {
    const auto offset = CheckedRational::Whole(game_time_offset_ms)
                            .Multiply(1, 1000);
    if (!offset) {
        return std::nullopt;
    }
    const auto judgement = source_seconds.Add(*offset);
    if (!judgement) {
        return std::nullopt;
    }
    return *judgement;
}

bool EpochShapeValid(const ExactPlaybackEpoch& epoch) noexcept {
    const bool closure_engaged = epoch.closure.has_value();
    const bool closed_tail_engaged = epoch.closed_source_tail.has_value();
    return epoch.buffer_instance_id != 0 &&
        epoch.endpoint_generation != 0 && epoch.playback_generation != 0 &&
        epoch.output_rate != 0 && epoch.source_rate != 0 &&
        epoch.mapped_output_tail > epoch.output_origin &&
        closure_engaged == closed_tail_engaged &&
        (!closed_tail_engaged || epoch.closed_source_tail->numerator() >= 0);
}

EpochSnapshotResult CopyAndValidate(
    const ObservedPlaybackHistory& observed,
    std::span<ExactPlaybackEpoch> scratch) noexcept {
    if (!observed.history || observed.buffer_instance_id == 0 ||
        observed.endpoint_generation == 0 ||
        observed.history->exact_buffer_instance_id() !=
            observed.buffer_instance_id ||
        observed.history->exact_endpoint_generation() !=
            observed.endpoint_generation) {
        return {.status = ExactClockStatus::Discontinuous};
    }

    gc::audio::ExactPlaybackHistoryStatus snapshot{};
    const auto count = observed.history->CopyExactPlaybackEpochs(
        scratch, &snapshot);
    if (snapshot.prefix_evicted) {
        return {
            .status = ExactClockStatus::HistoryLost,
            .count = count,
            .publication = snapshot.publication_sequence,
        };
    }
    if (observed.last_validated_publication != 0 &&
        snapshot.publication_sequence <
            observed.last_validated_publication) {
        return {
            .status = ExactClockStatus::Discontinuous,
            .count = count,
            .publication = snapshot.publication_sequence,
        };
    }
    if (snapshot.status != ExactClockStatus::Resolved) {
        if (snapshot.status == ExactClockStatus::NoPlayback ||
            (snapshot.status == ExactClockStatus::Pending &&
             observed.last_validated_publication != 0)) {
            return {
                .status = ExactClockStatus::Discontinuous,
                .count = count,
                .publication = snapshot.publication_sequence,
            };
        }
        return {
            .status = snapshot.status,
            .count = count,
            .publication = snapshot.publication_sequence,
        };
    }
    if (count == 0) {
        return {
            .status = ExactClockStatus::Pending,
            .publication = snapshot.publication_sequence,
        };
    }

    for (std::size_t index = 0; index < count; ++index) {
        const auto& epoch = scratch[index];
        if (!EpochShapeValid(epoch) ||
            epoch.buffer_instance_id != observed.buffer_instance_id ||
            epoch.endpoint_generation != observed.endpoint_generation) {
            return {
                .status = ExactClockStatus::Discontinuous,
                .count = count,
                .publication = snapshot.publication_sequence,
            };
        }
        if (epoch.closure.has_value() &&
            epoch.closure != ExactPlaybackClosure::NaturalEnd) {
            const auto output_tail = WholeUnsigned(
                epoch.mapped_output_tail);
            const auto derived_tail = output_tail
                ? SourceFrameAtOutput(epoch, *output_tail)
                : std::nullopt;
            if (!output_tail || !derived_tail) {
                return {
                    .status = ExactClockStatus::Discontinuous,
                    .count = count,
                    .publication = snapshot.publication_sequence,
                    .checked_arithmetic_failure = true,
                };
            }
            if (derived_tail->Compare(*epoch.closed_source_tail) != 0) {
                return {
                    .status = ExactClockStatus::Discontinuous,
                    .count = count,
                    .publication = snapshot.publication_sequence,
                };
            }
        }
        if (index == 0) {
            continue;
        }
        const auto& previous = scratch[index - 1];
        if (epoch.output_rate != previous.output_rate ||
            epoch.source_rate != previous.source_rate ||
            epoch.playback_generation <= previous.playback_generation ||
            epoch.output_origin < previous.output_origin ||
            !previous.closure.has_value() ||
            previous.closure == ExactPlaybackClosure::WriterQuiescedRelease) {
            return {
                .status = ExactClockStatus::Discontinuous,
                .count = count,
                .publication = snapshot.publication_sequence,
            };
        }
    }
    if (scratch[count - 1].closure == ExactPlaybackClosure::LaterEpoch) {
        return {
            .status = ExactClockStatus::Discontinuous,
            .count = count,
            .publication = snapshot.publication_sequence,
        };
    }

    return {
        .status = ExactClockStatus::Resolved,
        .count = count,
        .publication = snapshot.publication_sequence,
    };
}

JudgementHistoryValidationResult ValidateOverlap(
    const ExactPlaybackEpoch& left,
    const ExactPlaybackEpoch& right) noexcept {
    const auto overlap_begin = (std::max)(
        left.output_origin, right.output_origin);
    const auto overlap_end = (std::min)(
        left.mapped_output_tail, right.mapped_output_tail);
    if (overlap_begin >= overlap_end) {
        return {.status = ExactClockStatus::Resolved};
    }
    if (left.output_rate != right.output_rate) {
        return {.status = ExactClockStatus::Discontinuous};
    }
    const auto output = WholeUnsigned(overlap_begin);
    if (!output) {
        return {
            .status = ExactClockStatus::Discontinuous,
            .checked_arithmetic_failure = true,
        };
    }
    const auto left_seconds = SourceSecondsAtOutput(left, *output);
    const auto right_seconds = SourceSecondsAtOutput(right, *output);
    if (!left_seconds || !right_seconds) {
        return {
            .status = ExactClockStatus::Discontinuous,
            .checked_arithmetic_failure = true,
        };
    }
    return {
        .status = left_seconds->Compare(*right_seconds) == 0
            ? ExactClockStatus::Resolved
            : ExactClockStatus::Discontinuous,
    };
}

JudgementHistoryValidationResult ValidateSnapshotOverlaps(
    std::span<const ExactPlaybackEpoch> epochs) noexcept {
    for (std::size_t left = 0; left < epochs.size(); ++left) {
        for (std::size_t right = left + 1; right < epochs.size(); ++right) {
            const auto result = ValidateOverlap(epochs[left], epochs[right]);
            if (result.status != ExactClockStatus::Resolved) {
                return result;
            }
        }
    }
    return {.status = ExactClockStatus::Resolved};
}

JudgementClockResult EndpointFailure(
    const gc::audio::ExactOutputClockResult& endpoint) noexcept {
    if (endpoint.status == ExactClockStatus::Resolved &&
        !endpoint.output_frame) {
        return {.status = ExactClockStatus::Discontinuous};
    }
    return {
        .status = endpoint.status,
        .output_frame = endpoint.output_frame,
        .endpoint_anchor_sequence = endpoint.anchor_sequence,
    };
}

JudgementClockResult ConvertSourceResult(
    const gc::audio::ExactSourceFrameResult& source,
    const CheckedRational& output,
    const std::int32_t game_time_offset_ms) noexcept {
    JudgementClockResult result{
        .status = source.status,
        .output_frame = output,
        .buffer_instance_id = source.buffer_instance_id,
        .playback_generation = source.playback_generation,
    };
    if (source.status == ExactClockStatus::Resolved) {
        if (!source.resolved || source.resolved->source_rate == 0) {
            result.status = ExactClockStatus::Discontinuous;
            return result;
        }
        const auto source_seconds = source.resolved->source_frame.Multiply(
            1, source.resolved->source_rate);
        if (!source_seconds) {
            result.status = ExactClockStatus::Discontinuous;
            result.checked_arithmetic_failure = true;
            return result;
        }
        const auto judgement = AddGameTimeOffset(
            *source_seconds, game_time_offset_ms);
        if (!judgement) {
            result.status = ExactClockStatus::Discontinuous;
            result.checked_arithmetic_failure = true;
            return result;
        }
        result.source_frame = source.resolved->source_frame;
        result.judgement_seconds = *judgement;
    }
    if (source.status == ExactClockStatus::OutsidePlayback &&
        source.closed_frontier.has_value()) {
        if (source.closed_frontier->source_rate == 0) {
            result.status = ExactClockStatus::Discontinuous;
            return result;
        }
        const auto source_seconds = source.closed_frontier->source_frame
                                        .Multiply(
                                            1,
                                            source.closed_frontier
                                                ->source_rate);
        const auto judgement = source_seconds
            ? AddGameTimeOffset(*source_seconds, game_time_offset_ms)
            : std::nullopt;
        if (!source_seconds || !judgement) {
            result.status = ExactClockStatus::Discontinuous;
            result.checked_arithmetic_failure = true;
            return result;
        }
        result.source_frame = source.closed_frontier->source_frame;
        result.closed_frontier_seconds = *judgement;
    }
    return result;
}

JudgementClockResult ResolveStableSource(
    const ObservedPlaybackHistory& observed,
    const CheckedRational& output,
    const std::int32_t game_time_offset_ms,
    std::array<ExactPlaybackEpoch,
               gc::audio::kExactPlaybackEpochCapacity>& scratch) noexcept {
    if (!observed.history || observed.last_validated_publication == 0) {
        return {
            .status = ExactClockStatus::Discontinuous,
            .output_frame = output,
        };
    }

    const auto source = observed.history->ResolveExactSourceFrame(output);
    const auto stable = CopyAndValidate(observed, scratch);
    if (stable.checked_arithmetic_failure) {
        return {
            .status = ExactClockStatus::Discontinuous,
            .output_frame = output,
            .checked_arithmetic_failure = true,
        };
    }
    if (stable.status != ExactClockStatus::Resolved) {
        return {
            .status = stable.status,
            .output_frame = output,
        };
    }
    if (stable.publication != observed.last_validated_publication) {
        return {
            .status = ExactClockStatus::TemporarilyUnavailable,
            .output_frame = output,
        };
    }
    if (source.status == ExactClockStatus::NoPlayback ||
        (source.buffer_instance_id != 0 &&
         source.buffer_instance_id != observed.buffer_instance_id) ||
        ((source.status == ExactClockStatus::Resolved ||
          source.closed_frontier.has_value()) &&
         source.playback_generation == 0)) {
        return {
            .status = ExactClockStatus::Discontinuous,
            .output_frame = output,
        };
    }
    return ConvertSourceResult(source, output, game_time_offset_ms);
}

} // namespace

JudgementHistoryValidationResult
JudgementClockResolver::ValidateRetainedHistories(
    JudgementClockBinding& binding,
    std::array<ExactPlaybackEpoch,
               gc::audio::kExactPlaybackEpochCapacity>& left_scratch,
    std::array<ExactPlaybackEpoch,
               gc::audio::kExactPlaybackEpochCapacity>& right_scratch)
    const noexcept {
    if (!binding.endpoint || binding.endpoint_generation == 0 ||
        binding.endpoint->endpoint_generation() !=
            binding.endpoint_generation) {
        return {.status = ExactClockStatus::Discontinuous};
    }
    if (binding.observed_stage_bgm_histories.empty()) {
        return {.status = ExactClockStatus::Pending};
    }

    bool pending{};
    bool unavailable{};
    for (auto& observed : binding.observed_stage_bgm_histories) {
        const auto snapshot = CopyAndValidate(observed, left_scratch);
        observed.validation_candidate_publication = snapshot.publication;
        if (snapshot.status == ExactClockStatus::HistoryLost ||
            snapshot.status == ExactClockStatus::Discontinuous) {
            return {
                .status = snapshot.status,
                .checked_arithmetic_failure =
                    snapshot.checked_arithmetic_failure,
            };
        }
        if (snapshot.status == ExactClockStatus::TemporarilyUnavailable) {
            unavailable = true;
        } else if (snapshot.status == ExactClockStatus::Pending) {
            pending = true;
        } else if (snapshot.status != ExactClockStatus::Resolved) {
            return {.status = snapshot.status};
        } else if (snapshot.publication !=
                   observed.last_validated_publication) {
            const auto internal = ValidateSnapshotOverlaps(
                std::span<const ExactPlaybackEpoch>(
                    left_scratch.data(), snapshot.count));
            if (internal.status != ExactClockStatus::Resolved) {
                return internal;
            }
        }
    }
    if (unavailable) {
        return {.status = ExactClockStatus::TemporarilyUnavailable};
    }
    if (pending) {
        return {.status = ExactClockStatus::Pending};
    }

    for (std::size_t left_index = 0;
         left_index < binding.observed_stage_bgm_histories.size();
         ++left_index) {
        auto& left_observed =
            binding.observed_stage_bgm_histories[left_index];
        const bool left_changed =
            left_observed.validation_candidate_publication !=
            left_observed.last_validated_publication;
        for (std::size_t right_index = left_index + 1;
             right_index < binding.observed_stage_bgm_histories.size();
             ++right_index) {
            auto& right_observed =
                binding.observed_stage_bgm_histories[right_index];
            const bool right_changed =
                right_observed.validation_candidate_publication !=
                right_observed.last_validated_publication;
            if (!left_changed && !right_changed) {
                continue;
            }

            const auto left = CopyAndValidate(
                left_observed, left_scratch);
            if (left.status != ExactClockStatus::Resolved) {
                return {
                    .status = left.status,
                    .checked_arithmetic_failure =
                        left.checked_arithmetic_failure,
                };
            }
            if (left.publication !=
                left_observed.validation_candidate_publication) {
                return {
                    .status = ExactClockStatus::TemporarilyUnavailable,
                };
            }
            const auto right = CopyAndValidate(
                right_observed, right_scratch);
            if (right.status != ExactClockStatus::Resolved) {
                return {
                    .status = right.status,
                    .checked_arithmetic_failure =
                        right.checked_arithmetic_failure,
                };
            }
            if (right.publication !=
                right_observed.validation_candidate_publication) {
                return {
                    .status = ExactClockStatus::TemporarilyUnavailable,
                };
            }
            for (std::size_t left_epoch = 0;
                 left_epoch < left.count;
                 ++left_epoch) {
                for (std::size_t right_epoch = 0;
                     right_epoch < right.count;
                     ++right_epoch) {
                    const auto overlap = ValidateOverlap(
                        left_scratch[left_epoch],
                        right_scratch[right_epoch]);
                    if (overlap.status != ExactClockStatus::Resolved) {
                        return overlap;
                    }
                }
            }
        }
    }
    for (auto& observed : binding.observed_stage_bgm_histories) {
        observed.last_validated_publication =
            observed.validation_candidate_publication;
    }
    return {.status = ExactClockStatus::Resolved};
}

JudgementClockResult JudgementClockResolver::ResolveHistoricalQpc(
    const JudgementClockBinding& binding,
    const std::int64_t qpc_ticks,
    const std::int32_t game_time_offset_ms,
    std::array<ExactPlaybackEpoch,
               gc::audio::kExactPlaybackEpochCapacity>& scratch)
    const noexcept {
    if (!binding.endpoint || binding.endpoint_generation == 0 ||
        binding.endpoint->endpoint_generation() !=
            binding.endpoint_generation) {
        return {.status = ExactClockStatus::Discontinuous};
    }
    const auto endpoint = binding.endpoint->ResolveQpc(qpc_ticks);
    if (endpoint.endpoint_generation != binding.endpoint_generation) {
        return {.status = ExactClockStatus::Discontinuous};
    }
    if (endpoint.status != ExactClockStatus::Resolved ||
        !endpoint.output_frame) {
        return EndpointFailure(endpoint);
    }
    if (binding.observed_stage_bgm_histories.empty()) {
        return {
            .status = ExactClockStatus::Pending,
            .output_frame = endpoint.output_frame,
            .endpoint_anchor_sequence = endpoint.anchor_sequence,
        };
    }

    JudgementClockResult resolved{
        .status = ExactClockStatus::OutsidePlayback,
        .output_frame = endpoint.output_frame,
        .endpoint_anchor_sequence = endpoint.anchor_sequence,
    };
    bool has_resolved{};
    bool has_pending{};
    bool has_unavailable{};
    std::optional<ExactClockStatus> other_status;
    for (const auto& observed : binding.observed_stage_bgm_histories) {
        auto candidate = ResolveStableSource(
            observed,
            *endpoint.output_frame,
            game_time_offset_ms,
            scratch);
        candidate.endpoint_anchor_sequence = endpoint.anchor_sequence;
        if (candidate.checked_arithmetic_failure) {
            return candidate;
        }
        if (candidate.status == ExactClockStatus::Discontinuous ||
            candidate.status == ExactClockStatus::HistoryLost) {
            return candidate;
        }
        if (candidate.status == ExactClockStatus::Pending) {
            has_pending = true;
            continue;
        }
        if (candidate.status == ExactClockStatus::TemporarilyUnavailable) {
            has_unavailable = true;
            continue;
        }
        if (candidate.status == ExactClockStatus::OutsidePlayback) {
            continue;
        }
        if (candidate.status != ExactClockStatus::Resolved ||
            !candidate.judgement_seconds) {
            other_status = candidate.status;
            continue;
        }
        if (has_resolved &&
            resolved.judgement_seconds->Compare(
                *candidate.judgement_seconds) != 0) {
            return {
                .status = ExactClockStatus::Discontinuous,
                .endpoint_anchor_sequence = endpoint.anchor_sequence,
            };
        }
        if (!has_resolved) {
            resolved = candidate;
            has_resolved = true;
        }
    }
    if (has_pending) {
        return {
            .status = ExactClockStatus::Pending,
            .output_frame = endpoint.output_frame,
            .endpoint_anchor_sequence = endpoint.anchor_sequence,
        };
    }
    if (has_unavailable) {
        return {
            .status = ExactClockStatus::TemporarilyUnavailable,
            .output_frame = endpoint.output_frame,
            .endpoint_anchor_sequence = endpoint.anchor_sequence,
        };
    }
    if (other_status) {
        return {
            .status = *other_status,
            .output_frame = endpoint.output_frame,
            .endpoint_anchor_sequence = endpoint.anchor_sequence,
        };
    }
    return resolved;
}

JudgementClockResult JudgementClockResolver::ResolveCurrentQpc(
    const JudgementClockBinding& binding,
    const ObservedPlaybackHistory& selected_history,
    const std::int64_t qpc_ticks,
    const std::int32_t game_time_offset_ms,
    std::array<ExactPlaybackEpoch,
               gc::audio::kExactPlaybackEpochCapacity>& scratch)
    const noexcept {
    if (!binding.endpoint || binding.endpoint_generation == 0 ||
        binding.endpoint->endpoint_generation() !=
            binding.endpoint_generation ||
        selected_history.endpoint_generation !=
            binding.endpoint_generation) {
        return {.status = ExactClockStatus::Discontinuous};
    }
    const auto endpoint = binding.endpoint->ResolveQpc(qpc_ticks);
    if (endpoint.endpoint_generation != binding.endpoint_generation) {
        return {.status = ExactClockStatus::Discontinuous};
    }
    if (endpoint.status != ExactClockStatus::Resolved ||
        !endpoint.output_frame) {
        return EndpointFailure(endpoint);
    }
    auto result = ResolveStableSource(
        selected_history,
        *endpoint.output_frame,
        game_time_offset_ms,
        scratch);
    result.endpoint_anchor_sequence = endpoint.anchor_sequence;
    return result;
}

JudgementPlaybackOriginResult
JudgementClockResolver::FindFirstPlaybackOrigin(
    const JudgementClockBinding& binding,
    const std::int32_t game_time_offset_ms,
    std::array<ExactPlaybackEpoch,
               gc::audio::kExactPlaybackEpochCapacity>& scratch)
    const noexcept {
    std::optional<ExactPlaybackEpoch> first;
    for (const auto& observed : binding.observed_stage_bgm_histories) {
        const auto snapshot = CopyAndValidate(observed, scratch);
        if (snapshot.status != ExactClockStatus::Resolved) {
            return {
                .status = snapshot.status,
                .checked_arithmetic_failure =
                    snapshot.checked_arithmetic_failure,
            };
        }
        if (snapshot.publication !=
            observed.last_validated_publication) {
            return {
                .status = ExactClockStatus::TemporarilyUnavailable,
            };
        }
        if (snapshot.count != 0 &&
            (!first ||
             scratch[0].output_origin < first->output_origin)) {
            first = scratch[0];
        }
    }
    if (!first) {
        return {.status = ExactClockStatus::Pending};
    }
    const auto output = WholeUnsigned(first->output_origin);
    const auto source_seconds = output
        ? SourceSecondsAtOutput(*first, *output)
        : std::nullopt;
    const auto judgement = source_seconds
        ? AddGameTimeOffset(*source_seconds, game_time_offset_ms)
        : std::nullopt;
    if (!output || !source_seconds || !judgement) {
        return {
            .status = ExactClockStatus::Discontinuous,
            .checked_arithmetic_failure = true,
        };
    }
    return {
        .status = ExactClockStatus::Resolved,
        .judgement_seconds = *judgement,
    };
}

} // namespace gc::absolute_judgement
