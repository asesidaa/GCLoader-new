#include "Patches/AbsoluteJudgement/JudgementScope.h"

#include <Windows.h>

#include <atomic>
#include <cstdlib>
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
};

JudgementQueryInvariant MapHistoryError(JudgementHistoryError error) noexcept;

ActiveScopeResolution ResolveActiveScope(
    const void* receiver,
    const std::uint64_t stage_generation) noexcept
{
    const JudgementScopeData* data = ActiveJudgementScopeData();
    if (data == nullptr)
    {
        if (g_active_scope_thread.load(std::memory_order_seq_cst) != 0)
        {
            return {
                .data = nullptr,
                .disposition =
                    JudgementQueryDisposition::InvariantFailure,
                .invariant = JudgementQueryInvariant::ThreadMismatch,
            };
        }
        return {};
    }

    if (data->game_thread_id != GetCurrentThreadId())
    {
        return {
            .data = data,
            .disposition = JudgementQueryDisposition::InvariantFailure,
            .invariant = JudgementQueryInvariant::ThreadMismatch,
        };
    }
    if (data->expected_booster != receiver)
    {
        return {
            .data = data,
            .disposition = JudgementQueryDisposition::InvariantFailure,
            .invariant = JudgementQueryInvariant::ReceiverMismatch,
        };
    }
    if (data->stage_generation != stage_generation)
    {
        return {
            .data = data,
            .disposition = JudgementQueryDisposition::InvariantFailure,
            .invariant = JudgementQueryInvariant::StageMismatch,
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
    };
}

template <typename Value>
JudgementQueryResult<Value> InvariantResult(
    const JudgementQueryInvariant invariant,
    const std::optional<JudgementHistoryError> history_error = std::nullopt)
    noexcept
{
    return JudgementQueryResult<Value>{
        .disposition = JudgementQueryDisposition::InvariantFailure,
        .value = {},
        .invariant = invariant,
        .history_error = history_error,
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
    };
}

JudgementQueryInvariant MapHistoryError(
    const JudgementHistoryError error) noexcept
{
    switch (error)
    {
    case JudgementHistoryError::HistoryLost:
        return JudgementQueryInvariant::HistoryLost;
    case JudgementHistoryError::CheckedArithmeticFailure:
        return JudgementQueryInvariant::CheckedArithmeticFailure;
    case JudgementHistoryError::InvalidControl:
        return JudgementQueryInvariant::InvalidControl;
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

bool CanIncrement(const std::uint64_t value) noexcept
{
    return value != (std::numeric_limits<std::uint64_t>::max)();
}

bool RecordBooleanQuery(std::uint64_t& calls,
                        std::uint64_t& true_results,
                        const bool value) noexcept
{
    if (!CanIncrement(calls) || (value && !CanIncrement(true_results)))
    {
        return false;
    }
    ++calls;
    if (value)
    {
        ++true_results;
    }
    return true;
}

bool RecordDirectionQuery(AbsoluteJudgementQueryCounters& counters,
                          const bool nonzero) noexcept
{
    return RecordBooleanQuery(
        counters.direction_calls, counters.direction_nonzero, nonzero);
}

bool RecordHeldAgeQuery(AbsoluteJudgementQueryCounters& counters,
                        const int age) noexcept
{
    const bool age_one = age == 1;
    const bool age_two_plus = age >= 2;
    if (!CanIncrement(counters.held_age_calls) ||
        (age_one && !CanIncrement(counters.held_age_one)) ||
        (age_two_plus && !CanIncrement(counters.held_age_two_plus)))
    {
        return false;
    }
    ++counters.held_age_calls;
    if (age_one)
    {
        ++counters.held_age_one;
    }
    if (age_two_plus)
    {
        ++counters.held_age_two_plus;
    }
    return true;
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
        return;
    }
    if (g_active_scope != nullptr)
    {
        install_invariant_ = JudgementQueryInvariant::ScopeLifetimeViolation;
        return;
    }

    std::uint32_t inactive = 0;
    if (!g_active_scope_thread.compare_exchange_strong(
            inactive,
            installing_thread_id_,
            std::memory_order_seq_cst,
            std::memory_order_seq_cst))
    {
        install_invariant_ = JudgementQueryInvariant::ScopeLifetimeViolation;
        return;
    }
    g_active_scope = this;

    const auto resolution = ResolveActiveScope(
        data_.expected_booster, data_.stage_generation);
    if (resolution.disposition != JudgementQueryDisposition::Answered)
    {
        install_invariant_ = resolution.invariant;
        install_history_error_ = resolution.history_error;
        g_active_scope = nullptr;
        std::uint32_t active = installing_thread_id_;
        if (!g_active_scope_thread.compare_exchange_strong(
                active,
                0,
                std::memory_order_seq_cst,
                std::memory_order_seq_cst))
        {
            std::abort();
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
        std::abort();
    }
    g_active_scope = nullptr;

    std::uint32_t active = installing_thread_id_;
    if (!g_active_scope_thread.compare_exchange_strong(
            active,
            0,
            std::memory_order_seq_cst,
            std::memory_order_seq_cst))
    {
        std::abort();
    }
}

JudgementScopeInstallResult ScopedJudgementQueryView::install_result()
    const noexcept
{
    return {
        .installed = installed_,
        .invariant = install_invariant_,
        .history_error = install_history_error_,
    };
}

const JudgementScopeData* ActiveJudgementScopeData() noexcept
{
    return g_active_scope == nullptr ? nullptr : &g_active_scope->data_;
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
        return InvariantResult<std::uint8_t>(
            JudgementQueryInvariant::InvalidControl);
    }
    if (requested_frame != active.data->native_frame)
    {
        return InvariantResult<std::uint8_t>(
            JudgementQueryInvariant::InvalidFrame);
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
    if (!RecordBooleanQuery(
            active.data->diagnostics->pressed_calls,
            active.data->diagnostics->pressed_true,
            *pressed))
    {
        return InvariantResult<std::uint8_t>(
            JudgementQueryInvariant::DiagnosticOverflow);
    }
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
        return InvariantResult<std::uint8_t>(
            JudgementQueryInvariant::InvalidControl);
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
    if (!RecordBooleanQuery(active.data->diagnostics->held_calls,
                            active.data->diagnostics->held_true,
                            *held))
    {
        return InvariantResult<std::uint8_t>(
            JudgementQueryInvariant::DiagnosticOverflow);
    }
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
        return InvariantResult<std::uint8_t>(
            JudgementQueryInvariant::InvalidControl);
    }
    if (requested_frame != active.data->native_frame)
    {
        return InvariantResult<std::uint8_t>(
            JudgementQueryInvariant::InvalidFrame);
    }

    const auto released = active.data->history->Released(
        static_cast<std::uint32_t>(control),
        active.data->kind,
        active.data->coordinate,
        active.data->falling);
    if (!released)
    {
        return HistoryFailure<std::uint8_t>(released.error());
    }
    if (!RecordBooleanQuery(
            active.data->diagnostics->released_calls,
            active.data->diagnostics->released_true,
            *released))
    {
        return InvariantResult<std::uint8_t>(
            JudgementQueryInvariant::DiagnosticOverflow);
    }
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
    if (x == nullptr || y == nullptr || booster < 0 || booster > 2)
    {
        return InvariantResult<int>(
            JudgementQueryInvariant::InvalidDirectionArguments);
    }
    *x = 0.0F;
    *y = 0.0F;

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
    if (!RecordDirectionQuery(*active.data->diagnostics, nonzero))
    {
        return InvariantResult<int>(
            JudgementQueryInvariant::DiagnosticOverflow);
    }
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
        return InvariantResult<int>(JudgementQueryInvariant::InvalidControl);
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
    if (!RecordHeldAgeQuery(*active.data->diagnostics, *age))
    {
        return InvariantResult<int>(
            JudgementQueryInvariant::DiagnosticOverflow);
    }
    return AnsweredResult<int>(*age);
}

} // namespace gc::absolute_judgement
