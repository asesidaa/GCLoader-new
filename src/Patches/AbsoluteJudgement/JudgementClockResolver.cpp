#include "Patches/AbsoluteJudgement/JudgementClockResolver.h"

#include <limits>

namespace gc::absolute_judgement {
namespace {

using gc::audio::ExactClockStatus;
using gc::audio::ExactPlaybackEpoch;
using gc::audio::ExactPlaybackOrigin;
using gc::timing::CheckedRational;

[[nodiscard]] std::optional<CheckedRational> WholeUnsigned(
    const std::uint64_t value) noexcept {
    if (value > static_cast<std::uint64_t>(
                    (std::numeric_limits<std::int64_t>::max)())) {
        return std::nullopt;
    }
    return CheckedRational::Whole(static_cast<std::int64_t>(value));
}

[[nodiscard]] JudgementClockStatus EndpointStatus(
    const ExactClockStatus status,
    const bool before_binding) noexcept {
    switch (status) {
    case ExactClockStatus::Pending:
        return JudgementClockStatus::Pending;
    case ExactClockStatus::TemporarilyUnavailable:
        return JudgementClockStatus::TemporarilyUnavailable;
    case ExactClockStatus::HistoryLost:
        return before_binding
            ? JudgementClockStatus::HistoryLostBeforeBinding
            : JudgementClockStatus::UnsupportedContinuity;
    case ExactClockStatus::Resolved:
        return JudgementClockStatus::Resolved;
    default:
        return JudgementClockStatus::UnsupportedContinuity;
    }
}

[[nodiscard]] JudgementClockResult EndpointFailure(
    const gc::audio::ExactOutputClockResult& endpoint,
    const bool before_binding) noexcept {
    return {
        .status = EndpointStatus(endpoint.status, before_binding),
        .output_frame = endpoint.output_frame,
        .endpoint_anchor_sequence = endpoint.anchor_sequence,
        .endpoint_position = endpoint.anchor_endpoint_position,
    };
}

} // namespace

void JudgementClockResolver::Reset(
    const std::uint64_t stage_generation,
    const std::int64_t stage_entry_qpc,
    const std::int32_t game_time_offset_ms) noexcept {
    binding_ = {
        .stage_generation = stage_generation,
        .stage_entry_qpc = stage_entry_qpc,
        .game_time_offset_ms = game_time_offset_ms,
    };
}

bool JudgementClockResolver::bound() const noexcept {
    return binding_.anchor.has_value();
}

const JudgementStageClockAnchor& JudgementClockResolver::anchor()
    const noexcept {
    return *binding_.anchor;
}

JudgementClockResult JudgementClockResolver::TryBind(
    const gc::audio::GameplayAudioCursorObservation& selected,
    std::shared_ptr<const gc::audio::ExactWasapiClock> endpoint,
    const std::span<ExactPlaybackEpoch> scratch) noexcept {
    if (bound()) {
        return ResolveQpc(binding_.stage_entry_qpc);
    }
    if (binding_.stage_generation == 0 ||
        binding_.stage_entry_qpc <= 0) {
        return {.status = JudgementClockStatus::UnsupportedContinuity};
    }
    if (!endpoint) {
        return {.status = JudgementClockStatus::Pending};
    }

    const auto endpoint_generation = endpoint->endpoint_generation();
    if (endpoint_generation == 0) {
        return {.status = JudgementClockStatus::UnsupportedContinuity};
    }
    if (binding_.pending_endpoint) {
        if (binding_.pending_endpoint.get() != endpoint.get() ||
            binding_.pending_endpoint->endpoint_generation() !=
                endpoint_generation) {
            return {.status = JudgementClockStatus::UnsupportedContinuity};
        }
    } else {
        binding_.pending_endpoint = endpoint;
    }
    if (selected.state != gc::audio::GameplayAudioCursorState::Exact ||
        !selected.exact_history || selected.buffer_instance_id == 0 ||
        selected.endpoint_generation == 0 ||
        selected.playback_generation == 0) {
        return {.status = JudgementClockStatus::Pending};
    }
    if (endpoint_generation != selected.endpoint_generation) {
        return {.status = JudgementClockStatus::UnsupportedContinuity};
    }
    if (binding_.pending_history) {
        if (binding_.pending_buffer_instance_id !=
                selected.buffer_instance_id ||
            binding_.pending_endpoint_generation !=
                selected.endpoint_generation ||
            binding_.pending_history.get() != selected.exact_history.get()) {
            return {.status = JudgementClockStatus::UnsupportedContinuity};
        }
    } else {
        binding_.pending_buffer_instance_id = selected.buffer_instance_id;
        binding_.pending_endpoint_generation = selected.endpoint_generation;
        binding_.pending_history = selected.exact_history;
    }

    if (!binding_.pending_history->HasExactPlaybackHistory() ||
        binding_.pending_history->exact_buffer_instance_id() !=
            binding_.pending_buffer_instance_id ||
        binding_.pending_history->exact_endpoint_generation() !=
            binding_.pending_endpoint_generation) {
        return {.status = JudgementClockStatus::UnsupportedContinuity};
    }

    const auto entry_output = endpoint->ResolveQpc(
        binding_.stage_entry_qpc);
    if (entry_output.endpoint_generation != endpoint_generation) {
        return {.status = JudgementClockStatus::UnsupportedContinuity};
    }
    if (entry_output.status != ExactClockStatus::Resolved ||
        !entry_output.output_frame) {
        return EndpointFailure(entry_output, true);
    }

    gc::audio::ExactPlaybackHistoryStatus history_status{};
    const auto count = binding_.pending_history->CopyExactPlaybackEpochs(
        scratch, &history_status);
    if (history_status.prefix_evicted ||
        history_status.status == ExactClockStatus::HistoryLost) {
        return {
            .status = JudgementClockStatus::HistoryLostBeforeBinding,
            .output_frame = entry_output.output_frame,
            .endpoint_anchor_sequence = entry_output.anchor_sequence,
            .endpoint_position = entry_output.anchor_endpoint_position,
        };
    }
    if (history_status.status == ExactClockStatus::Pending ||
        history_status.status == ExactClockStatus::NoPlayback || count == 0) {
        return {
            .status = JudgementClockStatus::Pending,
            .output_frame = entry_output.output_frame,
            .endpoint_anchor_sequence = entry_output.anchor_sequence,
            .endpoint_position = entry_output.anchor_endpoint_position,
        };
    }
    if (history_status.status == ExactClockStatus::TemporarilyUnavailable) {
        return {
            .status = JudgementClockStatus::TemporarilyUnavailable,
            .output_frame = entry_output.output_frame,
            .endpoint_anchor_sequence = entry_output.anchor_sequence,
            .endpoint_position = entry_output.anchor_endpoint_position,
        };
    }
    if (history_status.status != ExactClockStatus::Resolved) {
        return {
            .status = JudgementClockStatus::UnsupportedContinuity,
            .output_frame = entry_output.output_frame,
            .endpoint_anchor_sequence = entry_output.anchor_sequence,
            .endpoint_position = entry_output.anchor_endpoint_position,
        };
    }

    const ExactPlaybackEpoch* earliest{};
    for (std::size_t index = 0; index < count; ++index) {
        const auto& epoch = scratch[index];
        if (epoch.buffer_instance_id != selected.buffer_instance_id ||
            epoch.endpoint_generation != selected.endpoint_generation ||
            epoch.origin != ExactPlaybackOrigin::Play ||
            epoch.playback_generation == 0 ||
            epoch.playback_generation > selected.playback_generation) {
            continue;
        }
        const auto output_origin = WholeUnsigned(epoch.output_origin);
        if (!output_origin) {
            return {
                .status = JudgementClockStatus::CheckedArithmeticFailure,
            };
        }
        if (output_origin->Compare(*entry_output.output_frame) < 0) {
            continue;
        }
        if (epoch.output_rate == 0 || epoch.source_rate == 0) {
            return {
                .status = JudgementClockStatus::UnsupportedContinuity,
            };
        }
        if (earliest == nullptr ||
            epoch.output_origin < earliest->output_origin ||
            (epoch.output_origin == earliest->output_origin &&
             epoch.playback_generation < earliest->playback_generation)) {
            earliest = &epoch;
        }
    }
    if (earliest == nullptr) {
        return {
            .status = JudgementClockStatus::Pending,
            .output_frame = entry_output.output_frame,
            .endpoint_anchor_sequence = entry_output.anchor_sequence,
            .endpoint_position = entry_output.anchor_endpoint_position,
        };
    }

    binding_.anchor = JudgementStageClockAnchor{
        .stage_generation = binding_.stage_generation,
        .endpoint_generation = endpoint_generation,
        .buffer_instance_id = earliest->buffer_instance_id,
        .playback_generation = earliest->playback_generation,
        .output_origin = earliest->output_origin,
        .source_origin = earliest->source_origin,
        .output_rate = earliest->output_rate,
        .source_rate = earliest->source_rate,
        .game_time_offset_ms = binding_.game_time_offset_ms,
        .endpoint = std::move(binding_.pending_endpoint),
    };
    binding_.pending_history.reset();
    return ResolveQpc(binding_.stage_entry_qpc);
}

JudgementClockResult JudgementClockResolver::ResolveQpc(
    const std::int64_t qpc_ticks) const noexcept {
    if (!bound()) {
        return {.status = JudgementClockStatus::Pending};
    }
    const auto& stage_anchor = *binding_.anchor;
    if (!stage_anchor.endpoint || stage_anchor.endpoint_generation == 0 ||
        stage_anchor.endpoint->endpoint_generation() !=
            stage_anchor.endpoint_generation) {
        return {.status = JudgementClockStatus::UnsupportedContinuity};
    }

    const auto endpoint = stage_anchor.endpoint->ResolveQpc(qpc_ticks);
    if (endpoint.endpoint_generation != stage_anchor.endpoint_generation) {
        return {.status = JudgementClockStatus::UnsupportedContinuity};
    }
    if (endpoint.status != ExactClockStatus::Resolved ||
        !endpoint.output_frame) {
        return EndpointFailure(endpoint, false);
    }

    const auto output_origin = WholeUnsigned(stage_anchor.output_origin);
    const auto source_origin = WholeUnsigned(stage_anchor.source_origin);
    if (!output_origin || !source_origin || stage_anchor.output_rate == 0 ||
        stage_anchor.source_rate == 0) {
        return {.status = JudgementClockStatus::CheckedArithmeticFailure};
    }
    const auto source_origin_seconds = source_origin->Multiply(
        1, stage_anchor.source_rate);
    const auto game_offset_seconds =
        CheckedRational::Whole(stage_anchor.game_time_offset_ms)
            .Multiply(1, 1000);
    const auto output_delta = endpoint.output_frame->Subtract(*output_origin);
    if (!source_origin_seconds || !game_offset_seconds || !output_delta) {
        return {.status = JudgementClockStatus::CheckedArithmeticFailure};
    }
    const auto output_delta_seconds = output_delta->Multiply(
        1, stage_anchor.output_rate);
    const auto with_offset = source_origin_seconds->Add(
        *game_offset_seconds);
    if (!output_delta_seconds || !with_offset) {
        return {.status = JudgementClockStatus::CheckedArithmeticFailure};
    }
    const auto judgement = with_offset->Add(*output_delta_seconds);
    if (!judgement) {
        return {.status = JudgementClockStatus::CheckedArithmeticFailure};
    }

    return {
        .status = JudgementClockStatus::Resolved,
        .output_frame = endpoint.output_frame,
        .judgement_seconds = *judgement,
        .endpoint_anchor_sequence = endpoint.anchor_sequence,
        .endpoint_position = endpoint.anchor_endpoint_position,
    };
}

} // namespace gc::absolute_judgement
