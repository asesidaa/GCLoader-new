#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace gc::config_gui {

struct NativeResultCounts {
    std::uint64_t miss{};
    std::uint64_t good{};
    std::uint64_t cool{};
    std::uint64_t great{};
};

struct JudgementOffsetSongStatistics {
    std::uint64_t stage_generation{};
    std::size_t eligible_judgements{};
    double median_error_before_offset_ms{};
    double median_absolute_deviation_ms{};
    NativeResultCounts native_results{};
};

enum class JudgementOffsetEstimatorRule : std::uint8_t {
    trim_5_percent,
    trim_7_5_percent,
    trim_10_percent,
    two_mad,
};

struct JudgementOffsetEstimatorResult {
    JudgementOffsetEstimatorRule rule{};
    std::size_t retained_samples{};
    std::size_t maximum_great{};
    std::optional<std::int32_t> centered_offset_ms;
};

struct JudgementOffsetEstimate {
    std::array<JudgementOffsetEstimatorResult, 4> estimators{};
    std::optional<std::int32_t> estimator_min_ms;
    std::optional<std::int32_t> estimator_max_ms;
    std::optional<std::int32_t> suggested_offset_ms;
    std::optional<std::size_t> projected_eligible_great;
    bool data_too_diverse{};
};

enum class ObservedGameplayOffsetKind : std::uint8_t {
    unavailable,
    uniform,
    varied,
};

struct ObservedGameplayOffset {
    ObservedGameplayOffsetKind kind{
        ObservedGameplayOffsetKind::unavailable};
    std::int32_t uniform_offset_ms{};
};

enum class JudgementOffsetSuggestionStrength : std::uint8_t {
    provisional,
    stable,
};

struct JudgementOffsetAnalysis {
    std::vector<JudgementOffsetSongStatistics> songs;
    NativeResultCounts native_results{};
    std::size_t eligible_judgements{};
    std::size_t observed_eligible_great{};
    ObservedGameplayOffset observed_gameplay_offset{};
    std::optional<JudgementOffsetEstimate> estimate;
    std::optional<JudgementOffsetSuggestionStrength> suggestion_strength;
};

enum class JudgementOffsetAdvisorErrorCode : std::uint8_t {
    cannot_open_log,
    log_read_failed,
    malformed_record,
};

struct JudgementOffsetAdvisorError {
    JudgementOffsetAdvisorErrorCode code{};
    std::size_t line{};
    std::string message;
};

[[nodiscard]] std::expected<
    JudgementOffsetAnalysis,
    JudgementOffsetAdvisorError>
AnalyzeJudgementOffsetLog(const std::filesystem::path& log_path);

} // namespace gc::config_gui
