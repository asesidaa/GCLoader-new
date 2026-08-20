#include "Patches/AbsoluteJudgement/AbsoluteJudgementRuntime.h"

#include "Audio/DirectSound/GameplayAudioCursorObservation.h"
#include "Audio/Wasapi/ExactWasapiClock.h"
#include "Patches/AbsoluteJudgement/JudgementScheduler.h"
#include "Patches/AbsoluteJudgement/NativeJudgementAbi.h"

#include <Windows.h>

#include <limits>
#include <memory>
#include <optional>
#include <utility>

namespace gc::absolute_judgement {
namespace {

using namespace native_abi;

[[nodiscard]] bool AddAddress(
    const std::uintptr_t base,
    const std::size_t offset,
    std::uintptr_t* const result) noexcept {
    if (result == nullptr ||
        offset > (std::numeric_limits<std::uintptr_t>::max)() - base) {
        return false;
    }
    *result = base + offset;
    return true;
}

[[nodiscard]] bool AddSignedAddress(
    const std::uintptr_t base,
    const std::ptrdiff_t offset,
    std::uintptr_t* const result) noexcept {
    if (result == nullptr) {
        return false;
    }
    if (offset < 0) {
        const auto magnitude = static_cast<std::uintptr_t>(-offset);
        if (magnitude > base) {
            return false;
        }
        *result = base - magnitude;
        return true;
    }
    return AddAddress(base, static_cast<std::size_t>(offset), result);
}

[[nodiscard]] bool ReadU32Safe(
    const std::uintptr_t address,
    std::uint32_t* const value) noexcept {
    if (address == 0 || value == nullptr) {
        return false;
    }
    __try {
        *value = *reinterpret_cast<volatile const std::uint32_t*>(address);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

[[nodiscard]] bool ReadFieldU32Safe(
    const std::uintptr_t base,
    const std::size_t offset,
    std::uint32_t* const value) noexcept {
    std::uintptr_t address{};
    return AddAddress(base, offset, &address) &&
        ReadU32Safe(address, value);
}

[[nodiscard]] bool ResolvePointerCollectionElementSafe(
    const std::uintptr_t owner,
    const std::size_t collection_offset,
    const std::uint32_t index,
    std::uintptr_t* const element) noexcept {
    if (owner == 0 || element == nullptr) {
        return false;
    }

    std::uintptr_t collection{};
    if (!AddAddress(owner, collection_offset, &collection)) {
        return false;
    }
    // The supported binary's native accessors are authoritative here:
    // sub_4128A0 computes ([this+0x10] - [this+0x0C]) / 4 and
    // sub_43D0C0 returns [this+0x0C] + index * 4. These 24-byte game
    // collections are not three-pointer std::vector objects at offset zero.
    std::uint32_t begin_raw{};
    std::uint32_t end_raw{};
    if (!ReadFieldU32Safe(
            collection, kPointerCollectionBeginOffset, &begin_raw) ||
        !ReadFieldU32Safe(
            collection, kPointerCollectionEndOffset, &end_raw)) {
        return false;
    }

    const auto begin = static_cast<std::uintptr_t>(begin_raw);
    const auto end = static_cast<std::uintptr_t>(end_raw);
    if (begin == 0 || end < begin ||
        (end - begin) % sizeof(std::uint32_t) != 0) {
        return false;
    }
    const auto count = (end - begin) / sizeof(std::uint32_t);
    if (static_cast<std::size_t>(index) >= count ||
        static_cast<std::size_t>(index) >
            (std::numeric_limits<std::size_t>::max)() /
                sizeof(std::uint32_t)) {
        return false;
    }
    const auto byte_offset = static_cast<std::size_t>(index) *
        sizeof(std::uint32_t);
    std::uintptr_t slot{};
    std::uint32_t resolved{};
    if (!AddAddress(begin, byte_offset, &slot) ||
        !ReadU32Safe(slot, &resolved) || resolved == 0) {
        return false;
    }
    *element = static_cast<std::uintptr_t>(resolved);
    return true;
}

[[nodiscard]] AbsoluteJudgementFatalReason HistoryErrorReason(
    const JudgementHistoryError error) noexcept {
    switch (error) {
    case JudgementHistoryError::TransportEpochMismatch:
        return AbsoluteJudgementFatalReason::TransportEpochLost;
    case JudgementHistoryError::SequenceDiscontinuity:
    case JudgementHistoryError::TransportStateMismatch:
        return AbsoluteJudgementFatalReason::TransportSequenceError;
    case JudgementHistoryError::BackwardTime:
        return AbsoluteJudgementFatalReason::BackwardTime;
    case JudgementHistoryError::HistoryLost:
        return AbsoluteJudgementFatalReason::RetainedHistoryLost;
    case JudgementHistoryError::CheckedArithmeticFailure:
        return AbsoluteJudgementFatalReason::CheckedArithmeticFailure;
    case JudgementHistoryError::NotInitialized:
    case JudgementHistoryError::InvalidControl:
        return AbsoluteJudgementFatalReason::NativeStateMismatch;
    }
    return AbsoluteJudgementFatalReason::NativeStateMismatch;
}

[[nodiscard]] AbsoluteJudgementFatalReason QueryInvariantReason(
    const JudgementQueryInvariant invariant) noexcept {
    switch (invariant) {
    case JudgementQueryInvariant::ThreadMismatch:
        return AbsoluteJudgementFatalReason::ScopeThreadMismatch;
    case JudgementQueryInvariant::ReceiverMismatch:
        return AbsoluteJudgementFatalReason::ScopeReceiverMismatch;
    case JudgementQueryInvariant::ScopeLifetimeViolation:
        return AbsoluteJudgementFatalReason::ScopeLifetimeViolation;
    case JudgementQueryInvariant::StageMismatch:
        return AbsoluteJudgementFatalReason::NativeIdentityChanged;
    case JudgementQueryInvariant::HistoryLost:
    case JudgementQueryInvariant::HistoryInvariantFailure:
        return AbsoluteJudgementFatalReason::RetainedHistoryLost;
    case JudgementQueryInvariant::CheckedArithmeticFailure:
    case JudgementQueryInvariant::DiagnosticOverflow:
        return AbsoluteJudgementFatalReason::CheckedArithmeticFailure;
    case JudgementQueryInvariant::None:
    case JudgementQueryInvariant::InvalidScope:
    case JudgementQueryInvariant::InvalidControl:
    case JudgementQueryInvariant::InvalidFrame:
    case JudgementQueryInvariant::InvalidDirectionArguments:
        return AbsoluteJudgementFatalReason::NativeStateMismatch;
    }
    return AbsoluteJudgementFatalReason::NativeStateMismatch;
}

class AbsoluteJudgementRuntime final {
public:
    void Initialize(const std::uintptr_t executable_base) noexcept {
        executable_base_ = executable_base;
    }

    void BeginNativeStage(const std::uintptr_t tune_manager) noexcept {
        scheduler_.BeginNativeStage(tune_manager);
        native_manager_ = tune_manager;
    }

    void EndNativeStage(const std::uintptr_t tune_manager) noexcept {
        scheduler_.EndNativeStage(tune_manager);
        native_manager_ = 0;
    }

    [[nodiscard]] bool NativeStageOpen() const noexcept {
        return scheduler_.NativeStageOpen();
    }

    [[nodiscard]] std::uint64_t stage_generation() const noexcept {
        return scheduler_.stage_generation();
    }

    [[noreturn]] void Fail(
        const AbsoluteJudgementFatalReason reason) const noexcept {
        scheduler_.FailActiveStage(reason);
    }

    [[noreturn]] void FailQueryInvariant(
        const JudgementQueryInvariant invariant,
        const std::optional<JudgementHistoryError> history_error) const
        noexcept {
        if (history_error) {
            Fail(HistoryErrorReason(*history_error));
        }
        Fail(QueryInvariantReason(invariant));
    }

    void DispatchOuterCall(safetyhook::Context& context) {
        const auto native = ResolveNativeIdentityOrFatal(context);

        std::optional<gc::audio::GameplayAudioCursorObservation> observation;
        bool group2_playing{};
        {
            gc::audio::ScopedGameplayAudioCursorQuery cursor_query;
            const auto get_sound_manager = reinterpret_cast<AccessorFn>(
                executable_base_ + kGetSoundManagerRva);
            const auto get_group_cursor = reinterpret_cast<GetGroupCursorFn>(
                executable_base_ + kGetGroupCursorRva);
            void* const sound_manager = get_sound_manager();
            if (sound_manager == nullptr) {
                Fail(AbsoluteJudgementFatalReason::NativeStateMismatch);
            }
            const int cursor_sign = get_group_cursor(
                sound_manager, kGameplaySoundGroup);
            observation = cursor_query.Consume();
            group2_playing = cursor_sign >= 0;
            if (!group2_playing) {
                observation.reset();
            }
        }

        auto endpoint = gc::audio::AcquireExactWasapiClock();
        LARGE_INTEGER now{};
        if (!QueryPerformanceCounter(&now)) {
            Fail(AbsoluteJudgementFatalReason::ClockDiscontinuous);
        }

        AbsoluteJudgementOuterProbe probe{
            .native = native,
            .group2_playing = group2_playing,
            .group2_observation = std::move(observation),
            .endpoint = std::move(endpoint),
            .now_qpc = now.QuadPart,
        };
        scheduler_.PrepareOuterCall(probe);
        while (const auto scope = scheduler_.NextScope()) {
            ExecuteScope(*scope, probe);
        }
        scheduler_.FinishOuterCall();

        std::uintptr_t tail{};
        if (!AddAddress(executable_base_, kLoopTailRva, &tail) ||
            tail > (std::numeric_limits<std::uint32_t>::max)()) {
            Fail(AbsoluteJudgementFatalReason::NativeStateMismatch);
        }
        context.eip = static_cast<std::uint32_t>(tail);
    }

private:
    [[nodiscard]] NativeJudgementIdentity ResolveNativeIdentityOrFatal(
        const safetyhook::Context& context) const noexcept {
        std::uintptr_t tune_slot{};
        std::uint32_t tune_raw{};
        if (native_manager_ == 0 ||
            !AddSignedAddress(
                static_cast<std::uintptr_t>(context.ebp),
                kTuneStackOffset,
                &tune_slot) ||
            !ReadU32Safe(tune_slot, &tune_raw) || tune_raw == 0) {
            Fail(AbsoluteJudgementFatalReason::NativeStateMismatch);
        }
        const auto tune = static_cast<std::uintptr_t>(tune_raw);

        const auto get_global = reinterpret_cast<AccessorFn>(
            executable_base_ + kGetGlobalRva);
        void* const global = get_global();
        std::uint32_t player{};
        if (global == nullptr ||
            !ReadFieldU32Safe(
                reinterpret_cast<std::uintptr_t>(global),
                kGlobalPlayerIndexOffset,
                &player)) {
            Fail(AbsoluteJudgementFatalReason::NativeStateMismatch);
        }

        std::uintptr_t judgement_state{};
        std::uintptr_t score_state{};
        if (!ResolvePointerCollectionElementSafe(
                tune,
                kTuneJudgementStatesOffset,
                player,
                &judgement_state) ||
            !ResolvePointerCollectionElementSafe(
                tune,
                kTuneScoreStatesOffset,
                player,
                &score_state)) {
            Fail(AbsoluteJudgementFatalReason::NativeStateMismatch);
        }

        const auto get_input_manager = reinterpret_cast<AccessorFn>(
            executable_base_ + kGetInputManagerRva);
        void* const input_manager = get_input_manager();
        std::uint32_t booster_raw{};
        if (input_manager == nullptr ||
            !ReadFieldU32Safe(
                reinterpret_cast<std::uintptr_t>(input_manager),
                kInputManagerBoosterOffset,
                &booster_raw) ||
            booster_raw == 0) {
            Fail(AbsoluteJudgementFatalReason::NativeStateMismatch);
        }
        const auto booster = static_cast<std::uintptr_t>(booster_raw);

        const auto get_config = reinterpret_cast<AccessorFn>(
            executable_base_ + kGetConfigRva);
        void* const config = get_config();
        std::uint32_t game_time_offset{};
        std::uint32_t hold_safe_frame{};
        std::uint32_t slide_hold_safe_frame{};
        if (config == nullptr ||
            !ReadFieldU32Safe(
                reinterpret_cast<std::uintptr_t>(config),
                kGameTimeOffsetOffset,
                &game_time_offset) ||
            !ReadFieldU32Safe(
                reinterpret_cast<std::uintptr_t>(config),
                kHoldSafeFrameOffset,
                &hold_safe_frame) ||
            !ReadFieldU32Safe(
                reinterpret_cast<std::uintptr_t>(config),
                kSlideHoldSafeFrameOffset,
                &slide_hold_safe_frame)) {
            Fail(AbsoluteJudgementFatalReason::NativeStateMismatch);
        }

        return {
            .stage_generation = scheduler_.stage_generation(),
            .tune_manager = native_manager_,
            .tune = tune,
            .judgement_state = judgement_state,
            .score_state = score_state,
            .booster = booster,
            .player = player,
            .game_time_offset_ms =
                static_cast<std::int32_t>(game_time_offset),
            .hold_safe_frame = static_cast<std::int32_t>(hold_safe_frame),
            .slide_hold_safe_frame =
                static_cast<std::int32_t>(slide_hold_safe_frame),
        };
    }

    [[nodiscard]] AbsoluteJudgementNativeScoreCounters
    ReadScoreCountersOrFatal(const std::uintptr_t score_state) const noexcept {
        AbsoluteJudgementNativeScoreCounters counters{};
        if (!ReadFieldU32Safe(
                score_state, kScoreMissOffset, &counters.miss) ||
            !ReadFieldU32Safe(
                score_state, kScoreGoodOffset, &counters.good) ||
            !ReadFieldU32Safe(
                score_state, kScoreCoolOffset, &counters.cool) ||
            !ReadFieldU32Safe(
                score_state, kScoreGreatOffset, &counters.great)) {
            Fail(AbsoluteJudgementFatalReason::NativeStateMismatch);
        }
        return counters;
    }

    void IncrementOrFatal(std::uint64_t& value) const noexcept {
        if (value == (std::numeric_limits<std::uint64_t>::max)()) {
            Fail(AbsoluteJudgementFatalReason::CheckedArithmeticFailure);
        }
        ++value;
    }

    void ExecuteScope(
        const ScheduledJudgementScope& scope,
        const AbsoluteJudgementOuterProbe& probe) {
        const auto& native = scheduler_.native_identity();
        gc::input::GameplayHeldMask held_before{};
        gc::input::GameplayHeldMask held_after{};
        gc::input::GameplayHeldMask rising{};
        gc::input::GameplayHeldMask falling{};
        std::optional<std::int64_t> event_qpc;
        if (scope.kind == JudgementScopeKind::Event) {
            if (scope.event == nullptr) {
                Fail(AbsoluteJudgementFatalReason::NativeStateMismatch);
            }
            held_before = scope.event->transport.held_before;
            held_after = scope.event->transport.held_after;
            rising = scope.event->transport.rising;
            falling = scope.event->transport.falling;
            event_qpc = scope.event->transport.qpc_ticks;
        } else {
            const auto held = scheduler_.history().OrdinaryHeldAt(
                scope.coordinate.judgement_seconds,
                scope.history_prefix_end_sequence);
            if (!held) {
                Fail(HistoryErrorReason(held.error()));
            }
            held_before = *held;
            held_after = *held;
        }

        AbsoluteJudgementQueryCounters scope_queries{};
        AbsoluteJudgementScoreDeltas score_deltas{};
        {
            ScopedJudgementQueryView query_view({
                .stage_generation = native.stage_generation,
                .expected_booster =
                    reinterpret_cast<const void*>(native.booster),
                .game_thread_id = GetCurrentThreadId(),
                .kind = scope.kind,
                .coordinate = scope.coordinate,
                .native_ms = scope.native_ms,
                .native_frame = scope.native_frame,
                .held_before = held_before,
                .held_after = held_after,
                .rising = rising,
                .falling = falling,
                .history_prefix_end_sequence =
                    scope.history_prefix_end_sequence,
                .history = &scheduler_.history(),
                .diagnostics = &scope_queries,
            });
            const auto install = query_view.install_result();
            if (!install.installed) {
                FailQueryInvariant(install.invariant, install.history_error);
            }

            const auto score_before = ReadScoreCountersOrFatal(
                native.score_state);
            static_cast<void>(
                scheduler_.CheckAndRecordNativeScoreCountersOrFatal(
                    score_before));

            const auto recognition = reinterpret_cast<RecognitionFn>(
                executable_base_ + kRecognitionRva);
            recognition(
                reinterpret_cast<void*>(native.judgement_state),
                scope.native_ms,
                scope.native_frame);
            IncrementOrFatal(
                JudgementDiagnostics().stage_counters().recognition_calls);

            const auto score = reinterpret_cast<ScoreFn>(
                executable_base_ + kScoreRva);
            score(
                reinterpret_cast<void*>(native.score_state),
                scope.native_ms);
            IncrementOrFatal(
                JudgementDiagnostics().stage_counters().score_calls);

            const auto score_after = ReadScoreCountersOrFatal(
                native.score_state);
            score_deltas =
                scheduler_.CheckAndRecordNativeScoreCountersOrFatal(
                    score_after);
        }

        scheduler_.CommitScope(scope);
        scheduler_.CheckNativeCallInvariantOrFatal();

        gc::timing::CheckedRational delivery_delay =
            gc::timing::CheckedRational::Whole(0);
        if (event_qpc && probe.now_qpc >= *event_qpc &&
            probe.endpoint) {
            const auto resolved_delay = gc::timing::CheckedRational::Create(
                probe.now_qpc - *event_qpc,
                probe.endpoint->qpc_frequency());
            if (!resolved_delay) {
                Fail(AbsoluteJudgementFatalReason::CheckedArithmeticFailure);
            }
            delivery_delay = *resolved_delay;
        }

        const auto& stage_counters =
            JudgementDiagnostics().stage_counters();
        JudgementDiagnostics().LogScopeVerbose({
            .native = {
                .stage_generation = native.stage_generation,
                .native_manager = native.tune_manager,
                .tune = native.tune,
                .judgement_state = native.judgement_state,
                .score_state = native.score_state,
                .booster = native.booster,
                .player = native.player,
            },
            .scope_id = stage_counters.recognition_calls,
            .kind = scope.kind == JudgementScopeKind::Event
                ? AbsoluteJudgementScopeKind::Event
                : AbsoluteJudgementScopeKind::Heartbeat,
            .equal_boundary_substitution =
                scope.kind == JudgementScopeKind::Event &&
                scope.commits_boundary,
            .journal_sequence = scope.coordinate.sequence,
            .mapped_j = scope.coordinate.judgement_seconds,
            .native_ms = scope.native_ms,
            .native_frame = scope.native_frame,
            .delivery_delay = delivery_delay,
            .held_before = held_before,
            .held_after = held_after,
            .rising = rising,
            .falling = falling,
            .queries = scope_queries,
            .recognition_completed = true,
            .score_completed = true,
            .score_deltas = score_deltas,
            .boundary_committed = scope.commits_boundary,
            .committed_boundary =
                scheduler_.committed_boundary_index().value_or(0),
            .remaining_backlog = stage_counters.pending_work,
        });
    }

    std::uintptr_t executable_base_{};
    std::uintptr_t native_manager_{};
    JudgementScheduler scheduler_;
};

AbsoluteJudgementRuntime& Runtime() noexcept {
    static AbsoluteJudgementRuntime runtime;
    return runtime;
}

} // namespace

void InitializeAbsoluteJudgementRuntime(
    const std::uintptr_t executable_base) noexcept {
    Runtime().Initialize(executable_base);
}

void BeginAbsoluteJudgementNativeStage(
    const std::uintptr_t tune_manager) noexcept {
    Runtime().BeginNativeStage(tune_manager);
}

void EndAbsoluteJudgementNativeStage(
    const std::uintptr_t tune_manager) noexcept {
    Runtime().EndNativeStage(tune_manager);
}

bool AbsoluteJudgementNativeStageOpen() noexcept {
    return Runtime().NativeStageOpen();
}

std::uint64_t AbsoluteJudgementStageGeneration() noexcept {
    return Runtime().stage_generation();
}

[[noreturn]] void FailAbsoluteJudgementQueryInvariant(
    const JudgementQueryInvariant invariant,
    const std::optional<JudgementHistoryError> history_error) noexcept {
    Runtime().FailQueryInvariant(invariant, history_error);
}

[[noreturn]] void FailAbsoluteJudgementActiveStage(
    const AbsoluteJudgementFatalReason reason) noexcept {
    Runtime().Fail(reason);
}

void DispatchAbsoluteJudgementOuterCall(safetyhook::Context& context) {
    Runtime().DispatchOuterCall(context);
}

} // namespace gc::absolute_judgement
