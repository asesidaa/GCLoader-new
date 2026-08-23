#include "JudgementOffsetAdvisor.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <concepts>
#include <cstdint>
#include <fstream>
#include <format>
#include <limits>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gc::config_gui {
namespace {

constexpr std::string_view kAbsoluteJudgementMarker =
    "AbsoluteJudgement: ";
constexpr std::int64_t kGreatEarlyMilliseconds = -33;
constexpr std::int64_t kGreatLateMilliseconds = 33;

struct RecordLocation {
    std::size_t line{};
    std::string_view body;
};

struct TimingSample {
    std::int64_t raw_error_ms{};
    std::int32_t native_grade{};
};

struct TimingObservation {
    std::int32_t recognition_ms{};
    std::int32_t note_target_ms{};
    std::int32_t signed_error_ms{};
    std::int32_t native_grade{};
};

struct StageAccumulator {
    std::uint64_t generation{};
    bool opened{};
    bool activated{};
    bool ended{};
    bool end_record_activated{};
    bool terminated{};
    bool diagnostics_trustworthy{};
    NativeResultCounts native_results{};
    std::vector<TimingSample> eligible_samples;
    std::vector<std::int32_t> observed_offsets_ms;
};

struct IntegerSegment {
    std::int64_t lower{};
    std::int64_t upper{};
};

struct SweepEvent {
    std::int64_t coordinate{};
    std::int64_t delta{};
};

struct EstimatorSelection {
    JudgementOffsetEstimatorResult public_result{};
    bool ambiguous{};
};

[[nodiscard]] JudgementOffsetAdvisorError MakeError(
    const JudgementOffsetAdvisorErrorCode code,
    const std::size_t line,
    std::string message) {
    return {
        .code = code,
        .line = line,
        .message = std::move(message),
    };
}

[[nodiscard]] std::expected<std::string_view, JudgementOffsetAdvisorError>
RequireField(
    const RecordLocation& record,
    const std::string_view key) {
    const auto token = std::format("{}=", key);
    auto search_from = std::size_t{};
    while (search_from < record.body.size()) {
        const auto position = record.body.find(token, search_from);
        if (position == std::string_view::npos) {
            break;
        }

        const bool begins_token =
            position == 0 || record.body[position - 1] == ' ';
        if (begins_token) {
            const auto value_begin = position + token.size();
            const auto value_end = record.body.find(' ', value_begin);
            const auto value = record.body.substr(
                value_begin,
                value_end == std::string_view::npos
                    ? std::string_view::npos
                    : value_end - value_begin);
            if (value.empty()) {
                return std::unexpected(MakeError(
                    JudgementOffsetAdvisorErrorCode::malformed_record,
                    record.line,
                    std::format(
                        "Malformed loader log at line {}: field {} is empty.",
                        record.line,
                        key)));
            }
            return value;
        }
        search_from = position + token.size();
    }

    return std::unexpected(MakeError(
        JudgementOffsetAdvisorErrorCode::malformed_record,
        record.line,
        std::format(
            "Malformed loader log at line {}: missing field {}.",
            record.line,
            key)));
}

template<std::integral T>
[[nodiscard]] std::expected<T, JudgementOffsetAdvisorError>
ParseDecimalField(
    const RecordLocation& record,
    const std::string_view key) {
    const auto field = RequireField(record, key);
    if (!field) {
        return std::unexpected(field.error());
    }

    T value{};
    const auto [end, error] = std::from_chars(
        field->data(),
        field->data() + field->size(),
        value,
        10);
    if (error != std::errc{} || end != field->data() + field->size()) {
        return std::unexpected(MakeError(
            JudgementOffsetAdvisorErrorCode::malformed_record,
            record.line,
            std::format(
                "Malformed loader log at line {}: field {} has invalid "
                "decimal value {}.",
                record.line,
                key,
                *field)));
    }
    return value;
}

[[nodiscard]] std::expected<std::uint64_t, JudgementOffsetAdvisorError>
ParseStageGeneration(const RecordLocation& record) {
    return ParseDecimalField<std::uint64_t>(record, "stage_generation");
}

[[nodiscard]] bool IsRecord(
    const std::string_view body,
    const std::string_view name) noexcept {
    return body.starts_with(name) &&
        (body.size() == name.size() || body[name.size()] == ' ');
}

[[nodiscard]] std::size_t FindToken(
    const std::string_view text,
    const std::string_view token,
    std::size_t search_from) noexcept {
    while (search_from < text.size()) {
        const auto position = text.find(token, search_from);
        if (position == std::string_view::npos) {
            return position;
        }

        const auto after = position + token.size();
        const bool begins_token = position == 0 || text[position - 1] == ' ';
        const bool ends_token = after == text.size() || text[after] == ' ';
        if (begins_token && ends_token) {
            return position;
        }
        search_from = position + token.size();
    }
    return std::string_view::npos;
}

[[nodiscard]] StageAccumulator* FindStage(
    std::vector<StageAccumulator>& stages,
    const std::uint64_t generation) noexcept {
    const auto found = std::ranges::find(
        stages,
        generation,
        &StageAccumulator::generation);
    return found == stages.end() ? nullptr : &*found;
}

[[nodiscard]] bool IsAcceptedStage(
    const StageAccumulator& stage) noexcept {
    return stage.opened &&
        stage.activated &&
        stage.ended &&
        stage.end_record_activated &&
        !stage.terminated &&
        stage.diagnostics_trustworthy;
}

void AddCounts(
    NativeResultCounts& destination,
    const NativeResultCounts& source) noexcept {
    destination.miss += source.miss;
    destination.good += source.good;
    destination.cool += source.cool;
    destination.great += source.great;
}

[[nodiscard]] bool GradeMatchesScoreDelta(
    const std::int32_t grade,
    const NativeResultCounts& deltas) noexcept {
    switch (grade) {
    case 0:
        return deltas.miss == 1 &&
            deltas.good == 0 &&
            deltas.cool == 0 &&
            deltas.great == 0;
    case 1:
        return deltas.miss == 0 &&
            deltas.good == 1 &&
            deltas.cool == 0 &&
            deltas.great == 0;
    case 2:
        return deltas.miss == 0 &&
            deltas.good == 0 &&
            deltas.cool == 1 &&
            deltas.great == 0;
    case 3:
        return deltas.miss == 0 &&
            deltas.good == 0 &&
            deltas.cool == 0 &&
            deltas.great == 1;
    default:
        return false;
    }
}

[[nodiscard]] std::expected<TimingObservation, JudgementOffsetAdvisorError>
ParseTimingObservation(const RecordLocation& record) {
    const auto recognition =
        ParseDecimalField<std::int32_t>(record, "recognition_ms");
    if (!recognition) {
        return std::unexpected(recognition.error());
    }
    const auto target =
        ParseDecimalField<std::int32_t>(record, "note_target_ms");
    if (!target) {
        return std::unexpected(target.error());
    }
    const auto signed_error =
        ParseDecimalField<std::int32_t>(record, "signed_error_ms");
    if (!signed_error) {
        return std::unexpected(signed_error.error());
    }
    const auto grade =
        ParseDecimalField<std::int32_t>(record, "native_grade");
    if (!grade) {
        return std::unexpected(grade.error());
    }

    const auto reconstructed_error =
        static_cast<std::int64_t>(*recognition) -
        static_cast<std::int64_t>(*target);
    if (reconstructed_error != *signed_error) {
        return std::unexpected(MakeError(
            JudgementOffsetAdvisorErrorCode::malformed_record,
            record.line,
            std::format(
                "Malformed loader log at line {}: signed_error_ms does not "
                "match recognition_ms - note_target_ms.",
                record.line)));
    }
    if (*grade < 0 || *grade > 3) {
        return std::unexpected(MakeError(
            JudgementOffsetAdvisorErrorCode::malformed_record,
            record.line,
            std::format(
                "Malformed loader log at line {}: unsupported native_grade {}.",
                record.line,
                *grade)));
    }

    return TimingObservation{
        .recognition_ms = *recognition,
        .note_target_ms = *target,
        .signed_error_ms = *signed_error,
        .native_grade = *grade,
    };
}

[[nodiscard]] std::expected<void, JudgementOffsetAdvisorError>
ParseScopeEntry(
    const RecordLocation& record,
    StageAccumulator& stage) {
    const auto kind = RequireField(record, "kind");
    if (!kind) {
        return std::unexpected(kind.error());
    }
    const auto native_ms =
        ParseDecimalField<std::int32_t>(record, "native_ms");
    if (!native_ms) {
        return std::unexpected(native_ms.error());
    }

    const auto pressed_true =
        ParseDecimalField<std::uint64_t>(
            record,
            "scope_query_pressed_true");
    if (!pressed_true) {
        return std::unexpected(pressed_true.error());
    }
    const auto held_true =
        ParseDecimalField<std::uint64_t>(
            record,
            "scope_query_held_true");
    if (!held_true) {
        return std::unexpected(held_true.error());
    }
    const auto direction_nonzero =
        ParseDecimalField<std::uint64_t>(
            record,
            "scope_query_direction_nonzero");
    if (!direction_nonzero) {
        return std::unexpected(direction_nonzero.error());
    }
    const auto timing_calls =
        ParseDecimalField<std::uint64_t>(
            record,
            "scope_timing_grade_calls");
    if (!timing_calls) {
        return std::unexpected(timing_calls.error());
    }
    const auto timing_records =
        ParseDecimalField<std::size_t>(
            record,
            "scope_timing_grade_records");
    if (!timing_records) {
        return std::unexpected(timing_records.error());
    }
    const auto timing_drops =
        ParseDecimalField<std::uint64_t>(
            record,
            "scope_timing_grade_drops");
    if (!timing_drops) {
        return std::unexpected(timing_drops.error());
    }

    const auto score_miss =
        ParseDecimalField<std::uint64_t>(
            record,
            "scope_score_miss_delta");
    if (!score_miss) {
        return std::unexpected(score_miss.error());
    }
    const auto score_good =
        ParseDecimalField<std::uint64_t>(
            record,
            "scope_score_good_delta");
    if (!score_good) {
        return std::unexpected(score_good.error());
    }
    const auto score_cool =
        ParseDecimalField<std::uint64_t>(
            record,
            "scope_score_cool_delta");
    if (!score_cool) {
        return std::unexpected(score_cool.error());
    }
    const auto score_great =
        ParseDecimalField<std::uint64_t>(
            record,
            "scope_score_great_delta");
    if (!score_great) {
        return std::unexpected(score_great.error());
    }

    std::vector<TimingObservation> observations;
    auto cursor = std::size_t{};
    while (true) {
        const auto begin =
            FindToken(record.body, "scope_timing_begin", cursor);
        if (begin == std::string_view::npos) {
            break;
        }
        const auto body_begin =
            begin + std::string_view{"scope_timing_begin"}.size();
        const auto end =
            FindToken(record.body, "scope_timing_end", body_begin);
        if (end == std::string_view::npos) {
            return std::unexpected(MakeError(
                JudgementOffsetAdvisorErrorCode::malformed_record,
                record.line,
                std::format(
                    "Malformed loader log at line {}: scope timing block "
                    "has no scope_timing_end.",
                    record.line)));
        }

        const auto timing = ParseTimingObservation({
            .line = record.line,
            .body = record.body.substr(body_begin, end - body_begin),
        });
        if (!timing) {
            return std::unexpected(timing.error());
        }
        observations.push_back(*timing);
        cursor = end + std::string_view{"scope_timing_end"}.size();
    }

    if (observations.size() != *timing_records) {
        return std::unexpected(MakeError(
            JudgementOffsetAdvisorErrorCode::malformed_record,
            record.line,
            std::format(
                "Malformed loader log at line {}: declared {} timing "
                "records but found {}.",
                record.line,
                *timing_records,
                observations.size())));
    }

    for (const auto& observation : observations) {
        const auto observed_offset =
            static_cast<std::int64_t>(observation.recognition_ms) -
            static_cast<std::int64_t>(*native_ms);
        if (observed_offset <
                (std::numeric_limits<std::int32_t>::min)() ||
            observed_offset >
                (std::numeric_limits<std::int32_t>::max)()) {
            return std::unexpected(MakeError(
                JudgementOffsetAdvisorErrorCode::malformed_record,
                record.line,
                std::format(
                    "Malformed loader log at line {}: observed gameplay "
                    "offset is outside the supported signed range.",
                    record.line)));
        }
        stage.observed_offsets_ms.push_back(
            static_cast<std::int32_t>(observed_offset));
    }

    const NativeResultCounts score_deltas{
        .miss = *score_miss,
        .good = *score_good,
        .cool = *score_cool,
        .great = *score_great,
    };
    const bool one_score_delta =
        score_deltas.miss <= 1 &&
        score_deltas.good <= 1 &&
        score_deltas.cool <= 1 &&
        score_deltas.great <= 1 &&
        score_deltas.miss +
                score_deltas.good +
                score_deltas.cool +
                score_deltas.great ==
            1;

    const bool eligible =
        *kind == "event" &&
        *pressed_true == 1 &&
        *held_true == 0 &&
        *direction_nonzero == 0 &&
        *timing_calls == 1 &&
        *timing_records == 1 &&
        *timing_drops == 0 &&
        observations.size() == 1 &&
        one_score_delta &&
        GradeMatchesScoreDelta(
            observations.front().native_grade,
            score_deltas);
    if (!eligible) {
        return {};
    }

    const auto raw_error =
        static_cast<std::int64_t>(*native_ms) -
        static_cast<std::int64_t>(
            observations.front().note_target_ms);
    if (raw_error <
            (std::numeric_limits<std::int32_t>::min)() ||
        raw_error >
            (std::numeric_limits<std::int32_t>::max)()) {
        return std::unexpected(MakeError(
            JudgementOffsetAdvisorErrorCode::malformed_record,
            record.line,
            std::format(
                "Malformed loader log at line {}: raw timing error is "
                "outside the supported signed range.",
                record.line)));
    }

    stage.eligible_samples.push_back({
        .raw_error_ms = raw_error,
        .native_grade = observations.front().native_grade,
    });
    return {};
}

[[nodiscard]] std::expected<void, JudgementOffsetAdvisorError>
ParseScopeTrace(
    const RecordLocation& record,
    StageAccumulator& stage) {
    const auto declared_entries =
        ParseDecimalField<std::size_t>(record, "entries_in_part");
    if (!declared_entries) {
        return std::unexpected(declared_entries.error());
    }

    auto cursor = std::size_t{};
    auto parsed_entries = std::size_t{};
    while (true) {
        const auto begin = FindToken(record.body, "entry_begin", cursor);
        if (begin == std::string_view::npos) {
            break;
        }
        const auto body_begin =
            begin + std::string_view{"entry_begin"}.size();
        const auto end =
            FindToken(record.body, "entry_end", body_begin);
        if (end == std::string_view::npos) {
            return std::unexpected(MakeError(
                JudgementOffsetAdvisorErrorCode::malformed_record,
                record.line,
                std::format(
                    "Malformed loader log at line {}: scope entry has no "
                    "entry_end.",
                    record.line)));
        }

        const auto parsed = ParseScopeEntry(
            {
                .line = record.line,
                .body = record.body.substr(body_begin, end - body_begin),
            },
            stage);
        if (!parsed) {
            return std::unexpected(parsed.error());
        }

        ++parsed_entries;
        cursor = end + std::string_view{"entry_end"}.size();
    }

    if (parsed_entries != *declared_entries) {
        return std::unexpected(MakeError(
            JudgementOffsetAdvisorErrorCode::malformed_record,
            record.line,
            std::format(
                "Malformed loader log at line {}: declared {} scope "
                "entries but found {}.",
                record.line,
                *declared_entries,
                parsed_entries)));
    }
    return {};
}

[[nodiscard]] std::expected<NativeResultCounts, JudgementOffsetAdvisorError>
ParseNativeResultCounts(const RecordLocation& record) {
    const auto miss =
        ParseDecimalField<std::uint64_t>(
            record,
            "cumulative_score_miss_delta");
    if (!miss) {
        return std::unexpected(miss.error());
    }
    const auto good =
        ParseDecimalField<std::uint64_t>(
            record,
            "cumulative_score_good_delta");
    if (!good) {
        return std::unexpected(good.error());
    }
    const auto cool =
        ParseDecimalField<std::uint64_t>(
            record,
            "cumulative_score_cool_delta");
    if (!cool) {
        return std::unexpected(cool.error());
    }
    const auto great =
        ParseDecimalField<std::uint64_t>(
            record,
            "cumulative_score_great_delta");
    if (!great) {
        return std::unexpected(great.error());
    }

    return NativeResultCounts{
        .miss = *miss,
        .good = *good,
        .cool = *cool,
        .great = *great,
    };
}

[[nodiscard]] std::expected<bool, JudgementOffsetAdvisorError>
StageDiagnosticsTrustworthy(const RecordLocation& record) {
    constexpr std::array<std::string_view, 10> diagnostic_fields{
        "cumulative_timing_grade_drops",
        "cumulative_scope_trace_drops",
        "cumulative_score_observation_read_failures",
        "cumulative_score_counter_regressions",
        "cumulative_final_accounting_mismatches",
        "cumulative_clock_unavailable",
        "cumulative_sequence_errors",
        "cumulative_overload_drops",
        "cumulative_cleanup_drops",
        "cumulative_rounded_fallback",
    };

    auto trustworthy = true;
    for (const auto field : diagnostic_fields) {
        const auto value =
            ParseDecimalField<std::uint64_t>(record, field);
        if (!value) {
            return std::unexpected(value.error());
        }
        trustworthy = trustworthy && *value == 0;
    }
    return trustworthy;
}

[[nodiscard]] std::expected<void, JudgementOffsetAdvisorError>
ParseStageEnd(
    const RecordLocation& record,
    StageAccumulator& stage) {
    const auto activated =
        ParseDecimalField<std::uint32_t>(record, "activated");
    if (!activated) {
        return std::unexpected(activated.error());
    }
    if (*activated > 1) {
        return std::unexpected(MakeError(
            JudgementOffsetAdvisorErrorCode::malformed_record,
            record.line,
            std::format(
                "Malformed loader log at line {}: activated must be 0 or 1.",
                record.line)));
    }

    const auto counts = ParseNativeResultCounts(record);
    if (!counts) {
        return std::unexpected(counts.error());
    }
    const auto trustworthy = StageDiagnosticsTrustworthy(record);
    if (!trustworthy) {
        return std::unexpected(trustworthy.error());
    }

    stage.ended = true;
    stage.end_record_activated = *activated == 1;
    stage.native_results = *counts;
    stage.diagnostics_trustworthy = *trustworthy;
    return {};
}

[[nodiscard]] std::int64_t MedianTwice(
    const std::span<const std::int64_t> sorted_values) noexcept {
    const auto count = sorted_values.size();
    if (count % 2 != 0) {
        return sorted_values[count / 2] * 2;
    }
    return sorted_values[count / 2 - 1] + sorted_values[count / 2];
}

[[nodiscard]] std::int64_t MedianAbsoluteDeviationTwice(
    const std::span<const std::int64_t> sorted_values,
    const std::int64_t median_twice) {
    std::vector<std::int64_t> deviations_twice;
    deviations_twice.reserve(sorted_values.size());
    for (const auto value : sorted_values) {
        const auto difference = value * 2 - median_twice;
        deviations_twice.push_back(
            difference < 0 ? -difference : difference);
    }
    std::ranges::sort(deviations_twice);

    const auto doubled_median_of_doubled_deviations =
        MedianTwice(deviations_twice);
    return doubled_median_of_doubled_deviations / 2;
}

[[nodiscard]] std::vector<std::int64_t> TrimSymmetric(
    const std::span<const std::int64_t> sorted_values,
    const std::uint32_t fraction_numerator,
    const std::uint32_t fraction_denominator) {
    const auto tail = sorted_values.size() * fraction_numerator /
        fraction_denominator;
    return {
        sorted_values.begin() +
            static_cast<std::ptrdiff_t>(tail),
        sorted_values.end() -
            static_cast<std::ptrdiff_t>(tail),
    };
}

[[nodiscard]] std::vector<std::int64_t> RetainWithinTwoMad(
    const std::span<const std::int64_t> sorted_values,
    const std::int64_t median_twice,
    const std::int64_t mad_twice) {
    std::vector<std::int64_t> retained;
    retained.reserve(sorted_values.size());
    for (const auto value : sorted_values) {
        const auto difference = value * 2 - median_twice;
        const auto absolute_difference =
            difference < 0 ? -difference : difference;
        if (absolute_difference <= mad_twice * 2) {
            retained.push_back(value);
        }
    }
    return retained;
}

[[nodiscard]] std::uint64_t AbsoluteLoss(
    const std::span<const std::int64_t> errors,
    const std::int64_t candidate) noexcept {
    auto total = std::uint64_t{};
    for (const auto error : errors) {
        const auto signed_distance = error + candidate;
        const auto magnitude = signed_distance < 0
            ? static_cast<std::uint64_t>(-(signed_distance + 1)) + 1
            : static_cast<std::uint64_t>(signed_distance);
        if ((std::numeric_limits<std::uint64_t>::max)() - total <
            magnitude) {
            return (std::numeric_limits<std::uint64_t>::max)();
        }
        total += magnitude;
    }
    return total;
}

[[nodiscard]] std::int64_t MidpointRoundedHalfAwayFromZero(
    const std::int64_t lower,
    const std::int64_t upper) noexcept {
    const auto sum = lower + upper;
    return sum >= 0 ? (sum + 1) / 2 : (sum - 1) / 2;
}

[[nodiscard]] std::vector<IntegerSegment> MaximumOverlapSegments(
    const std::span<const std::int64_t> errors,
    std::size_t& maximum_overlap) {
    std::vector<SweepEvent> events;
    events.reserve(errors.size() * 2);
    for (const auto error : errors) {
        const auto lower = kGreatEarlyMilliseconds - error;
        const auto upper = kGreatLateMilliseconds - error;
        events.push_back({.coordinate = lower, .delta = 1});
        events.push_back({.coordinate = upper + 1, .delta = -1});
    }
    std::ranges::sort(
        events,
        {},
        &SweepEvent::coordinate);

    auto active = std::int64_t{};
    maximum_overlap = 0;
    std::vector<IntegerSegment> maximum_segments;

    auto index = std::size_t{};
    while (index < events.size()) {
        const auto coordinate = events[index].coordinate;
        auto delta = std::int64_t{};
        while (index < events.size() &&
            events[index].coordinate == coordinate) {
            delta += events[index].delta;
            ++index;
        }
        active += delta;

        if (index == events.size()) {
            break;
        }
        const auto next_coordinate = events[index].coordinate;
        if (active <= 0 || coordinate >= next_coordinate) {
            continue;
        }

        const auto overlap = static_cast<std::size_t>(active);
        const IntegerSegment segment{
            .lower = coordinate,
            .upper = next_coordinate - 1,
        };
        if (overlap > maximum_overlap) {
            maximum_overlap = overlap;
            maximum_segments.clear();
            maximum_segments.push_back(segment);
        } else if (overlap == maximum_overlap) {
            if (!maximum_segments.empty() &&
                maximum_segments.back().upper + 1 == segment.lower) {
                maximum_segments.back().upper = segment.upper;
            } else {
                maximum_segments.push_back(segment);
            }
        }
    }
    return maximum_segments;
}

[[nodiscard]] EstimatorSelection BuildEstimator(
    const JudgementOffsetEstimatorRule rule,
    std::vector<std::int64_t> retained) {
    EstimatorSelection selection{
        .public_result = {
            .rule = rule,
            .retained_samples = retained.size(),
        },
    };
    if (retained.empty()) {
        selection.ambiguous = true;
        return selection;
    }

    std::ranges::sort(retained);
    auto maximum_overlap = std::size_t{};
    const auto maximum_segments =
        MaximumOverlapSegments(retained, maximum_overlap);
    selection.public_result.maximum_great = maximum_overlap;

    std::vector<std::int64_t> centers;
    centers.reserve(retained.size());
    for (const auto error : retained) {
        centers.push_back(-error);
    }
    std::ranges::sort(centers);
    const auto median_lower = centers[(centers.size() - 1) / 2];
    const auto median_upper = centers[centers.size() / 2];

    auto minimum_loss =
        (std::numeric_limits<std::uint64_t>::max)();
    std::vector<IntegerSegment> minimum_intervals;
    for (const auto& segment : maximum_segments) {
        IntegerSegment local_minimum{};
        if (segment.upper < median_lower) {
            local_minimum = {
                .lower = segment.upper,
                .upper = segment.upper,
            };
        } else if (segment.lower > median_upper) {
            local_minimum = {
                .lower = segment.lower,
                .upper = segment.lower,
            };
        } else {
            local_minimum = {
                .lower = (std::max)(segment.lower, median_lower),
                .upper = (std::min)(segment.upper, median_upper),
            };
        }

        const auto loss = AbsoluteLoss(retained, local_minimum.lower);
        if (loss < minimum_loss) {
            minimum_loss = loss;
            minimum_intervals.clear();
            minimum_intervals.push_back(local_minimum);
        } else if (loss == minimum_loss) {
            minimum_intervals.push_back(local_minimum);
        }
    }

    if (minimum_intervals.size() != 1) {
        selection.ambiguous = true;
        return selection;
    }

    const auto centered = MidpointRoundedHalfAwayFromZero(
        minimum_intervals.front().lower,
        minimum_intervals.front().upper);
    if (centered < (std::numeric_limits<std::int32_t>::min)() ||
        centered > (std::numeric_limits<std::int32_t>::max)()) {
        selection.ambiguous = true;
        return selection;
    }

    selection.public_result.centered_offset_ms =
        static_cast<std::int32_t>(centered);
    return selection;
}

[[nodiscard]] std::int32_t MedianOfFourRoundedHalfAwayFromZero(
    std::array<std::int32_t, 4> values) noexcept {
    std::ranges::sort(values);
    return static_cast<std::int32_t>(
        MidpointRoundedHalfAwayFromZero(values[1], values[2]));
}

[[nodiscard]] std::size_t ProjectGreat(
    const std::span<const std::int64_t> errors,
    const std::int32_t offset_ms) noexcept {
    return static_cast<std::size_t>(std::ranges::count_if(
        errors,
        [offset_ms](const std::int64_t error) {
            const auto adjusted =
                error + static_cast<std::int64_t>(offset_ms);
            return adjusted >= kGreatEarlyMilliseconds &&
                adjusted <= kGreatLateMilliseconds;
        }));
}

[[nodiscard]] std::expected<void, JudgementOffsetAdvisorError>
ProcessRecord(
    const RecordLocation& record,
    std::vector<StageAccumulator>& stages) {
    if (IsRecord(record.body, "semantic-stage-open")) {
        const auto generation = ParseStageGeneration(record);
        if (!generation) {
            return std::unexpected(generation.error());
        }
        if (FindStage(stages, *generation) != nullptr) {
            return std::unexpected(MakeError(
                JudgementOffsetAdvisorErrorCode::malformed_record,
                record.line,
                std::format(
                    "Malformed loader log at line {}: duplicate semantic "
                    "stage generation {}.",
                    record.line,
                    *generation)));
        }
        stages.push_back({
            .generation = *generation,
            .opened = true,
        });
        return {};
    }

    if (IsRecord(record.body, "absolute-stage-activation")) {
        const auto generation = ParseStageGeneration(record);
        if (!generation) {
            return std::unexpected(generation.error());
        }
        auto* stage = FindStage(stages, *generation);
        if (stage == nullptr || stage->activated || stage->ended) {
            return std::unexpected(MakeError(
                JudgementOffsetAdvisorErrorCode::malformed_record,
                record.line,
                std::format(
                    "Malformed loader log at line {}: invalid activation "
                    "for stage generation {}.",
                    record.line,
                    *generation)));
        }
        stage->activated = true;
        return {};
    }

    if (IsRecord(record.body, "scope-trace")) {
        const auto generation = ParseStageGeneration(record);
        if (!generation) {
            return std::unexpected(generation.error());
        }
        auto* stage = FindStage(stages, *generation);
        if (stage == nullptr || stage->ended) {
            return std::unexpected(MakeError(
                JudgementOffsetAdvisorErrorCode::malformed_record,
                record.line,
                std::format(
                    "Malformed loader log at line {}: scope trace is "
                    "outside stage generation {}.",
                    record.line,
                    *generation)));
        }
        return ParseScopeTrace(record, *stage);
    }

    if (IsRecord(record.body, "semantic-stage-end")) {
        const auto generation = ParseStageGeneration(record);
        if (!generation) {
            return std::unexpected(generation.error());
        }
        auto* stage = FindStage(stages, *generation);
        if (stage == nullptr || stage->ended) {
            return std::unexpected(MakeError(
                JudgementOffsetAdvisorErrorCode::malformed_record,
                record.line,
                std::format(
                    "Malformed loader log at line {}: invalid end for "
                    "stage generation {}.",
                    record.line,
                    *generation)));
        }
        return ParseStageEnd(record, *stage);
    }

    if (IsRecord(record.body, "semantic-stage-termination")) {
        const auto generation = ParseStageGeneration(record);
        if (!generation) {
            return std::unexpected(generation.error());
        }
        const auto source = RequireField(record, "source");
        if (!source) {
            return std::unexpected(source.error());
        }
        auto* stage = FindStage(stages, *generation);
        if (stage == nullptr || !stage->ended || stage->terminated) {
            return std::unexpected(MakeError(
                JudgementOffsetAdvisorErrorCode::malformed_record,
                record.line,
                std::format(
                    "Malformed loader log at line {}: invalid termination "
                    "for stage generation {}.",
                    record.line,
                    *generation)));
        }
        stage->terminated = true;
        return {};
    }

    return {};
}

} // namespace

std::expected<JudgementOffsetAnalysis, JudgementOffsetAdvisorError>
AnalyzeJudgementOffsetLog(const std::filesystem::path& log_path) {
    std::ifstream input{log_path};
    if (!input.is_open()) {
        return std::unexpected(MakeError(
            JudgementOffsetAdvisorErrorCode::cannot_open_log,
            0,
            std::format(
                "Could not open judgement log: {}",
                log_path.string())));
    }

    std::vector<StageAccumulator> stages;
    std::string line;
    auto line_number = std::size_t{};
    while (std::getline(input, line)) {
        ++line_number;
        const auto marker = line.find(kAbsoluteJudgementMarker);
        if (marker == std::string::npos) {
            continue;
        }

        const RecordLocation record{
            .line = line_number,
            .body = std::string_view{line}.substr(
                marker + kAbsoluteJudgementMarker.size()),
        };
        const auto processed = ProcessRecord(record, stages);
        if (!processed) {
            return std::unexpected(processed.error());
        }
    }
    if (input.bad()) {
        return std::unexpected(MakeError(
            JudgementOffsetAdvisorErrorCode::log_read_failed,
            line_number,
            std::format(
                "Could not finish reading judgement log: {}",
                log_path.string())));
    }

    JudgementOffsetAnalysis analysis;
    std::array<std::vector<std::int64_t>, 4> retained_by_rule;
    std::vector<std::int64_t> all_errors;
    std::vector<std::int32_t> all_observed_offsets;
    auto estimating_song_count = std::size_t{};

    for (const auto& stage : stages) {
        if (!IsAcceptedStage(stage)) {
            continue;
        }

        AddCounts(analysis.native_results, stage.native_results);
        all_observed_offsets.insert(
            all_observed_offsets.end(),
            stage.observed_offsets_ms.begin(),
            stage.observed_offsets_ms.end());

        std::vector<std::int64_t> sorted_errors;
        sorted_errors.reserve(stage.eligible_samples.size());
        for (const auto& sample : stage.eligible_samples) {
            sorted_errors.push_back(sample.raw_error_ms);
            all_errors.push_back(sample.raw_error_ms);
            analysis.observed_eligible_great +=
                sample.native_grade == 3 ? 1U : 0U;
        }
        std::ranges::sort(sorted_errors);

        JudgementOffsetSongStatistics song{
            .stage_generation = stage.generation,
            .eligible_judgements = sorted_errors.size(),
            .native_results = stage.native_results,
        };
        if (!sorted_errors.empty()) {
            ++estimating_song_count;
            const auto median_twice = MedianTwice(sorted_errors);
            const auto mad_twice = MedianAbsoluteDeviationTwice(
                sorted_errors,
                median_twice);
            song.median_error_before_offset_ms =
                static_cast<double>(median_twice) / 2.0;
            song.median_absolute_deviation_ms =
                static_cast<double>(mad_twice) / 2.0;

            const std::array retained{
                TrimSymmetric(sorted_errors, 5, 100),
                TrimSymmetric(sorted_errors, 75, 1000),
                TrimSymmetric(sorted_errors, 10, 100),
                RetainWithinTwoMad(
                    sorted_errors,
                    median_twice,
                    mad_twice),
            };
            for (std::size_t index = 0;
                index < retained.size();
                ++index) {
                retained_by_rule[index].insert(
                    retained_by_rule[index].end(),
                    retained[index].begin(),
                    retained[index].end());
            }
        }
        analysis.songs.push_back(song);
    }

    analysis.eligible_judgements = all_errors.size();
    if (all_observed_offsets.empty()) {
        analysis.observed_gameplay_offset.kind =
            ObservedGameplayOffsetKind::unavailable;
    } else {
        const auto first = all_observed_offsets.front();
        const auto varied = std::ranges::any_of(
            all_observed_offsets,
            [first](const std::int32_t value) {
                return value != first;
            });
        analysis.observed_gameplay_offset.kind = varied
            ? ObservedGameplayOffsetKind::varied
            : ObservedGameplayOffsetKind::uniform;
        analysis.observed_gameplay_offset.uniform_offset_ms = first;
    }

    if (all_errors.empty()) {
        return analysis;
    }

    JudgementOffsetEstimate estimate;
    constexpr std::array rules{
        JudgementOffsetEstimatorRule::trim_5_percent,
        JudgementOffsetEstimatorRule::trim_7_5_percent,
        JudgementOffsetEstimatorRule::trim_10_percent,
        JudgementOffsetEstimatorRule::two_mad,
    };

    auto ambiguous = false;
    std::array<std::int32_t, 4> centered_offsets{};
    for (std::size_t index = 0; index < rules.size(); ++index) {
        auto selection = BuildEstimator(
            rules[index],
            std::move(retained_by_rule[index]));
        estimate.estimators[index] = selection.public_result;
        if (selection.ambiguous ||
            !estimate.estimators[index].centered_offset_ms) {
            ambiguous = true;
        } else {
            centered_offsets[index] =
                *estimate.estimators[index].centered_offset_ms;
        }
    }

    if (ambiguous) {
        estimate.data_too_diverse = true;
        analysis.estimate = estimate;
        return analysis;
    }

    const auto [minimum, maximum] = std::ranges::minmax(centered_offsets);
    estimate.estimator_min_ms = minimum;
    estimate.estimator_max_ms = maximum;
    if (static_cast<std::int64_t>(maximum) -
            static_cast<std::int64_t>(minimum) <=
        3) {
        const auto suggestion =
            MedianOfFourRoundedHalfAwayFromZero(centered_offsets);
        estimate.suggested_offset_ms = suggestion;
        estimate.projected_eligible_great =
            ProjectGreat(all_errors, suggestion);
        analysis.suggestion_strength =
            estimating_song_count == 1
            ? JudgementOffsetSuggestionStrength::provisional
            : JudgementOffsetSuggestionStrength::stable;
    }

    analysis.estimate = estimate;
    return analysis;
}

} // namespace gc::config_gui
