#pragma once

#include "Patches/AbsoluteJudgement/AbsoluteJudgementDiagnostics.h"
#include "Patches/AbsoluteJudgement/JudgementHistory.h"

#include <cstdint>
#include <optional>

namespace gc::absolute_judgement {

enum class JudgementQueryDisposition : std::uint8_t {
    Inactive,
    Answered,
    InvariantFailure,
};

enum class JudgementQueryInvariant : std::uint8_t {
    None,
    ThreadMismatch,
    ReceiverMismatch,
    StageMismatch,
    ScopeAlreadyActive,
    ScopeLifetimeViolation,
    InvalidScope,
    InvalidFrame,
    InvalidDirectionArguments,
    HistoryLost,
    CheckedArithmeticFailure,
    HistoryInvariantFailure,
};

template <typename Value>
struct JudgementQueryResult final {
    JudgementQueryDisposition disposition{JudgementQueryDisposition::Inactive};
    Value value{};
    JudgementQueryInvariant invariant{JudgementQueryInvariant::None};
    std::optional<JudgementHistoryError> history_error;
    std::uint64_t failure_operand0{};
    std::uint64_t failure_operand1{};
    std::uint8_t failure_operand_count{};
};

struct JudgementScopeInstallResult final {
    bool installed{};
    JudgementQueryInvariant invariant{JudgementQueryInvariant::None};
    std::optional<JudgementHistoryError> history_error;
    std::uint64_t failure_operand0{};
    std::uint64_t failure_operand1{};
    std::uint8_t failure_operand_count{};
};

struct JudgementScopeData final {
    std::uint64_t stage_generation{};
    const void* expected_booster{};
    std::uint32_t game_thread_id{};
    JudgementScopeKind kind{JudgementScopeKind::Heartbeat};
    JudgementScopeCoordinate coordinate{};
    std::int32_t native_ms{};
    std::int32_t native_frame{};
    gc::input::GameplayHeldMask held_before{};
    gc::input::GameplayHeldMask held_after{};
    gc::input::GameplayHeldMask rising{};
    gc::input::GameplayHeldMask falling{};
    // Exclusive transport-sequence end. This represents the empty prefix as
    // the stage cutoff (including zero) and prevents future relative queries
    // from observing records that were drained but not yet delivered.
    std::uint64_t history_prefix_end_sequence{};
    const JudgementHistory* history{};
    AbsoluteJudgementQueryCounters* diagnostics{};
    AbsoluteJudgementTimingGradeObservations* timing_grades{};
};

class ScopedJudgementQueryView final {
public:
    explicit ScopedJudgementQueryView(const JudgementScopeData& data) noexcept;
    ~ScopedJudgementQueryView() noexcept;

    [[nodiscard]] JudgementScopeInstallResult install_result() const noexcept;

    ScopedJudgementQueryView(const ScopedJudgementQueryView&) = delete;
    ScopedJudgementQueryView& operator=(const ScopedJudgementQueryView&) =
        delete;
    ScopedJudgementQueryView(ScopedJudgementQueryView&&) = delete;
    ScopedJudgementQueryView& operator=(ScopedJudgementQueryView&&) = delete;

private:
    friend const JudgementScopeData* ActiveJudgementScopeData() noexcept;

    JudgementScopeData data_;
    std::uint32_t installing_thread_id_{};
    JudgementQueryInvariant install_invariant_{
        JudgementQueryInvariant::None};
    std::optional<JudgementHistoryError> install_history_error_;
    std::uint64_t install_failure_operand0_{};
    std::uint64_t install_failure_operand1_{};
    std::uint8_t install_failure_operand_count_{};
    bool installed_{};
};

[[nodiscard]] const JudgementScopeData* ActiveJudgementScopeData() noexcept;
void RecordActiveTimingGradeObservation(
    std::uintptr_t note_address,
    std::int32_t recognition_ms,
    std::int32_t note_target_ms,
    std::int32_t native_grade) noexcept;

[[nodiscard]] JudgementQueryResult<std::uint8_t> QueryJudgementPressed(
    const void* receiver,
    std::uint64_t stage_generation,
    int control,
    int requested_frame) noexcept;
[[nodiscard]] JudgementQueryResult<std::uint8_t> QueryJudgementHeld(
    const void* receiver,
    std::uint64_t stage_generation,
    int control,
    int requested_frame) noexcept;
[[nodiscard]] JudgementQueryResult<std::uint8_t> QueryJudgementReleased(
    const void* receiver,
    std::uint64_t stage_generation,
    int control,
    int requested_frame) noexcept;
[[nodiscard]] JudgementQueryResult<int> QueryJudgementDirection(
    const void* receiver,
    std::uint64_t stage_generation,
    int booster,
    float* x,
    float* y,
    int requested_frame) noexcept;
[[nodiscard]] JudgementQueryResult<int> QueryJudgementHeldAge(
    const void* receiver,
    std::uint64_t stage_generation,
    unsigned int control) noexcept;

} // namespace gc::absolute_judgement
