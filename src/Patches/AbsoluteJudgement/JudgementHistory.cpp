#include "Patches/AbsoluteJudgement/JudgementHistory.h"

#include <algorithm>
#include <limits>

// Native algebra authority (read-only, completed audit):
// H:\gc\artifacts\GCLoader\.planning\debug\high-fps-timing-domains\evidence\E-045-native-component-freeinput-closure.md
// H:\gc\artifacts\GCLoader\.planning\debug\high-fps-timing-domains\evidence\E-046-native-normalization-progression-closure.md
// H:\gc\runs\20260815T182438Z-297470b1\artifacts\audit-input-helper-decompile-2026-08-17.txt
// In particular, CBooster pressed/released are pure recursive queries; paired
// IDs use the current constituent plus the other constituent in frames 1..4.

namespace gc::absolute_judgement {
namespace {

constexpr gc::input::GameplayHeldMask kOrdinaryHeldMask =
    (gc::input::GameplayHeldMask{1} << 10) - 1;

std::expected<gc::timing::CheckedRational, JudgementHistoryError>
PairedLookback() noexcept
{
    auto result = gc::timing::CheckedRational::Whole(4).Multiply(1, 60);
    if (!result)
    {
        return std::unexpected(
            JudgementHistoryError::CheckedArithmeticFailure);
    }
    return *result;
}

} // namespace

std::expected<void, JudgementHistoryError> JudgementHistory::Reset(
    const std::uint64_t transport_epoch,
    const std::uint64_t cutoff_sequence,
    const gc::input::GameplayHeldMask baseline)
    noexcept
{
    if (!IsValidMask(baseline))
    {
        return std::unexpected(RecordFailure(
            JudgementHistoryError::BaselineMaskInvalid,
            {baseline, kOrdinaryHeldMask}));
    }
    last_failure_operands_ = {};
    last_failure_operand_count_ = 0;
    read_slot_ = 0;
    size_ = 0;
    transport_epoch_ = transport_epoch;
    next_sequence_ = cutoff_sequence;
    base_next_sequence_ = cutoff_sequence;
    current_held_ = baseline;
    causal_base_ = {};
    causal_base_.ordinary_held = current_held_;
    for (std::uint32_t control = 0;
         control < kJudgementLogicalControlCount;
         ++control)
    {
        if (LogicalHeld(current_held_, control))
        {
            causal_base_.rises[control].stale = true;
        }
    }
    causal_time_floor_.reset();
    last_coordinate_.reset();
    initialized_ = true;
    return {};
}

std::expected<void, JudgementHistoryError> JudgementHistory::Append(
    const ResolvedGameplayTransition& transition) noexcept
{
    return AppendEntry(
        transition, true, StateOnlyReason::AcceptedLate);
}

std::expected<void, JudgementHistoryError>
JudgementHistory::AppendStateOnly(
    const ResolvedGameplayTransition& transition,
    const StateOnlyReason reason) noexcept
{
    return AppendEntry(transition, false, reason);
}

std::expected<void, JudgementHistoryError> JudgementHistory::AppendEntry(
    const ResolvedGameplayTransition& transition,
    const bool event_eligible,
    const StateOnlyReason state_only_reason) noexcept
{
    const auto transport_validation = ValidateTransport(transition.transport);
    if (!transport_validation)
    {
        return std::unexpected(transport_validation.error());
    }
    if (transition.transport.sequence ==
        (std::numeric_limits<std::uint64_t>::max)())
    {
        return std::unexpected(
            RecordFailure(
                JudgementHistoryError::SequenceExhausted,
                {transition.transport.sequence}));
    }

    const JudgementScopeCoordinate coordinate{
        .judgement_seconds = transition.judgement_seconds,
        .sequence = transition.transport.sequence,
    };
    if (last_coordinate_)
    {
        const int time_order = transition.judgement_seconds.Compare(
            last_coordinate_->judgement_seconds);
        if (time_order < 0 ||
            (time_order == 0 &&
             transition.transport.sequence <= last_coordinate_->sequence))
        {
            return std::unexpected(RecordFailure(
                JudgementHistoryError::BackwardTime,
                {last_coordinate_->sequence,
                 transition.transport.sequence}));
        }
    }
    if (size_ == kJudgementHistoryCapacity)
    {
        return std::unexpected(RecordFailure(
            JudgementHistoryError::CapacityExhausted,
            {kJudgementHistoryCapacity, size_}));
    }

    EntryAt(size_) = RetainedEntry{
        .transition = transition,
        .state_only_reason = state_only_reason,
        .event_eligible = event_eligible,
    };
    ++size_;
    ++next_sequence_;
    current_held_ = transition.transport.held_after;
    last_coordinate_ = coordinate;
    return {};
}

std::expected<std::uint64_t, JudgementHistoryError>
JudgementHistory::CountResolvedAtOrBefore(
    const std::uint64_t first_sequence,
    const gc::timing::CheckedRational& ready) const noexcept
{
    if (!initialized_)
    {
        return std::unexpected(RecordFailure(
            JudgementHistoryError::NotInitialized));
    }
    if (first_sequence > next_sequence_)
    {
        return std::unexpected(RecordFailure(
            JudgementHistoryError::PrefixBeyondNext,
            {first_sequence, next_sequence_}));
    }

    // A consumed state-only prefix may already have advanced the retained
    // base. For example, base=66 with delivery request=64 starts at 66; it is
    // not missing promised event history.
    const auto retained_start = (std::max)(
        first_sequence, base_next_sequence_);
    std::uint64_t count{};
    for (std::size_t offset = 0; offset < size_; ++offset)
    {
        const RetainedEntry& entry = EntryAt(offset);
        if (entry.transition.transport.sequence < retained_start)
        {
            continue;
        }
        if (entry.transition.judgement_seconds.Compare(ready) > 0)
        {
            break;
        }
        if (!entry.event_eligible)
        {
            continue;
        }
        if (count == (std::numeric_limits<std::uint64_t>::max)())
        {
            return std::unexpected(
                JudgementHistoryError::CheckedArithmeticFailure);
        }
        ++count;
    }
    return count;
}

std::expected<void, JudgementHistoryError>
JudgementHistory::ConvertResolvedToStateOnly(
    const std::uint64_t sequence,
    const StateOnlyReason reason) noexcept
{
    if (!initialized_)
    {
        return std::unexpected(RecordFailure(
            JudgementHistoryError::NotInitialized));
    }
    if (reason != StateOnlyReason::Overload ||
        sequence < base_next_sequence_ || sequence >= next_sequence_)
    {
        return std::unexpected(RecordFailure(
            JudgementHistoryError::PromisedEntryMissing,
            {sequence, base_next_sequence_, next_sequence_}));
    }

    for (std::size_t offset = 0; offset < size_; ++offset)
    {
        RetainedEntry& entry = EntryAt(offset);
        const auto entry_sequence = entry.transition.transport.sequence;
        if (entry_sequence > sequence)
        {
            return std::unexpected(
                RecordFailure(
                    JudgementHistoryError::PromisedEntryMissing,
                    {sequence, base_next_sequence_, next_sequence_}));
        }
        if (entry_sequence != sequence)
        {
            continue;
        }
        if (!entry.event_eligible)
        {
            return std::unexpected(
                RecordFailure(
                    JudgementHistoryError::TransportStateMismatch,
                    {1, 0}));
        }
        entry.event_eligible = false;
        entry.state_only_reason = reason;
        return {};
    }
    return std::unexpected(RecordFailure(
        JudgementHistoryError::PromisedEntryMissing,
        {sequence, base_next_sequence_, next_sequence_}));
}

std::expected<void, JudgementHistoryError> JudgementHistory::PruneBefore(
    const gc::timing::CheckedRational& earliest_query_time,
    const std::uint64_t earliest_history_prefix_end_sequence) noexcept
{
    if (!initialized_)
    {
        return std::unexpected(RecordFailure(
            JudgementHistoryError::NotInitialized));
    }
    if (earliest_history_prefix_end_sequence > next_sequence_)
    {
        return std::unexpected(RecordFailure(
            JudgementHistoryError::PrefixBeyondNext,
            {earliest_history_prefix_end_sequence, next_sequence_}));
    }
    if (earliest_history_prefix_end_sequence < base_next_sequence_)
    {
        return std::unexpected(RecordFailure(
            JudgementHistoryError::PromisedEntryMissing,
            {earliest_history_prefix_end_sequence,
             base_next_sequence_,
             next_sequence_}));
    }

    const auto lookback = PairedLookback();
    if (!lookback)
    {
        return std::unexpected(lookback.error());
    }
    const auto lookback_start =
        earliest_query_time.Subtract(*lookback);
    if (!lookback_start)
    {
        return std::unexpected(
            JudgementHistoryError::CheckedArithmeticFailure);
    }

    while (size_ != 0)
    {
        const RetainedEntry& entry = EntryAt(0);
        if (entry.transition.transport.sequence >=
            earliest_history_prefix_end_sequence)
        {
            break;
        }
        if (entry.transition.judgement_seconds.Compare(*lookback_start) >= 0)
        {
            break;
        }

        ApplyToState(causal_base_, entry);
        if (!causal_time_floor_ ||
            entry.transition.judgement_seconds.Compare(
                causal_time_floor_->judgement_seconds) > 0)
        {
            causal_time_floor_ = JudgementScopeCoordinate{
                .judgement_seconds = entry.transition.judgement_seconds,
                .sequence = entry.transition.transport.sequence,
            };
        }
        base_next_sequence_ = entry.transition.transport.sequence + 1;
        read_slot_ = (read_slot_ + 1) % kJudgementHistoryCapacity;
        --size_;
    }
    return {};
}

std::expected<bool, JudgementHistoryError> JudgementHistory::HeldAt(
    const std::uint32_t control,
    const gc::timing::CheckedRational& query_time,
    const std::uint64_t history_prefix_end_sequence) const noexcept
{
    if (control >= kJudgementLogicalControlCount)
    {
        return std::unexpected(RecordFailure(
            JudgementHistoryError::InvalidControl,
            {control, kJudgementLogicalControlCount}));
    }
    const auto state = StateAt(query_time, history_prefix_end_sequence);
    if (!state)
    {
        return std::unexpected(state.error());
    }
    return LogicalHeld(state->ordinary_held, control);
}

std::expected<gc::input::GameplayHeldMask, JudgementHistoryError>
JudgementHistory::OrdinaryHeldAt(
    const gc::timing::CheckedRational& query_time,
    const std::uint64_t history_prefix_end_sequence) const noexcept
{
    const auto state = StateAt(query_time, history_prefix_end_sequence);
    if (!state)
    {
        return std::unexpected(state.error());
    }
    return state->ordinary_held;
}

std::expected<bool, JudgementHistoryError> JudgementHistory::Pressed(
    const std::uint32_t control,
    const JudgementScopeKind kind,
    const JudgementScopeCoordinate& coordinate,
    const gc::input::GameplayHeldMask current_rising) const noexcept
{
    return Edge(control, kind, coordinate, current_rising, true);
}

std::expected<bool, JudgementHistoryError> JudgementHistory::Released(
    const std::uint32_t control,
    const JudgementScopeKind kind,
    const JudgementScopeCoordinate& coordinate,
    const gc::input::GameplayHeldMask current_falling) const noexcept
{
    return Edge(control, kind, coordinate, current_falling, false);
}

std::expected<bool, JudgementHistoryError>
JudgementHistory::ReleasedInWindow(
    const std::uint32_t control,
    const gc::timing::CheckedRational& window_end,
    const std::uint64_t history_prefix_end_sequence) const noexcept
{
    if (!initialized_)
    {
        return std::unexpected(RecordFailure(
            JudgementHistoryError::NotInitialized));
    }
    if (control >= kJudgementLogicalControlCount)
    {
        return std::unexpected(RecordFailure(
            JudgementHistoryError::InvalidControl,
            {control, kJudgementLogicalControlCount}));
    }
    if (history_prefix_end_sequence < base_next_sequence_ ||
        history_prefix_end_sequence > next_sequence_)
    {
        return false;
    }

    const auto quantum =
        gc::timing::CheckedRational::Whole(1).Multiply(1, 60);
    const auto window_start = quantum
        ? window_end.Subtract(*quantum)
        : std::expected<gc::timing::CheckedRational,
                        gc::timing::RationalError>(
              std::unexpected(gc::timing::RationalError::Overflow));
    if (!quantum || !window_start)
    {
        return std::unexpected(
            JudgementHistoryError::CheckedArithmeticFailure);
    }
    if (causal_time_floor_ &&
        window_start->Compare(
            causal_time_floor_->judgement_seconds) < 0)
    {
        return false;
    }

    for (std::size_t offset = 0; offset < size_; ++offset)
    {
        const RetainedEntry& entry = EntryAt(offset);
        const auto sequence = entry.transition.transport.sequence;
        if (sequence >= history_prefix_end_sequence)
        {
            break;
        }
        if (entry.transition.judgement_seconds.Compare(window_end) > 0)
        {
            break;
        }
        if (!entry.event_eligible ||
            entry.transition.judgement_seconds.Compare(*window_start) <= 0)
        {
            continue;
        }

        const auto falling = entry.transition.transport.falling;
        const auto ordinary_edge = [falling](
                                       const std::uint32_t ordinary) {
            return (falling & static_cast<gc::input::GameplayHeldMask>(
                                  1u << ordinary)) != 0;
        };
        if (control < 10)
        {
            if (ordinary_edge(control))
            {
                return true;
            }
            continue;
        }

        const std::uint32_t pair =
            control >= 15 ? control - 15 : control - 10;
        const std::uint32_t first = pair;
        const std::uint32_t second = pair + 5;
        const bool first_current = ordinary_edge(first);
        const bool second_current = ordinary_edge(second);
        if (control < 15)
        {
            if (first_current || second_current)
            {
                return true;
            }
            continue;
        }
        if (first_current && second_current)
        {
            return true;
        }
        if (!first_current && !second_current)
        {
            continue;
        }

        const JudgementScopeCoordinate coordinate{
            .judgement_seconds = entry.transition.judgement_seconds,
            .sequence = sequence,
        };
        const auto paired = HasPriorEdge(
            first_current ? second : first, coordinate, false);
        if (!paired)
        {
            return std::unexpected(paired.error());
        }
        if (*paired)
        {
            return true;
        }
    }
    return false;
}

std::expected<std::int32_t, JudgementHistoryError>
JudgementHistory::HeldAge(
    const std::uint32_t control,
    const JudgementScopeKind kind,
    const JudgementScopeCoordinate& coordinate,
    const std::uint64_t history_prefix_end_sequence,
    const gc::input::GameplayHeldMask current_held_before,
    const gc::input::GameplayHeldMask current_held_after) const noexcept
{
    if (control >= kJudgementLogicalControlCount)
    {
        return std::unexpected(RecordFailure(
            JudgementHistoryError::InvalidControl,
            {control, kJudgementLogicalControlCount}));
    }
    const auto state = StateAt(
        coordinate.judgement_seconds, history_prefix_end_sequence);
    if (!state)
    {
        return std::unexpected(state.error());
    }
    if (!LogicalHeld(state->ordinary_held, control))
    {
        return 0;
    }

    const bool current_logical_rise =
        kind == JudgementScopeKind::Event &&
        !LogicalHeld(current_held_before, control) &&
        LogicalHeld(current_held_after, control);
    if (current_logical_rise)
    {
        return 1;
    }

    const LogicalRiseState& rise = state->rises[control];
    if (rise.stale || !rise.accepted_rise)
    {
        return 5;
    }
    if (rise.accepted_rise->sequence >= history_prefix_end_sequence ||
        rise.accepted_rise->judgement_seconds.Compare(
            coordinate.judgement_seconds) > 0)
    {
        return std::unexpected(RecordFailure(
            JudgementHistoryError::PromisedEntryMissing,
            {coordinate.sequence, base_next_sequence_, next_sequence_}));
    }

    const auto elapsed = coordinate.judgement_seconds.Subtract(
        rise.accepted_rise->judgement_seconds);
    if (!elapsed)
    {
        return std::unexpected(
            JudgementHistoryError::CheckedArithmeticFailure);
    }
    const auto elapsed_quanta = elapsed->Multiply(60, 1);
    if (!elapsed_quanta)
    {
        return std::unexpected(
            JudgementHistoryError::CheckedArithmeticFailure);
    }
    const auto whole_quanta = elapsed_quanta->Floor();
    if (!whole_quanta || *whole_quanta < 0 ||
        *whole_quanta >=
            static_cast<std::int64_t>(
                (std::numeric_limits<std::int32_t>::max)()))
    {
        return std::unexpected(
            JudgementHistoryError::CheckedArithmeticFailure);
    }

    const std::int64_t exact_age = 1 + *whole_quanta;
    const std::int64_t later_age = exact_age < 2 ? 2 : exact_age;
    return static_cast<std::int32_t>(later_age);
}

const ResolvedGameplayTransition* JudgementHistory::FindResolvedTransition(
    const std::uint64_t sequence) const noexcept
{
    for (std::size_t offset = 0; offset < size_; ++offset)
    {
        const RetainedEntry& entry = EntryAt(offset);
        if (entry.transition.transport.sequence > sequence)
        {
            return nullptr;
        }
        if (entry.transition.transport.sequence == sequence &&
            entry.event_eligible)
        {
            return &entry.transition;
        }
    }
    return nullptr;
}

const ResolvedGameplayTransition* JudgementHistory::FirstResolvedAtOrAfter(
    const std::uint64_t sequence) const noexcept
{
    for (std::size_t offset = 0; offset < size_; ++offset)
    {
        const RetainedEntry& entry = EntryAt(offset);
        if (entry.event_eligible &&
            entry.transition.transport.sequence >= sequence)
        {
            return &entry.transition;
        }
    }
    return nullptr;
}

std::uint64_t JudgementHistory::transport_epoch() const noexcept
{
    return transport_epoch_;
}

std::uint64_t JudgementHistory::next_sequence() const noexcept
{
    return next_sequence_;
}

gc::input::GameplayHeldMask JudgementHistory::current_held() const noexcept
{
    return current_held_;
}

const std::array<std::uint64_t, 8>&
JudgementHistory::last_failure_operands() const noexcept
{
    return last_failure_operands_;
}

std::uint8_t JudgementHistory::last_failure_operand_count() const noexcept
{
    return last_failure_operand_count_;
}

std::size_t JudgementHistory::retained_entry_count() const noexcept
{
    return size_;
}

std::expected<void, JudgementHistoryError>
JudgementHistory::ValidateTransport(
    const gc::input::GameplayTransitionRecord& transition) const noexcept
{
    if (!initialized_)
    {
        return std::unexpected(RecordFailure(
            JudgementHistoryError::NotInitialized));
    }
    if (transition.transport_epoch != transport_epoch_)
    {
        return std::unexpected(
            RecordFailure(
                JudgementHistoryError::TransportEpochMismatch,
                {transport_epoch_, transition.transport_epoch}));
    }
    if (transition.sequence != next_sequence_)
    {
        return std::unexpected(
            RecordFailure(
                JudgementHistoryError::SequenceDiscontinuity,
                {next_sequence_, transition.sequence}));
    }
    if (!IsValidMask(transition.held_before) ||
        !IsValidMask(transition.held_after) ||
        !IsValidMask(transition.rising) ||
        !IsValidMask(transition.falling) ||
        transition.held_before != current_held_)
    {
        return std::unexpected(
            RecordFailure(
                JudgementHistoryError::TransportStateMismatch,
                {current_held_, transition.held_before}));
    }

    const auto expected_rising = static_cast<gc::input::GameplayHeldMask>(
        transition.held_after & ~transition.held_before & kOrdinaryHeldMask);
    const auto expected_falling = static_cast<gc::input::GameplayHeldMask>(
        transition.held_before & ~transition.held_after & kOrdinaryHeldMask);
    if (transition.rising != expected_rising ||
        transition.falling != expected_falling)
    {
        return std::unexpected(
            RecordFailure(
                JudgementHistoryError::TransportStateMismatch,
                {static_cast<std::uint64_t>(expected_rising) |
                     (static_cast<std::uint64_t>(expected_falling) << 16),
                 static_cast<std::uint64_t>(transition.rising) |
                     (static_cast<std::uint64_t>(transition.falling) << 16)}));
    }
    return {};
}

std::expected<JudgementHistory::CausalState, JudgementHistoryError>
JudgementHistory::StateAt(
    const gc::timing::CheckedRational& query_time,
    const std::uint64_t history_prefix_end_sequence) const noexcept
{
    if (!initialized_)
    {
        return std::unexpected(RecordFailure(
            JudgementHistoryError::NotInitialized));
    }
    if (history_prefix_end_sequence > next_sequence_)
    {
        return std::unexpected(RecordFailure(
            JudgementHistoryError::PrefixBeyondNext,
            {history_prefix_end_sequence, next_sequence_}));
    }
    if (history_prefix_end_sequence < base_next_sequence_)
    {
        return std::unexpected(RecordFailure(
            JudgementHistoryError::PromisedEntryMissing,
            {history_prefix_end_sequence, base_next_sequence_, next_sequence_}));
    }
    if (causal_time_floor_ &&
        query_time.Compare(causal_time_floor_->judgement_seconds) < 0)
    {
        return std::unexpected(
            RecordFailure(
                JudgementHistoryError::PromisedEntryMissing,
                {history_prefix_end_sequence,
                 base_next_sequence_,
                 next_sequence_}));
    }

    CausalState state = causal_base_;
    for (std::size_t offset = 0; offset < size_; ++offset)
    {
        const RetainedEntry& entry = EntryAt(offset);
        if (entry.transition.transport.sequence >=
            history_prefix_end_sequence)
        {
            break;
        }
        // Sequence eligibility alone is not causal. A release at J=2.000
        // cannot affect a held-state query at J=1.993 even when both entries
        // are already present under the immutable prefix.
        if (entry.transition.judgement_seconds.Compare(query_time) > 0)
        {
            break;
        }
        ApplyToState(state, entry);
    }
    return state;
}

std::expected<bool, JudgementHistoryError> JudgementHistory::Edge(
    const std::uint32_t control,
    const JudgementScopeKind kind,
    const JudgementScopeCoordinate& coordinate,
    const gc::input::GameplayHeldMask current_edges,
    const bool rising) const noexcept
{
    if (control >= kJudgementLogicalControlCount)
    {
        return std::unexpected(RecordFailure(
            JudgementHistoryError::InvalidControl,
            {control, kJudgementLogicalControlCount}));
    }
    if (kind == JudgementScopeKind::Heartbeat)
    {
        return false;
    }

    const auto ordinary_edge = [current_edges](const std::uint32_t ordinary) {
        return (current_edges &
                static_cast<gc::input::GameplayHeldMask>(1u << ordinary)) != 0;
    };
    if (control < 10)
    {
        return ordinary_edge(control);
    }

    const std::uint32_t pair = control >= 15 ? control - 15 : control - 10;
    const std::uint32_t first = pair;
    const std::uint32_t second = pair + 5;
    const bool first_current = ordinary_edge(first);
    const bool second_current = ordinary_edge(second);
    if (control < 15)
    {
        return first_current || second_current;
    }
    if (first_current && second_current)
    {
        return true;
    }
    if (!first_current && !second_current)
    {
        return false;
    }

    return HasPriorEdge(
        first_current ? second : first, coordinate, rising);
}

std::expected<bool, JudgementHistoryError> JudgementHistory::HasPriorEdge(
    const std::uint32_t ordinary_control,
    const JudgementScopeCoordinate& coordinate,
    const bool rising) const noexcept
{
    const auto lookback = PairedLookback();
    if (!lookback)
    {
        return std::unexpected(lookback.error());
    }
    const auto first_eligible =
        coordinate.judgement_seconds.Subtract(*lookback);
    if (!first_eligible)
    {
        return std::unexpected(
            JudgementHistoryError::CheckedArithmeticFailure);
    }
    const gc::input::GameplayHeldMask bit =
        static_cast<gc::input::GameplayHeldMask>(1u << ordinary_control);

    for (std::size_t offset = size_; offset != 0; --offset)
    {
        const RetainedEntry& entry = EntryAt(offset - 1);
        if (!entry.event_eligible ||
            entry.transition.transport.sequence >= coordinate.sequence)
        {
            continue;
        }
        if (entry.transition.judgement_seconds.Compare(
                coordinate.judgement_seconds) > 0)
        {
            continue;
        }
        if (entry.transition.judgement_seconds.Compare(*first_eligible) < 0)
        {
            break;
        }

        const gc::input::GameplayHeldMask edges =
            rising ? entry.transition.transport.rising
                   : entry.transition.transport.falling;
        if ((edges & bit) != 0)
        {
            return true;
        }
    }
    return false;
}

bool JudgementHistory::LogicalHeld(
    const gc::input::GameplayHeldMask ordinary_held,
    const std::uint32_t control) noexcept
{
    if (control < 10)
    {
        return (ordinary_held &
                static_cast<gc::input::GameplayHeldMask>(1u << control)) != 0;
    }

    const std::uint32_t pair = control >= 15 ? control - 15 : control - 10;
    const bool first =
        (ordinary_held &
         static_cast<gc::input::GameplayHeldMask>(1u << pair)) != 0;
    const bool second =
        (ordinary_held &
         static_cast<gc::input::GameplayHeldMask>(1u << (pair + 5))) != 0;
    return control < 15 ? first || second : first && second;
}

void JudgementHistory::ApplyToState(CausalState& state,
                                    const RetainedEntry& entry) noexcept
{
    const gc::input::GameplayHeldMask before = state.ordinary_held;
    const gc::input::GameplayHeldMask after =
        entry.transition.transport.held_after;
    for (std::uint32_t control = 0;
         control < kJudgementLogicalControlCount;
         ++control)
    {
        const bool was_held = LogicalHeld(before, control);
        const bool is_held = LogicalHeld(after, control);
        LogicalRiseState& rise = state.rises[control];
        if (!was_held && is_held)
        {
            if (entry.event_eligible)
            {
                rise.stale = false;
                rise.accepted_rise = JudgementScopeCoordinate{
                    .judgement_seconds = entry.transition.judgement_seconds,
                    .sequence = entry.transition.transport.sequence,
                };
            }
            else
            {
                rise.stale = true;
                rise.accepted_rise.reset();
            }
        }
        else if (was_held && !is_held)
        {
            rise.stale = false;
            rise.accepted_rise.reset();
        }
    }
    state.ordinary_held = after;
}

bool JudgementHistory::IsValidMask(
    const gc::input::GameplayHeldMask mask) noexcept
{
    return (mask & ~kOrdinaryHeldMask) == 0;
}

JudgementHistoryError JudgementHistory::RecordFailure(
    const JudgementHistoryError error,
    const std::initializer_list<std::uint64_t> operands) const noexcept
{
    last_failure_operands_ = {};
    last_failure_operand_count_ = 0;
    for (const auto operand : operands)
    {
        if (last_failure_operand_count_ == last_failure_operands_.size())
        {
            break;
        }
        last_failure_operands_[last_failure_operand_count_++] = operand;
    }
    return error;
}

const JudgementHistory::RetainedEntry& JudgementHistory::EntryAt(
    const std::size_t offset) const noexcept
{
    return entries_[(read_slot_ + offset) % kJudgementHistoryCapacity];
}

JudgementHistory::RetainedEntry& JudgementHistory::EntryAt(
    const std::size_t offset) noexcept
{
    return entries_[(read_slot_ + offset) % kJudgementHistoryCapacity];
}

} // namespace gc::absolute_judgement
