#pragma once

#include "Input/Polling/GameplayTransitionJournal.h"
#include "Timing/CheckedRational.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>

namespace gc::absolute_judgement {

inline constexpr std::size_t kJudgementHistoryCapacity = 65'536;
inline constexpr std::uint32_t kJudgementLogicalControlCount = 20;

struct ResolvedGameplayTransition {
    gc::input::GameplayTransitionRecord transport{};
    gc::timing::CheckedRational judgement_seconds =
        gc::timing::CheckedRational::Whole(0);
};

struct JudgementScopeCoordinate {
    gc::timing::CheckedRational judgement_seconds =
        gc::timing::CheckedRational::Whole(0);
    std::uint64_t sequence{};
};

enum class BaselineOnlyReason : std::uint8_t {
    OutsidePlayback,
    AcceptedLate,
    Overload,
};

enum class JudgementScopeKind : std::uint8_t {
    Event,
    Heartbeat,
};

enum class JudgementHistoryError : std::uint8_t {
    NotInitialized,
    TransportEpochMismatch,
    SequenceDiscontinuity,
    TransportStateMismatch,
    BackwardTime,
    HistoryLost,
    InvalidControl,
    CheckedArithmeticFailure,
};

struct JudgementHeldState {
    bool held{};
    bool stale{};
    std::optional<JudgementScopeCoordinate> accepted_rise;
};

class JudgementHistory final {
public:
    void Reset(std::uint64_t transport_epoch,
               std::uint64_t cutoff_sequence,
               gc::input::GameplayHeldMask baseline) noexcept;

    [[nodiscard]] std::expected<void, JudgementHistoryError> Append(
        const ResolvedGameplayTransition& transition) noexcept;
    [[nodiscard]] std::expected<void, JudgementHistoryError>
    ApplyBaselineOnly(const gc::input::GameplayTransitionRecord& transition,
                      BaselineOnlyReason reason) noexcept;
    [[nodiscard]] std::expected<std::uint64_t, JudgementHistoryError>
    CountResolvedAtOrBefore(
        std::uint64_t first_sequence,
        const gc::timing::CheckedRational& ready) const noexcept;
    [[nodiscard]] std::expected<void, JudgementHistoryError>
    ConvertResolvedToBaselineOnly(std::uint64_t sequence,
                                  BaselineOnlyReason reason) noexcept;

    // earliest_query_time is the oldest exact time any pending or future
    // scope can request, including relative-frame translation. The exclusive
    // prefix end is independent of the scope's delivery-order sequence (a
    // heartbeat may use a sentinel there). The implementation additionally
    // retains the inclusive 4Q edge suffix.
    [[nodiscard]] std::expected<void, JudgementHistoryError> PruneBefore(
        const gc::timing::CheckedRational& earliest_query_time,
        std::uint64_t earliest_history_prefix_end_sequence) noexcept;

    [[nodiscard]] std::expected<bool, JudgementHistoryError> HeldAt(
        std::uint32_t control,
        const gc::timing::CheckedRational& query_time,
        std::uint64_t history_prefix_end_sequence) const noexcept;
    [[nodiscard]] std::expected<gc::input::GameplayHeldMask,
                                JudgementHistoryError>
    OrdinaryHeldAt(
        const gc::timing::CheckedRational& query_time,
        std::uint64_t history_prefix_end_sequence) const noexcept;
    [[nodiscard]] std::expected<bool, JudgementHistoryError> Pressed(
        std::uint32_t control,
        JudgementScopeKind kind,
        const JudgementScopeCoordinate& coordinate,
        gc::input::GameplayHeldMask current_rising) const noexcept;
    [[nodiscard]] std::expected<bool, JudgementHistoryError> Released(
        std::uint32_t control,
        JudgementScopeKind kind,
        const JudgementScopeCoordinate& coordinate,
        gc::input::GameplayHeldMask current_falling) const noexcept;
    [[nodiscard]] std::expected<std::int32_t, JudgementHistoryError> HeldAge(
        std::uint32_t control,
        JudgementScopeKind kind,
        const JudgementScopeCoordinate& coordinate,
        std::uint64_t history_prefix_end_sequence,
        gc::input::GameplayHeldMask current_held_before,
        gc::input::GameplayHeldMask current_held_after) const noexcept;

    [[nodiscard]] const ResolvedGameplayTransition* FindResolvedTransition(
        std::uint64_t sequence) const noexcept;
    [[nodiscard]] const ResolvedGameplayTransition* FirstResolvedAtOrAfter(
        std::uint64_t sequence) const noexcept;
    [[nodiscard]] std::uint64_t transport_epoch() const noexcept;
    [[nodiscard]] std::uint64_t next_sequence() const noexcept;
    [[nodiscard]] gc::input::GameplayHeldMask current_held() const noexcept;
    [[nodiscard]] std::size_t retained_entry_count() const noexcept;

private:
    struct LogicalRiseState final {
        bool stale{};
        std::optional<JudgementScopeCoordinate> accepted_rise;
    };

    struct CausalState final {
        gc::input::GameplayHeldMask ordinary_held{};
        std::array<LogicalRiseState, kJudgementLogicalControlCount> rises{};
    };

    struct RetainedEntry final {
        ResolvedGameplayTransition transition{};
        BaselineOnlyReason baseline_reason{BaselineOnlyReason::OutsidePlayback};
        bool resolved{};
    };

    [[nodiscard]] std::expected<void, JudgementHistoryError>
    ValidateTransport(const gc::input::GameplayTransitionRecord& transition)
        const noexcept;
    [[nodiscard]] std::expected<CausalState, JudgementHistoryError> StateAt(
        const gc::timing::CheckedRational& query_time,
        std::uint64_t history_prefix_end_sequence) const noexcept;
    [[nodiscard]] std::expected<bool, JudgementHistoryError> Edge(
        std::uint32_t control,
        JudgementScopeKind kind,
        const JudgementScopeCoordinate& coordinate,
        gc::input::GameplayHeldMask current_edges,
        bool rising) const noexcept;
    [[nodiscard]] std::expected<bool, JudgementHistoryError> HasPriorEdge(
        std::uint32_t ordinary_control,
        const JudgementScopeCoordinate& coordinate,
        bool rising) const noexcept;

    static bool LogicalHeld(gc::input::GameplayHeldMask ordinary_held,
                            std::uint32_t control) noexcept;
    static void ApplyToState(CausalState& state,
                             const RetainedEntry& entry) noexcept;
    static bool IsValidMask(gc::input::GameplayHeldMask mask) noexcept;

    [[nodiscard]] const RetainedEntry& EntryAt(std::size_t offset) const
        noexcept;
    [[nodiscard]] RetainedEntry& EntryAt(std::size_t offset) noexcept;

    std::array<RetainedEntry, kJudgementHistoryCapacity> entries_{};
    CausalState causal_base_{};
    std::optional<JudgementScopeCoordinate> causal_time_floor_;
    std::optional<JudgementScopeCoordinate> last_resolved_coordinate_;
    std::size_t read_slot_{};
    std::size_t size_{};
    std::uint64_t transport_epoch_{};
    std::uint64_t next_sequence_{};
    std::uint64_t base_next_sequence_{};
    gc::input::GameplayHeldMask current_held_{};
    bool initialized_{};
};

} // namespace gc::absolute_judgement
