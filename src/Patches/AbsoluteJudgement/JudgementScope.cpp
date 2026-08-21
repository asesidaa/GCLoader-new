#include "Patches/AbsoluteJudgement/JudgementScope.h"

#include "Patches/AbsoluteJudgement/AbsoluteJudgementRuntime.h"

#include <Windows.h>

#include <atomic>
#include <limits>

// Native algebra and ABI-return authority (read-only, completed audit):
// H:\gc\artifacts\GCLoader\.planning\debug\high-fps-timing-domains\evidence\E-045-native-component-freeinput-closure.md
// H:\gc\artifacts\GCLoader\.planning\debug\high-fps-timing-domains\evidence\E-046-native-normalization-progression-closure.md
// H:\gc\runs\20260815T182438Z-297470b1\artifacts\audit-input-helper-decompile-2026-08-17.txt
// The direction implementation below reproduces only helper 0x62E290's held
// mask/vector priority and its final-horizontal EAX value. Press/release use
// the symmetric recursive current-plus-inclusive-prior-four-frame algebra.

namespace gc::absolute_judgement {
namespace {

thread_local const ScopedJudgementQueryView* g_active_scope = nullptr;
std::atomic<std::uint32_t> g_active_scope_thread{};

struct ActiveScopeResolution final {
    const JudgementScopeData* data{};
    JudgementQueryDisposition disposition{JudgementQueryDisposition::Inactive};
    JudgementQueryInvariant invariant{JudgementQueryInvariant::None};
    std::optional<JudgementHistoryError> history_error;
    std::uint64_t failure_operand0{};
    std::uint64_t failure_operand1{};
    std::uint8_t failure_operand_count{};
};

JudgementQueryInvariant MapHistoryError(JudgementHistoryError error) noexcept;

ActiveScopeResolution ResolveActiveScope(
    const void* receiver,
    const std::uint64_t stage_generation) noexcept
{
    const JudgementScopeData* data = ActiveJudgementScopeData();
    if (data == nullptr)
    {
        return {};
    }

    if (data->game_thread_id != GetCurrentThreadId())
    {
        return {
            .data = data,
            .disposition = JudgementQueryDisposition::InvariantFailure,
            .invariant = JudgementQueryInvariant::ThreadMismatch,
            .failure_operand0 = data->game_thread_id,
            .failure_operand1 = GetCurrentThreadId(),
            .failure_operand_count = 2,
        };
    }
    if (data->expected_booster != receiver)
    {
        return {
            .data = data,
            .disposition = JudgementQueryDisposition::InvariantFailure,
            .invariant = JudgementQueryInvariant::ReceiverMismatch,
            .failure_operand0 = reinterpret_cast<std::uintptr_t>(
                data->expected_booster),
            .failure_operand1 = reinterpret_cast<std::uintptr_t>(receiver),
            .failure_operand_count = 2,
        };
    }
    if (data->stage_generation != stage_generation)
    {
        return {
            .data = data,
            .disposition = JudgementQueryDisposition::InvariantFailure,
            .invariant = JudgementQueryInvariant::StageMismatch,
            .failure_operand0 = data->stage_generation,
            .failure_operand1 = stage_generation,
            .failure_operand_count = 2,
        };
    }
    if (data->stage_generation == 0 || data->expected_booster == nullptr ||
        data->game_thread_id == 0 || data->history == nullptr ||
        data->diagnostics == nullptr ||
        (data->kind != JudgementScopeKind::Event &&
         data->kind != JudgementScopeKind::Heartbeat))
    {
        return {
            .data = data,
            .disposition = JudgementQueryDisposition::InvariantFailure,
            .invariant = JudgementQueryInvariant::InvalidScope,
        };
    }
    if (data->kind == JudgementScopeKind::Event)
    {
        const ResolvedGameplayTransition* event =
            data->history->FindResolvedTransition(data->coordinate.sequence);
        if (event == nullptr ||
            data->coordinate.sequence ==
                (std::numeric_limits<std::uint64_t>::max)() ||
            data->history_prefix_end_sequence !=
                data->coordinate.sequence + 1 ||
            event->judgement_seconds.Compare(
                data->coordinate.judgement_seconds) != 0 ||
            event->transport.held_before != data->held_before ||
            event->transport.held_after != data->held_after ||
            event->transport.rising != data->rising ||
            event->transport.falling != data->falling)
        {
            return {
                .data = data,
                .disposition = JudgementQueryDisposition::InvariantFailure,
                .invariant = JudgementQueryInvariant::InvalidScope,
            };
        }
    }
    else if (data->rising != 0 || data->falling != 0 ||
             data->held_before != data->held_after)
    {
        return {
            .data = data,
            .disposition = JudgementQueryDisposition::InvariantFailure,
            .invariant = JudgementQueryInvariant::InvalidScope,
            };
        }
    else
    {
        const auto held = data->history->OrdinaryHeldAt(
            data->coordinate.judgement_seconds,
            data->history_prefix_end_sequence);
        if (!held)
        {
            return {
                .data = data,
                .disposition = JudgementQueryDisposition::InvariantFailure,
                .invariant = MapHistoryError(held.error()),
                .history_error = held.error(),
            };
        }
        if (*held != data->held_after)
        {
            return {
                .data = data,
                .disposition = JudgementQueryDisposition::InvariantFailure,
                .invariant = JudgementQueryInvariant::InvalidScope,
            };
        }
    }
    return {
        .data = data,
        .disposition = JudgementQueryDisposition::Answered,
        .invariant = JudgementQueryInvariant::None,
    };
}

template <typename Value>
JudgementQueryResult<Value> FromResolution(
    const ActiveScopeResolution& resolution) noexcept
{
    return JudgementQueryResult<Value>{
        .disposition = resolution.disposition,
        .value = {},
        .invariant = resolution.invariant,
        .history_error = resolution.history_error,
        .failure_operand0 = resolution.failure_operand0,
        .failure_operand1 = resolution.failure_operand1,
        .failure_operand_count = resolution.failure_operand_count,
    };
}

template <typename Value>
JudgementQueryResult<Value> InvariantResult(
    const JudgementQueryInvariant invariant,
    const std::optional<JudgementHistoryError> history_error = std::nullopt,
    const std::uint64_t failure_operand0 = 0,
    const std::uint64_t failure_operand1 = 0,
    const std::uint8_t failure_operand_count = 0)
    noexcept
{
    return JudgementQueryResult<Value>{
        .disposition = JudgementQueryDisposition::InvariantFailure,
        .value = {},
        .invariant = invariant,
        .history_error = history_error,
        .failure_operand0 = failure_operand0,
        .failure_operand1 = failure_operand1,
        .failure_operand_count = failure_operand_count,
    };
}

template <typename Value>
JudgementQueryResult<Value> AnsweredResult(const Value value) noexcept
{
    return JudgementQueryResult<Value>{
        .disposition = JudgementQueryDisposition::Answered,
        .value = value,
        .invariant = JudgementQueryInvariant::None,
        .history_error = std::nullopt,
        .failure_operand0 = 0,
        .failure_operand1 = 0,
        .failure_operand_count = 0,
    };
}

JudgementQueryInvariant MapHistoryError(
    const JudgementHistoryError error) noexcept
{
    switch (error)
    {
    case JudgementHistoryError::CapacityExhausted:
    case JudgementHistoryError::PrefixBeyondNext:
    case JudgementHistoryError::PromisedEntryMissing:
        return JudgementQueryInvariant::HistoryLost;
    case JudgementHistoryError::CheckedArithmeticFailure:
        return JudgementQueryInvariant::CheckedArithmeticFailure;
    default:
        return JudgementQueryInvariant::HistoryInvariantFailure;
    }
}

template <typename Value>
JudgementQueryResult<Value> HistoryFailure(
    const JudgementHistoryError error) noexcept
{
    return InvariantResult<Value>(MapHistoryError(error), error);
}

std::expected<gc::timing::CheckedRational, JudgementHistoryError>
TranslateRequestedFrame(const JudgementScopeData& data,
                        const int requested_frame) noexcept
{
    const std::int64_t delta =
        static_cast<std::int64_t>(requested_frame) -
        static_cast<std::int64_t>(data.native_frame);
    const auto delta_seconds =
        gc::timing::CheckedRational::Whole(delta).Multiply(1, 60);
    if (!delta_seconds)
    {
        return std::unexpected(
            JudgementHistoryError::CheckedArithmeticFailure);
    }
    const auto query_time =
        data.coordinate.judgement_seconds.Add(*delta_seconds);
    if (!query_time)
    {
        return std::unexpected(
            JudgementHistoryError::CheckedArithmeticFailure);
    }
    return *query_time;
}

void IncrementSaturating(std::uint64_t& value) noexcept
{
    if (value != (std::numeric_limits<std::uint64_t>::max)())
    {
        ++value;
    }
}

void RecordBooleanQuery(std::uint64_t& calls,
                        std::uint64_t& true_results,
                        const bool value) noexcept
{
    IncrementSaturating(calls);
    if (value)
    {
        IncrementSaturating(true_results);
    }
}

void RecordDirectionQuery(AbsoluteJudgementQueryCounters& counters,
                          const bool nonzero) noexcept
{
    RecordBooleanQuery(
        counters.direction_calls, counters.direction_nonzero, nonzero);
}

void RecordHeldAgeQuery(AbsoluteJudgementQueryCounters& counters,
                        const int age) noexcept
{
    const bool age_one = age == 1;
    const bool age_two_plus = age >= 2;
    IncrementSaturating(counters.held_age_calls);
    if (age_one)
    {
        IncrementSaturating(counters.held_age_one);
    }
    if (age_two_plus)
    {
        IncrementSaturating(counters.held_age_two_plus);
    }
}

std::expected<bool, JudgementHistoryError> HeldForDirection(
    const JudgementScopeData& data,
    const std::uint32_t control,
    const gc::timing::CheckedRational& query_time) noexcept
{
    return data.history->HeldAt(
        control, query_time, data.history_prefix_end_sequence);
}

} // namespace

ScopedJudgementQueryView::ScopedJudgementQueryView(
    const JudgementScopeData& data) noexcept
    : data_(data), installing_thread_id_(GetCurrentThreadId())
{
    if (data_.game_thread_id != installing_thread_id_ ||
        data_.game_thread_id == 0)
    {
        install_invariant_ = JudgementQueryInvariant::ThreadMismatch;
        install_failure_operand0_ = data_.game_thread_id;
        install_failure_operand1_ = installing_thread_id_;
        install_failure_operand_count_ = 2;
        return;
    }
    if (g_active_scope != nullptr)
    {
        install_invariant_ = JudgementQueryInvariant::ScopeAlreadyActive;
        install_failure_operand0_ =
            g_active_scope_thread.load(std::memory_order_seq_cst);
        install_failure_operand1_ = installing_thread_id_;
        install_failure_operand_count_ = 2;
        return;
    }

    std::uint32_t inactive = 0;
    if (!g_active_scope_thread.compare_exchange_strong(
            inactive,
            installing_thread_id_,
            std::memory_order_seq_cst,
            std::memory_order_seq_cst))
    {
        install_invariant_ = JudgementQueryInvariant::ScopeAlreadyActive;
        install_failure_operand0_ = inactive;
        install_failure_operand1_ = installing_thread_id_;
        install_failure_operand_count_ = 2;
        return;
    }
    g_active_scope = this;

    const auto resolution = ResolveActiveScope(
        data_.expected_booster, data_.stage_generation);
    if (resolution.disposition != JudgementQueryDisposition::Answered)
    {
        install_invariant_ = resolution.invariant;
        install_history_error_ = resolution.history_error;
        install_failure_operand0_ = resolution.failure_operand0;
        install_failure_operand1_ = resolution.failure_operand1;
        install_failure_operand_count_ = resolution.failure_operand_count;
        g_active_scope = nullptr;
        std::uint32_t active = installing_thread_id_;
        if (!g_active_scope_thread.compare_exchange_strong(
                active,
                0,
                std::memory_order_seq_cst,
                std::memory_order_seq_cst))
        {
            FailAbsoluteJudgementActiveStage(
                AbsoluteJudgementFatalPredicate::ScopeTlsOwnerMismatch,
                AbsoluteJudgementFatalReason::ScopeThreadMismatch,
                {installing_thread_id_, active});
        }
        return;
    }
    installed_ = true;
}

ScopedJudgementQueryView::~ScopedJudgementQueryView() noexcept
{
    if (!installed_)
    {
        return;
    }
    if (g_active_scope != this || GetCurrentThreadId() != installing_thread_id_)
    {
        FailAbsoluteJudgementActiveStage(
            AbsoluteJudgementFatalPredicate::ScopeLifetimeMismatch,
            AbsoluteJudgementFatalReason::ScopeLifetimeViolation,
            {installing_thread_id_, GetCurrentThreadId()});
    }
    g_active_scope = nullptr;

    std::uint32_t active = installing_thread_id_;
    if (!g_active_scope_thread.compare_exchange_strong(
            active,
            0,
            std::memory_order_seq_cst,
            std::memory_order_seq_cst))
    {
        FailAbsoluteJudgementActiveStage(
            AbsoluteJudgementFatalPredicate::ScopeTlsOwnerMismatch,
            AbsoluteJudgementFatalReason::ScopeThreadMismatch,
            {installing_thread_id_, active});
    }
}

JudgementScopeInstallResult ScopedJudgementQueryView::install_result()
    const noexcept
{
    return {
        .installed = installed_,
        .invariant = install_invariant_,
        .history_error = install_history_error_,
        .failure_operand0 = install_failure_operand0_,
        .failure_operand1 = install_failure_operand1_,
        .failure_operand_count = install_failure_operand_count_,
    };
}

const JudgementScopeData* ActiveJudgementScopeData() noexcept
{
    return g_active_scope == nullptr ? nullptr : &g_active_scope->data_;
}

void RecordActiveTimingGradeObservation(
    const std::uintptr_t note_address,
    const std::int32_t recognition_ms,
    const std::int32_t note_target_ms,
    const std::int32_t native_grade) noexcept
{
    const auto* active = ActiveJudgementScopeData();
    if (active == nullptr || active->timing_grades == nullptr)
    {
        return;
    }

    auto& observations = *active->timing_grades;
    IncrementSaturating(observations.calls);
    if (observations.size >= observations.records.size())
    {
        IncrementSaturating(observations.drops);
        return;
    }

    observations.records[observations.size] = {
        .note_address = note_address,
        .recognition_ms = recognition_ms,
        .note_target_ms = note_target_ms,
        .signed_error_ms = static_cast<std::int64_t>(recognition_ms) -
            static_cast<std::int64_t>(note_target_ms),
        .native_grade = native_grade,
    };
    ++observations.size;
}

JudgementQueryResult<std::uint8_t> QueryJudgementPressed(
    const void* receiver,
    const std::uint64_t stage_generation,
    const int control,
    const int requested_frame) noexcept
{
    const ActiveScopeResolution active =
        ResolveActiveScope(receiver, stage_generation);
    if (active.disposition != JudgementQueryDisposition::Answered)
    {
        return FromResolution<std::uint8_t>(active);
    }
    if (control < 0 ||
        control >= static_cast<int>(kJudgementLogicalControlCount))
    {
        return AnsweredResult<std::uint8_t>(0);
    }
    if (requested_frame != active.data->native_frame)
    {
        return InvariantResult<std::uint8_t>(
            JudgementQueryInvariant::InvalidFrame,
            std::nullopt,
            static_cast<std::uint64_t>(
                static_cast<std::uint32_t>(active.data->native_frame)),
            static_cast<std::uint64_t>(
                static_cast<std::uint32_t>(requested_frame)),
            2);
    }

    const auto pressed = active.data->history->Pressed(
        static_cast<std::uint32_t>(control),
        active.data->kind,
        active.data->coordinate,
        active.data->rising);
    if (!pressed)
    {
        return HistoryFailure<std::uint8_t>(pressed.error());
    }
    RecordBooleanQuery(
        active.data->diagnostics->pressed_calls,
        active.data->diagnostics->pressed_true,
        *pressed);
    return AnsweredResult<std::uint8_t>(*pressed ? 1 : 0);
}

JudgementQueryResult<std::uint8_t> QueryJudgementHeld(
    const void* receiver,
    const std::uint64_t stage_generation,
    const int control,
    const int requested_frame) noexcept
{
    const ActiveScopeResolution active =
        ResolveActiveScope(receiver, stage_generation);
    if (active.disposition != JudgementQueryDisposition::Answered)
    {
        return FromResolution<std::uint8_t>(active);
    }
    if (control < 0 ||
        control >= static_cast<int>(kJudgementLogicalControlCount))
    {
        return AnsweredResult<std::uint8_t>(0);
    }

    const auto query_time =
        TranslateRequestedFrame(*active.data, requested_frame);
    if (!query_time)
    {
        return HistoryFailure<std::uint8_t>(query_time.error());
    }
    const auto held = active.data->history->HeldAt(
        static_cast<std::uint32_t>(control),
        *query_time,
        active.data->history_prefix_end_sequence);
    if (!held)
    {
        return HistoryFailure<std::uint8_t>(held.error());
    }
    RecordBooleanQuery(active.data->diagnostics->held_calls,
                       active.data->diagnostics->held_true,
                       *held);
    return AnsweredResult<std::uint8_t>(*held ? 1 : 0);
}

JudgementQueryResult<std::uint8_t> QueryJudgementReleased(
    const void* receiver,
    const std::uint64_t stage_generation,
    const int control,
    const int requested_frame) noexcept
{
    const ActiveScopeResolution active =
        ResolveActiveScope(receiver, stage_generation);
    if (active.disposition != JudgementQueryDisposition::Answered)
    {
        return FromResolution<std::uint8_t>(active);
    }
    if (control < 0 ||
        control >= static_cast<int>(kJudgementLogicalControlCount))
    {
        return AnsweredResult<std::uint8_t>(0);
    }

    std::expected<bool, JudgementHistoryError> released = false;
    if (requested_frame == active.data->native_frame)
    {
        released = active.data->history->Released(
            static_cast<std::uint32_t>(control),
            active.data->kind,
            active.data->coordinate,
            active.data->falling);
    }
    else
    {
        const auto window_end = TranslateRequestedFrame(
            *active.data, requested_frame);
        if (!window_end)
        {
            return HistoryFailure<std::uint8_t>(window_end.error());
        }
        released = active.data->history->ReleasedInWindow(
            static_cast<std::uint32_t>(control),
            *window_end,
            active.data->history_prefix_end_sequence);
    }
    if (!released)
    {
        return HistoryFailure<std::uint8_t>(released.error());
    }
    RecordBooleanQuery(
        active.data->diagnostics->released_calls,
        active.data->diagnostics->released_true,
        *released);
    return AnsweredResult<std::uint8_t>(*released ? 1 : 0);
}

JudgementQueryResult<int> QueryJudgementDirection(
    const void* receiver,
    const std::uint64_t stage_generation,
    const int booster,
    float* const x,
    float* const y,
    const int requested_frame) noexcept
{
    const ActiveScopeResolution active =
        ResolveActiveScope(receiver, stage_generation);
    if (active.disposition != JudgementQueryDisposition::Answered)
    {
        return FromResolution<int>(active);
    }
    if (x == nullptr || y == nullptr)
    {
        return InvariantResult<int>(
            JudgementQueryInvariant::InvalidDirectionArguments,
            std::nullopt,
            reinterpret_cast<std::uintptr_t>(x),
            reinterpret_cast<std::uintptr_t>(y),
            2);
    }
    *x = 0.0F;
    *y = 0.0F;

    if (booster < 0 || booster > 2)
    {
        RecordDirectionQuery(*active.data->diagnostics, false);
        return AnsweredResult<int>(static_cast<int>(
            reinterpret_cast<std::uintptr_t>(x)));
    }

    const auto query_time =
        TranslateRequestedFrame(*active.data, requested_frame);
    if (!query_time)
    {
        return HistoryFailure<int>(query_time.error());
    }

    float result_x = 0.0F;
    float result_y = 0.0F;
    bool horizontal_result = false;
    if (booster < 2)
    {
        const std::uint32_t base = booster == 0 ? 0u : 5u;
        const auto up = HeldForDirection(*active.data, base, *query_time);
        if (!up)
        {
            return HistoryFailure<int>(up.error());
        }
        if (*up)
        {
            result_y = -1.0F;
        }
        else
        {
            const auto down =
                HeldForDirection(*active.data, base + 1, *query_time);
            if (!down)
            {
                return HistoryFailure<int>(down.error());
            }
            if (*down)
            {
                result_y = 1.0F;
            }
        }

        const auto left =
            HeldForDirection(*active.data, base + 2, *query_time);
        if (!left)
        {
            return HistoryFailure<int>(left.error());
        }
        if (*left)
        {
            result_x = -1.0F;
            horizontal_result = true;
        }
        else
        {
            const auto right =
                HeldForDirection(*active.data, base + 3, *query_time);
            if (!right)
            {
                return HistoryFailure<int>(right.error());
            }
            if (*right)
            {
                result_x = 1.0F;
            }
            horizontal_result = *right;
        }
    }
    else
    {
        const auto up = HeldForDirection(*active.data, 10, *query_time);
        const auto down = HeldForDirection(*active.data, 11, *query_time);
        const auto left = HeldForDirection(*active.data, 12, *query_time);
        const auto right = HeldForDirection(*active.data, 13, *query_time);
        if (!up)
        {
            return HistoryFailure<int>(up.error());
        }
        if (!down)
        {
            return HistoryFailure<int>(down.error());
        }
        if (!left)
        {
            return HistoryFailure<int>(left.error());
        }
        if (!right)
        {
            return HistoryFailure<int>(right.error());
        }

        result_y += *up ? -1.0F : 0.0F;
        result_y += *down ? 1.0F : 0.0F;
        result_x += *left ? -1.0F : 0.0F;
        result_x += *right ? 1.0F : 0.0F;
        horizontal_result = *right;
    }

    const bool nonzero = result_x != 0.0F || result_y != 0.0F;
    RecordDirectionQuery(*active.data->diagnostics, nonzero);
    *x = result_x;
    *y = result_y;
    return AnsweredResult<int>(horizontal_result ? 1 : 0);
}

JudgementQueryResult<int> QueryJudgementHeldAge(
    const void* receiver,
    const std::uint64_t stage_generation,
    const unsigned int control) noexcept
{
    const ActiveScopeResolution active =
        ResolveActiveScope(receiver, stage_generation);
    if (active.disposition != JudgementQueryDisposition::Answered)
    {
        return FromResolution<int>(active);
    }
    if (control >= kJudgementLogicalControlCount)
    {
        return AnsweredResult<int>(0);
    }

    const auto age = active.data->history->HeldAge(
        control,
        active.data->kind,
        active.data->coordinate,
        active.data->history_prefix_end_sequence,
        active.data->held_before,
        active.data->held_after);
    if (!age)
    {
        return HistoryFailure<int>(age.error());
    }
    RecordHeldAgeQuery(*active.data->diagnostics, *age);
    return AnsweredResult<int>(*age);
}

} // namespace gc::absolute_judgement
