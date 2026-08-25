#include "Patches/AbsoluteJudgement/AbsoluteJudgementRuntime.h"

#include "Audio/DirectSound/GameplayAudioCursorObservation.h"
#include "Audio/ExactOutputClock.h"
#include "Patches/AbsoluteJudgement/JudgementScheduler.h"
#include "Patches/AbsoluteJudgement/NativeJudgementAbi.h"

#include <Windows.h>
#include <timeapi.h>

#include <plog/Log.h>

#include <format>
#include <limits>
#include <memory>
#include <optional>
#include <utility>

namespace gc::absolute_judgement {
namespace {

using namespace native_abi;

struct NativeJudgementConfiguration final {
    std::int32_t game_time_offset_ms{};
    std::int32_t hold_safe_frame{};
    std::int32_t slide_hold_safe_frame{};
};

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

[[nodiscard]] bool ReadU8Safe(
    const std::uintptr_t address,
    std::uint8_t* const value) noexcept {
    if (address == 0 || value == nullptr) {
        return false;
    }
    __try {
        *value = *reinterpret_cast<volatile const std::uint8_t*>(address);
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

[[nodiscard]] bool ReadFieldU8Safe(
    const std::uintptr_t base,
    const std::size_t offset,
    std::uint8_t* const value) noexcept {
    std::uintptr_t address{};
    return AddAddress(base, offset, &address) && ReadU8Safe(address, value);
}

void ExpirePreviousTransientSoundPublications(
    const std::uintptr_t judgement_state) noexcept {
    auto* const bytes =
        reinterpret_cast<volatile std::uint8_t*>(judgement_state);
    bytes[kJudgementArrangePublicationOffset] = 0;
    bytes[kJudgementLeftFreeTapPublicationOffset] = 0;
    bytes[kJudgementRightFreeTapPublicationOffset] = 0;
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

struct ExactFailure final {
    AbsoluteJudgementFatalPredicate predicate{};
    AbsoluteJudgementFatalReason category{};
};

[[nodiscard]] ExactFailure QueryInvariantFailure(
    const JudgementQueryInvariant invariant) noexcept {
    switch (invariant) {
    case JudgementQueryInvariant::ThreadMismatch:
        return {AbsoluteJudgementFatalPredicate::ScopeTlsOwnerMismatch,
                AbsoluteJudgementFatalReason::ScopeThreadMismatch};
    case JudgementQueryInvariant::ReceiverMismatch:
        return {AbsoluteJudgementFatalPredicate::ScopeReceiverMismatch,
                AbsoluteJudgementFatalReason::ScopeReceiverMismatch};
    case JudgementQueryInvariant::ScopeLifetimeViolation:
        return {AbsoluteJudgementFatalPredicate::ScopeLifetimeMismatch,
                AbsoluteJudgementFatalReason::ScopeLifetimeViolation};
    case JudgementQueryInvariant::StageMismatch:
        return {AbsoluteJudgementFatalPredicate::ScopeGenerationMismatch,
                AbsoluteJudgementFatalReason::NativeIdentityChanged};
    case JudgementQueryInvariant::ScopeAlreadyActive:
        return {AbsoluteJudgementFatalPredicate::ScopeAlreadyActive,
                AbsoluteJudgementFatalReason::ScopeLifetimeViolation};
    case JudgementQueryInvariant::HistoryLost:
        return {AbsoluteJudgementFatalPredicate::HistoryPromisedEntryMissing,
                AbsoluteJudgementFatalReason::RetainedHistoryLost};
    case JudgementQueryInvariant::HistoryInvariantFailure:
        return {AbsoluteJudgementFatalPredicate::TransportMaskMismatch,
                AbsoluteJudgementFatalReason::RetainedHistoryLost};
    case JudgementQueryInvariant::CheckedArithmeticFailure:
        return {
            AbsoluteJudgementFatalPredicate::RationalOperationUnrepresentable,
            AbsoluteJudgementFatalReason::CheckedArithmeticFailure};
    case JudgementQueryInvariant::InvalidFrame:
        return {AbsoluteJudgementFatalPredicate::PressedFrameMismatch,
                AbsoluteJudgementFatalReason::NativeStateMismatch};
    case JudgementQueryInvariant::InvalidDirectionArguments:
        return {AbsoluteJudgementFatalPredicate::DirectionOutputNull,
                AbsoluteJudgementFatalReason::NativeStateMismatch};
    case JudgementQueryInvariant::InvalidScope:
        return {AbsoluteJudgementFatalPredicate::ScopeLifetimeMismatch,
                AbsoluteJudgementFatalReason::ScopeLifetimeViolation};
    case JudgementQueryInvariant::None:
        break;
    }
    return {AbsoluteJudgementFatalPredicate::FatalRecordInvalid,
            AbsoluteJudgementFatalReason::NativeStateMismatch};
}

class AbsoluteJudgementRuntime final {
public:
    void Initialize(
        const std::uintptr_t executable_base,
        const gc::audio::ExactOutputClockDomain expected_domain) noexcept {
        executable_base_ = executable_base;
        expected_domain_ = expected_domain;
    }

    // This small timestamp is forwarded and consumed by value.
    // ReSharper disable once CppPassValueParameterByConstReference
    void BeginSemanticStage(
        const std::uintptr_t tune_manager,
        const gc::timing::AbsoluteHostTime stage_entry_time) noexcept {
        const auto configuration = ReadConfigurationOrFatal();
        scheduler_.BeginSemanticStage(
            tune_manager,
            stage_entry_time,
            configuration.game_time_offset_ms,
            configuration.hold_safe_frame,
            configuration.slide_hold_safe_frame);
        native_manager_ = tune_manager;
    }

    void EndSemanticStage(const std::uintptr_t tune_manager) noexcept {
        scheduler_.EndSemanticStage(tune_manager);
        native_manager_ = 0;
    }

    void ObserveGameplayInitialization(
        const std::uintptr_t tune_manager) noexcept {
        const auto stage_generation = scheduler_.stage_generation();
        if (!scheduler_.TerminateSemanticStageForGameplayInitialization(
                tune_manager)) {
            return;
        }
        native_manager_ = 0;
        PLOG_INFO << std::format(
            "AbsoluteJudgement: semantic-stage-termination"
            " source=gameplay_initialization stage_generation={}"
            " native_manager={}",
            stage_generation,
            tune_manager);
    }

    void EndSemanticStageForTestMode() noexcept {
        if (!scheduler_.SemanticStageOpen()) {
            return;
        }

        const auto stage_generation = scheduler_.stage_generation();
        EndSemanticStage(native_manager_);
        PLOG_INFO << std::format(
            "AbsoluteJudgement: semantic-stage-termination"
            " source=test_mode_entry stage_generation={}",
            stage_generation);
    }

    [[nodiscard]] bool SemanticStageOpen() const noexcept {
        return scheduler_.SemanticStageOpen();
    }

    [[nodiscard]] std::uint64_t stage_generation() const noexcept {
        return scheduler_.stage_generation();
    }

    [[noreturn]] void Fail(
        const AbsoluteJudgementFatalPredicate predicate,
        const AbsoluteJudgementFatalReason category,
        const std::initializer_list<std::uint64_t> operands = {}) const
        noexcept {
        scheduler_.FailActiveStage(predicate, category, operands);
    }

    [[noreturn]] void FailQueryInvariant(
        const JudgementQueryInvariant invariant,
        const std::optional<JudgementHistoryError> history_error,
        const std::uint64_t failure_operand0 = 0,
        const std::uint64_t failure_operand1 = 0,
        const std::uint8_t failure_operand_count = 0) const noexcept {
        if (history_error) {
            scheduler_.FailHistoryInvariant(*history_error);
        }
        const auto failure = QueryInvariantFailure(invariant);
        if (failure_operand_count >= 2) {
            Fail(
                failure.predicate,
                failure.category,
                {failure_operand0, failure_operand1});
        }
        if (failure_operand_count == 1) {
            Fail(
                failure.predicate,
                failure.category,
                {failure_operand0});
        }
        Fail(failure.predicate, failure.category);
    }

    [[nodiscard]] gc::timing::AbsoluteHostTime
    CaptureAbsoluteHostTimeOrFatal() const noexcept {
        LARGE_INTEGER qpc{};
        const auto qpc_result = QueryPerformanceCounter(&qpc);
        if (!qpc_result || qpc.QuadPart <= 0) {
            Fail(
                AbsoluteJudgementFatalPredicate::
                    QueryPerformanceCounterFailed,
                AbsoluteJudgementFatalReason::ClockDiscontinuous,
                {qpc_result ? 1u : 0u,
                 static_cast<std::uint64_t>(qpc.QuadPart)});
        }
        return {
            .qpc_ticks = qpc.QuadPart,
            .multimedia_time_ms = timeGetTime(),
        };
    }

    void DispatchOuterCall(safetyhook::Context& context) {
        auto endpoint = gc::audio::AcquireExactOutputClock();
        if (!endpoint) {
            Fail(
                AbsoluteJudgementFatalPredicate::ExactOutputProviderMissing,
                AbsoluteJudgementFatalReason::EndpointCapabilityUnavailable,
                {static_cast<std::uint64_t>(expected_domain_)});
        }
        const auto endpoint_info = endpoint->info();
        if (endpoint_info.domain != expected_domain_) {
            Fail(
                AbsoluteJudgementFatalPredicate::
                    ExactOutputProviderDomainMismatch,
                AbsoluteJudgementFatalReason::EndpointCapabilityUnavailable,
                {static_cast<std::uint64_t>(expected_domain_),
                 static_cast<std::uint64_t>(endpoint_info.domain)});
        }

        const auto native = ResolveNativeIdentityOrFatal(context);
        // The native tail consumes these one-shot publications once after this
        // owned call. Expire that completed call's values before recognition
        // can publish the next call's values; neighboring judgement state is
        // deliberately untouched.
        ExpirePreviousTransientSoundPublications(native.judgement_state);

        std::optional<gc::audio::GameplayAudioCursorObservation> observation;
        bool group2_cursor_selected{};
        {
            gc::audio::ScopedGameplayAudioCursorQuery cursor_query;
            const auto get_sound_manager = reinterpret_cast<AccessorFn>(
                executable_base_ + kGetSoundManagerRva);
            const auto get_group_cursor = reinterpret_cast<GetGroupCursorFn>(
                executable_base_ + kGetGroupCursorRva);
            void* const sound_manager = get_sound_manager();
            if (sound_manager == nullptr) {
                Fail(
                    AbsoluteJudgementFatalPredicate::
                        GameplaySoundManagerMissing,
                    AbsoluteJudgementFatalReason::NativeStateMismatch);
            }
            const int cursor_sign = get_group_cursor(
                sound_manager, kGameplaySoundGroup);
            observation = cursor_query.Consume();
            group2_cursor_selected = cursor_sign >= 0;
            if (!group2_cursor_selected) {
                observation.reset();
            }
        }

        const auto now = CaptureAbsoluteHostTimeOrFatal();

        AbsoluteJudgementOuterProbe probe{
            .native = native,
            .group2_cursor_selected = group2_cursor_selected,
            .group2_observation = std::move(observation),
            .endpoint = std::move(endpoint),
            .now = now,
        };
        scheduler_.PrepareOuterCall(probe);
        while (const auto scope = scheduler_.NextScope()) {
            ExecuteScope(*scope, probe);
        }
        scheduler_.FinishOuterCall();

        std::uintptr_t tail{};
        if (!AddAddress(executable_base_, kLoopTailRva, &tail) ||
            tail > (std::numeric_limits<std::uint32_t>::max)()) {
            Fail(
                AbsoluteJudgementFatalPredicate::GameImageAddressInvalid,
                AbsoluteJudgementFatalReason::NativeStateMismatch,
                {executable_base_, kLoopTailRva});
        }
        context.eip = static_cast<std::uint32_t>(tail);
    }

private:
    [[nodiscard]] NativeJudgementConfiguration
    ReadConfigurationOrFatal() const noexcept {
        const auto get_config = reinterpret_cast<AccessorFn>(
            executable_base_ + kGetConfigRva);
        void* const config = get_config();
        std::uint32_t game_time_offset{};
        std::uint32_t hold_safe_frame{};
        std::uint32_t slide_hold_safe_frame{};
        if (config == nullptr) {
            Fail(
                AbsoluteJudgementFatalPredicate::GameConfigurationMissing,
                AbsoluteJudgementFatalReason::NativeStateMismatch);
        }
        const auto config_address =
            reinterpret_cast<std::uintptr_t>(config);
        if (!ReadFieldU32Safe(
                config_address, kGameTimeOffsetOffset, &game_time_offset)) {
            Fail(
                AbsoluteJudgementFatalPredicate::GameConfigurationReadFailed,
                AbsoluteJudgementFatalReason::NativeStateMismatch,
                {config_address, kGameTimeOffsetOffset});
        }
        if (!ReadFieldU32Safe(
                config_address, kHoldSafeFrameOffset, &hold_safe_frame)) {
            Fail(
                AbsoluteJudgementFatalPredicate::GameConfigurationReadFailed,
                AbsoluteJudgementFatalReason::NativeStateMismatch,
                {config_address, kHoldSafeFrameOffset});
        }
        if (!ReadFieldU32Safe(
                config_address,
                kSlideHoldSafeFrameOffset,
                &slide_hold_safe_frame)) {
            Fail(
                AbsoluteJudgementFatalPredicate::GameConfigurationReadFailed,
                AbsoluteJudgementFatalReason::NativeStateMismatch,
                {config_address, kSlideHoldSafeFrameOffset});
        }
        return {
            .game_time_offset_ms =
                static_cast<std::int32_t>(game_time_offset),
            .hold_safe_frame =
                static_cast<std::int32_t>(hold_safe_frame),
            .slide_hold_safe_frame =
                static_cast<std::int32_t>(slide_hold_safe_frame),
        };
    }

    [[nodiscard]] NativeJudgementIdentity ResolveNativeIdentityOrFatal(
        const safetyhook::Context& context) const noexcept {
        std::uintptr_t tune_slot{};
        std::uint32_t tune_raw{};
        if (native_manager_ == 0) {
            Fail(
                AbsoluteJudgementFatalPredicate::
                    SemanticStageMissingAtOwnedLoop,
                AbsoluteJudgementFatalReason::NativeStateMismatch);
        }
        if (!AddSignedAddress(
                static_cast<std::uintptr_t>(context.ebp),
                kTuneStackOffset,
                &tune_slot) || !ReadU32Safe(tune_slot, &tune_raw)) {
            Fail(
                AbsoluteJudgementFatalPredicate::GameImageAddressInvalid,
                AbsoluteJudgementFatalReason::NativeStateMismatch,
                {context.ebp,
                 static_cast<std::uint64_t>(kTuneStackOffset)});
        }
        if (tune_raw == 0) {
            Fail(
                AbsoluteJudgementFatalPredicate::TuneMissing,
                AbsoluteJudgementFatalReason::NativeStateMismatch,
                {tune_slot, tune_raw});
        }
        const auto tune = static_cast<std::uintptr_t>(tune_raw);

        const auto get_global = reinterpret_cast<AccessorFn>(
            executable_base_ + kGetGlobalRva);
        void* const global = get_global();
        std::uint32_t player{};
        if (global == nullptr) {
            Fail(
                AbsoluteJudgementFatalPredicate::GlobalStateMissing,
                AbsoluteJudgementFatalReason::NativeStateMismatch,
                {0});
        }
        if (!ReadFieldU32Safe(
                reinterpret_cast<std::uintptr_t>(global),
                kGlobalPlayerIndexOffset,
                &player)) {
            Fail(
                AbsoluteJudgementFatalPredicate::GlobalStateMissing,
                AbsoluteJudgementFatalReason::NativeStateMismatch,
                {reinterpret_cast<std::uintptr_t>(global)});
        }
        if (player >= 2) {
            Fail(
                AbsoluteJudgementFatalPredicate::PlayerIndexInvalid,
                AbsoluteJudgementFatalReason::NativeStateMismatch,
                {player});
        }

        std::uintptr_t judgement_state{};
        std::uintptr_t score_state{};
        if (!ResolvePointerCollectionElementSafe(
                tune,
                kTuneJudgementStatesOffset,
                player,
                &judgement_state)) {
            Fail(
                AbsoluteJudgementFatalPredicate::JudgementStateMissing,
                AbsoluteJudgementFatalReason::NativeStateMismatch,
                {tune, player});
        }
        if (!ResolvePointerCollectionElementSafe(
                tune,
                kTuneScoreStatesOffset,
                player,
                &score_state)) {
            Fail(
                AbsoluteJudgementFatalPredicate::ScoreStateMissing,
                AbsoluteJudgementFatalReason::NativeStateMismatch,
                {tune, player});
        }

        const auto get_input_manager = reinterpret_cast<AccessorFn>(
            executable_base_ + kGetInputManagerRva);
        void* const input_manager = get_input_manager();
        std::uint32_t booster_raw{};
        if (input_manager == nullptr) {
            Fail(
                AbsoluteJudgementFatalPredicate::InputManagerMissing,
                AbsoluteJudgementFatalReason::NativeStateMismatch,
                {0});
        }
        if (!ReadFieldU32Safe(
                reinterpret_cast<std::uintptr_t>(input_manager),
                kInputManagerBoosterOffset,
                &booster_raw)) {
            Fail(
                AbsoluteJudgementFatalPredicate::InputManagerMissing,
                AbsoluteJudgementFatalReason::NativeStateMismatch,
                {reinterpret_cast<std::uintptr_t>(input_manager)});
        }
        if (booster_raw == 0) {
            Fail(
                AbsoluteJudgementFatalPredicate::BoosterMissing,
                AbsoluteJudgementFatalReason::NativeStateMismatch,
                {reinterpret_cast<std::uintptr_t>(input_manager), 0});
        }
        const auto booster = static_cast<std::uintptr_t>(booster_raw);

        const auto configuration = ReadConfigurationOrFatal();

        return {
            .stage_generation = scheduler_.stage_generation(),
            .tune_manager = native_manager_,
            .tune = tune,
            .judgement_state = judgement_state,
            .score_state = score_state,
            .booster = booster,
            .player = player,
            .game_time_offset_ms = configuration.game_time_offset_ms,
            .hold_safe_frame = configuration.hold_safe_frame,
            .slide_hold_safe_frame =
                configuration.slide_hold_safe_frame,
        };
    }

    [[nodiscard]] static
    std::optional<AbsoluteJudgementNativeScoreCounters>
    ReadScoreCounters(const std::uintptr_t score_state) noexcept {
        AbsoluteJudgementNativeScoreCounters counters{};
        if (!ReadFieldU32Safe(
                score_state, kScoreMissOffset, &counters.miss) ||
            !ReadFieldU32Safe(
                score_state, kScoreGoodOffset, &counters.good) ||
            !ReadFieldU32Safe(
                score_state, kScoreCoolOffset, &counters.cool) ||
            !ReadFieldU32Safe(
                score_state, kScoreGreatOffset, &counters.great)) {
            JudgementDiagnostics().RecordScoreObservationReadFailure();
            return std::nullopt;
        }
        return counters;
    }

    static void IncrementDiagnostic(std::uint64_t& value) noexcept {
        if (value != (std::numeric_limits<std::uint64_t>::max)()) {
            ++value;
        }
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
        bool raw_message_queue_age_available{};
        std::uint32_t raw_message_queue_age_ms{};
        if (scope.kind == JudgementScopeKind::Event) {
            if (scope.event == nullptr) {
                Fail(
                    AbsoluteJudgementFatalPredicate::CommitTopologyMismatch,
                    AbsoluteJudgementFatalReason::NativeStateMismatch,
                    {static_cast<std::uint64_t>(scope.kind),
                     scope.coordinate.sequence});
            }
            held_before = scope.event->transport.held_before;
            held_after = scope.event->transport.held_after;
            rising = scope.event->transport.rising;
            falling = scope.event->transport.falling;
            event_qpc = scope.event->transport.observed_time.qpc_ticks;
            raw_message_queue_age_available =
                scope.event->transport.raw_message_queue_age_available;
            raw_message_queue_age_ms =
                scope.event->transport.raw_message_queue_age_ms;
        } else {
            const auto held = scheduler_.history().OrdinaryHeldAt(
                scope.coordinate.judgement_seconds,
                scope.history_prefix_end_sequence);
            if (!held) {
                scheduler_.FailHistoryInvariant(held.error());
            }
            held_before = *held;
            held_after = *held;
        }

        AbsoluteJudgementQueryCounters scope_queries{};
        AbsoluteJudgementTimingGradeObservations scope_timing_grades{};
        AbsoluteJudgementScoreDeltas score_deltas{};
        AbsoluteJudgementTransientPublications transient_publications{};
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
                .timing_grades = &scope_timing_grades,
            });
            const auto install = query_view.install_result();
            if (!install.installed) {
                FailQueryInvariant(
                    install.invariant,
                    install.history_error,
                    install.failure_operand0,
                    install.failure_operand1,
                    install.failure_operand_count);
            }

            const auto score_before = ReadScoreCounters(
                native.score_state);
            if (score_before) {
                static_cast<void>(
                    scheduler_.ObserveNativeScoreCounters(*score_before));
            }

            const auto recognition = reinterpret_cast<RecognitionFn>(
                executable_base_ + kRecognitionRva);
            recognition(
                reinterpret_cast<void*>(native.judgement_state),
                scope.native_ms,
                scope.native_frame);
            IncrementDiagnostic(
                JudgementDiagnostics().stage_counters().recognition_calls);

            const auto score = reinterpret_cast<ScoreFn>(
                executable_base_ + kScoreRva);
            score(
                reinterpret_cast<void*>(native.score_state),
                scope.native_ms);
            IncrementDiagnostic(
                JudgementDiagnostics().stage_counters().score_calls);

            std::uint8_t arrange{};
            std::uint8_t left_free_tap{};
            std::uint8_t right_free_tap{};
            const bool transient_read = ReadFieldU8Safe(
                    native.judgement_state,
                    kJudgementArrangePublicationOffset,
                    &arrange) &&
                ReadFieldU8Safe(
                    native.judgement_state,
                    kJudgementLeftFreeTapPublicationOffset,
                    &left_free_tap) &&
                ReadFieldU8Safe(
                    native.judgement_state,
                    kJudgementRightFreeTapPublicationOffset,
                    &right_free_tap);
            if (!transient_read) {
                JudgementDiagnostics().
                    RecordTransientPublicationReadFailure();
            } else {
                transient_publications = {
                    .arrange = arrange != 0,
                    .left_free_tap = left_free_tap != 0,
                    .right_free_tap = right_free_tap != 0,
                };
            }

            const auto score_after = ReadScoreCounters(
                native.score_state);
            if (score_after) {
                score_deltas =
                    scheduler_.ObserveNativeScoreCounters(*score_after);
            }
            scheduler_.AccumulateQueryCounters(scope_queries);
            if (transient_read) {
                scheduler_.RecordTransientPublications(
                    transient_publications);
            }
        }

        scheduler_.CommitScope(scope);
        scheduler_.CheckNativeCallInvariantOrFatal();

        gc::timing::CheckedRational delivery_delay =
            gc::timing::CheckedRational::Whole(0);
        if (event_qpc && probe.now.qpc_ticks >= *event_qpc &&
            probe.endpoint) {
            const auto endpoint_info = probe.endpoint->info();
            const auto resolved_delay = gc::timing::CheckedRational::Create(
                probe.now.qpc_ticks - *event_qpc,
                endpoint_info.qpc_frequency);
            if (!resolved_delay) {
                JudgementDiagnostics().
                    RecordDeliveryDelayConversionFailure();
            } else {
                delivery_delay = *resolved_delay;
            }
        }

        const auto& stage_counters =
            JudgementDiagnostics().stage_counters();
        JudgementDiagnostics().ObserveScope({
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
            .raw_message_queue_age_available =
                raw_message_queue_age_available,
            .raw_message_queue_age_ms = raw_message_queue_age_ms,
            .held_before = held_before,
            .held_after = held_after,
            .rising = rising,
            .falling = falling,
            .queries = scope_queries,
            .timing_grades = scope_timing_grades,
            .recognition_completed = true,
            .score_completed = true,
            .score_deltas = score_deltas,
            .transient_publications = transient_publications,
            .batch_kind = scope.kind == JudgementScopeKind::Event
                ? AbsoluteJudgementBatchKind::EventOnly
                : AbsoluteJudgementBatchKind::HeartbeatOnly,
            .isolation_disposition =
                scope.kind == JudgementScopeKind::Event
                    ? AbsoluteJudgementEventIsolationDisposition::
                          EventEndsBatch
                    : AbsoluteJudgementEventIsolationDisposition::
                          HeartbeatOnlyBatch,
            .boundary_committed = scope.commits_boundary,
            .committed_boundary =
                scheduler_.committed_boundary_index().value_or(0),
            .remaining_backlog = stage_counters.pending_work,
        });
    }

    std::uintptr_t executable_base_{};
    std::uintptr_t native_manager_{};
    gc::audio::ExactOutputClockDomain expected_domain_{};
    JudgementScheduler scheduler_;
};

AbsoluteJudgementRuntime& Runtime() noexcept {
    static AbsoluteJudgementRuntime runtime;
    return runtime;
}

} // namespace

void InitializeAbsoluteJudgementRuntime(
    const std::uintptr_t executable_base,
    const gc::audio::ExactOutputClockDomain expected_domain) noexcept {
    Runtime().Initialize(executable_base, expected_domain);
}

void BeginAbsoluteJudgementSemanticStage(
    const std::uintptr_t tune_manager) noexcept {
    const auto stage_entry_time =
        Runtime().CaptureAbsoluteHostTimeOrFatal();
    Runtime().BeginSemanticStage(tune_manager, stage_entry_time);
}

void ObserveAbsoluteJudgementGameplayInitialization(
    const std::uintptr_t tune_manager) noexcept {
    Runtime().ObserveGameplayInitialization(tune_manager);
}

void EndAbsoluteJudgementSemanticStage(
    const std::uintptr_t tune_manager) noexcept {
    Runtime().EndSemanticStage(tune_manager);
}

void EndAbsoluteJudgementSemanticStageForTestMode() noexcept {
    Runtime().EndSemanticStageForTestMode();
}

bool AbsoluteJudgementSemanticStageOpen() noexcept {
    return Runtime().SemanticStageOpen();
}

std::uint64_t AbsoluteJudgementStageGeneration() noexcept {
    return Runtime().stage_generation();
}

[[noreturn]] void FailAbsoluteJudgementQueryInvariant(
    const JudgementQueryInvariant invariant,
    const std::optional<JudgementHistoryError> history_error,
    const std::uint64_t failure_operand0,
    const std::uint64_t failure_operand1,
    const std::uint8_t failure_operand_count) noexcept {
    Runtime().FailQueryInvariant(
        invariant,
        history_error,
        failure_operand0,
        failure_operand1,
        failure_operand_count);
}

[[noreturn]] void FailAbsoluteJudgementActiveStage(
    const AbsoluteJudgementFatalPredicate predicate,
    const AbsoluteJudgementFatalReason category,
    const std::initializer_list<std::uint64_t> operands) noexcept {
    Runtime().Fail(predicate, category, operands);
}

void DispatchAbsoluteJudgementOuterCall(safetyhook::Context& context) {
    Runtime().DispatchOuterCall(context);
}

} // namespace gc::absolute_judgement
